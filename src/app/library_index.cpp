#include "app/library_index.h"

#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "library/indexer.h"
#include "platform/paths.h"

namespace pb {

namespace fs = std::filesystem;

namespace {
constexpr int64_t kIndexPriority = 1'000'000'000;  // behind any visible-tile work

bool save(const fs::path& libDir, const LibraryState& st) {
  return saveCatalog(st.catalog, libDir / "catalog.bin") && writeL0Pack(libDir / "l0.pack", st.l0);
}
}  // namespace

std::optional<LibraryState> loadWarm(const fs::path& libDir) {
  auto catalog = loadCatalog(libDir / "catalog.bin");
  if (!catalog) return std::nullopt;
  auto l0 = readL0Pack(libDir / "l0.pack");
  if (!l0) return std::nullopt;
  return LibraryState{std::move(*catalog), std::move(*l0)};
}

std::pair<PhotoRecord, Image> indexOne(const fs::path& root, const ScanEntry& e, const ThumbCache& thumbs) {
  QuickResult q = quickIndex(root, e);
  if (q.rec.flags & kNeedsDecodeL1) {
    if (!decodeL0L1FromOriginal(root / fromUtf8(e.relPath), q.rec, q.l0, q.l1)) q.rec.flags |= kBroken;
  }
  if (!(q.rec.flags & kBroken) && !q.l1.empty()) {
    thumbs.write(1, q.rec.key, q.l1);
    if (!q.rec.width) {
      q.rec.width = q.l1.w;  // best effort aspect when the header had no SOF
      q.rec.height = q.l1.h;
    }
  }
  return {std::move(q.rec), std::move(q.l0)};
}

Indexer::Indexer(fs::path root, fs::path libDir, JobSystem& jobs, const ThumbCache& thumbs)
    : root_(std::move(root)), libDir_(std::move(libDir)), jobs_(jobs), thumbs_(thumbs) {}

Indexer::~Indexer() {
  cancelled_ = true;
  while (outstanding_ > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void Indexer::start() {
  ++outstanding_;
  jobs_.submit(kIndexPriority, [this] {
    if (cancelled_) {
      --outstanding_;
      return;
    }
    auto scan = scanFolder(root_);
    total_ = uint32_t(scan.size());
    scanned_ = true;
    if (scan.empty()) {
      finish();
      --outstanding_;
      return;
    }
    {
      std::lock_guard lock(m_);
      results_.resize(scan.size());
    }
    for (size_t i = 0; i < scan.size(); ++i) {
      ++outstanding_;
      jobs_.submit(kIndexPriority + 1, [this, i, e = scan[i]] {
        if (cancelled_) {
          --outstanding_;
          return;
        }
        auto r = indexOne(root_, e, thumbs_);
        bool last;
        {
          std::lock_guard lock(m_);
          results_[i] = std::move(r);
          last = ++done_ == total_;
        }
        if (last) finish();
        --outstanding_;
      });
    }
    --outstanding_;
  });
}

void Indexer::finish() {
  std::lock_guard lock(m_);
  for (auto& [rec, l0] : results_) {
    if (!l0.empty()) state_.l0.emplace_back(rec.key, std::move(l0));
    state_.catalog.photos.push_back(std::move(rec));
  }
  results_.clear();
  state_.catalog.sortOrder();
  fs::create_directories(libDir_);
  save(libDir_, state_);
  finished_ = true;
}

LibraryState Indexer::take() {
  std::lock_guard lock(m_);
  return std::move(state_);
}

bool refreshLibrary(const fs::path& root, const fs::path& libDir, const LibraryState& current,
                    const ThumbCache& thumbs, const std::atomic<bool>* stop) {
  auto stopped = [&] { return stop && stop->load(); };
  if (stopped()) return false;
  const auto scan = scanFolder(root);
  const ScanDiff diff = diffCatalog(current.catalog, scan);
  if (diff.empty()) return false;

  std::unordered_set<PhotoId> drop(diff.removed.begin(), diff.removed.end());
  drop.insert(diff.changed.begin(), diff.changed.end());
  std::unordered_map<uint64_t, const Image*> l0ByKey;
  for (const auto& [key, img] : current.l0) l0ByKey.emplace(key, &img);

  LibraryState next;
  for (PhotoId id = 0; id < current.catalog.photos.size(); ++id) {
    if (drop.contains(id)) continue;
    const auto& rec = current.catalog.photos[id];
    next.catalog.photos.push_back(rec);
    if (auto it = l0ByKey.find(rec.key); it != l0ByKey.end()) next.l0.emplace_back(rec.key, *it->second);
  }
  std::vector<ScanEntry> toIndex = diff.added;
  for (PhotoId id : diff.changed) {
    const auto& rel = current.catalog.photos[id].relPath;
    for (const auto& e : scan) {
      if (e.relPath == rel) toIndex.push_back(e);
    }
  }
  for (const auto& e : toIndex) {
    if (stopped()) return false;
    auto [rec, l0] = indexOne(root, e, thumbs);
    if (!l0.empty()) next.l0.emplace_back(rec.key, std::move(l0));
    next.catalog.photos.push_back(std::move(rec));
  }
  next.catalog.sortOrder();
  return save(libDir, next);
}

}  // namespace pb
