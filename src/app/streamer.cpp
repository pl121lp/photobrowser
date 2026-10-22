#include "app/streamer.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>

#include "imaging/resize.h"
#include "layout/content.h"
#include "library/indexer.h"
#include "platform/paths.h"

namespace pb {

namespace {
constexpr float kFadeSeconds = 0.15f;
constexpr int64_t kRingPenalty = 100'000;  // off-screen prefetch ring loads after on-screen ones
constexpr int64_t kFullPriority = -1;      // whole-photo view goes first

size_t mipBytes(const std::vector<Image>& mips) {
  size_t n = 0;
  for (const auto& m : mips) n += m.rgba.size();
  return n;
}
}  // namespace

Streamer::Streamer(const Catalog& catalog, std::filesystem::path photoRoot, const ThumbCache& thumbs,
                   JobSystem& jobs, TextureSink& sink, size_t vramBudgetBytes)
    : catalog_(catalog),
      root_(std::move(photoRoot)),
      thumbs_(thumbs),
      jobs_(jobs),
      sink_(sink),
      residency_(uint32_t(catalog.photos.size()), vramBudgetBytes),
      l0Dims_(catalog.photos.size()),
      desired_(catalog.photos.size(), -1),
      shown_(catalog.photos.size()) {}

Streamer::~Streamer() {
  guard_->alive = false;
  while (guard_->running > 0) std::this_thread::yield();
}

void Streamer::setL0(const L0Entries& entries) {
  std::unordered_map<uint64_t, PhotoId> byKey;
  for (PhotoId id = 0; id < catalog_.photos.size(); ++id) byKey.emplace(catalog_.photos[id].key, id);
  const uint32_t perPage = l0Slots_.slotsPerPage();
  for (const auto& [key, img] : entries) {
    auto it = byKey.find(key);
    if (it == byKey.end() || img.empty()) continue;
    const PhotoId id = it->second;
    const SlotRef s{uint16_t(id / perPage), uint16_t(id % perPage)};
    while (l0Slots_.pageSlots() <= s.page) sink_.addPage(0, l0Slots_.addPage());
    sink_.upload(0, s.page, l0Slots_.origin(s), buildMips(img));
    l0Dims_[id] = {uint16_t(img.w), uint16_t(img.h)};
  }
}

TexRef Streamer::l0Ref(PhotoId id) const {
  const auto [w, h] = l0Dims_[id];
  if (!w) return {};
  const uint32_t perPage = l0Slots_.slotsPerPage();
  const SlotRef s{uint16_t(id / perPage), uint16_t(id % perPage)};
  return {0, s.page, l0Slots_.uv(s, w, h)};
}

TexRef Streamer::refFor(PhotoId id, int level) {
  if (level <= 0) return l0Ref(id);
  if (auto s = residency_.resident(id, level)) {
    return {level, s->slot.page, residency_.pool(level).uv(s->slot, s->w, s->h)};
  }
  return l0Ref(id);
}

void Streamer::submit(const Residency::Request& r) {
  const PhotoRecord rec = catalog_.photos[r.id];
  jobs_.submit(r.priority, [results = results_, guard = guard_, rec, req = r, root = root_, &thumbs = thumbs_] {
    ++guard->running;
    struct Done {
      JobGuard& g;
      ~Done() { --g.running; }
    } done{*guard};
    if (!guard->alive || req.cancel->cancelled) return;
    std::optional<Image> img = thumbs.read(req.level, rec.key);
    if (!img) {
      if (req.cancel->cancelled) return;
      const auto abs = root / fromUtf8(rec.relPath);
      if (req.level == 1) {
        PhotoRecord copy = rec;
        Image l0, l1;
        if (decodeL0L1FromOriginal(abs, copy, l0, l1)) {
          thumbs.write(1, rec.key, l1);
          img = std::move(l1);
        }
      } else {
        auto levels = buildLazyLevels(abs, rec, req.level);
        for (int l = 2; l <= req.level; ++l) {
          if (!levels[size_t(l)].empty()) thumbs.write(l, rec.key, levels[size_t(l)]);
        }
        if (!levels[size_t(req.level)].empty()) img = std::move(levels[size_t(req.level)]);
      }
    }
    results->push({req.id, req.level, img ? buildMips(*img) : std::vector<Image>{}});
  });
}

std::optional<TexRef> Streamer::fullFor(PhotoId id, double now, uint32_t longEdge, bool request) {
  for (int i = 0; i < kFullSlots; ++i) {
    Full& f = full_[size_t(i)];
    if (f.used && f.id == id) {
      f.lastUsed = now;
      if (f.loading) return std::nullopt;
      return TexRef{kFullLevel, uint16_t(i), {0, 0, 1, 1}};
    }
  }
  if (!request) return std::nullopt;
  // Take a free slot or the least recently used one that is not loading.
  int victim = -1;
  for (int i = 0; i < kFullSlots; ++i) {
    const Full& f = full_[size_t(i)];
    if (!f.used) {
      victim = i;
      break;
    }
    if (!f.loading && (victim < 0 || f.lastUsed < full_[size_t(victim)].lastUsed)) victim = i;
  }
  if (victim < 0) return std::nullopt;
  Full& f = full_[size_t(victim)];
  f = Full{true, true, id, 0, 0, longEdge, now, std::make_shared<CancelToken>()};
  const PhotoRecord rec = catalog_.photos[id];
  jobs_.submit(kFullPriority, [results = results_, guard = guard_, rec, id, longEdge, cancel = f.cancel,
                               root = root_] {
    ++guard->running;
    struct Done {
      JobGuard& g;
      ~Done() { --g.running; }
    } done{*guard};
    if (!guard->alive || cancel->cancelled) return;
    auto img = decodeFull(root / fromUtf8(rec.relPath), rec, longEdge);
    std::vector<Image> mips;
    if (img && !cancel->cancelled) mips = buildMips(fitToLongEdge(*img, longEdge));
    results->push({id, kFullLevel, std::move(mips)});
  });
  return std::nullopt;
}

void Streamer::applyPageEvents() {
  for (const auto& e : residency_.takePageEvents()) {
    if (e.added) {
      sink_.addPage(e.level, e.page);
    } else {
      sink_.releasePage(e.level, e.page);
    }
  }
}

void Streamer::frame(double now, const Camera& cam, const Layout& layout, std::optional<uint32_t> focusedPos,
                     std::vector<TileDraw>& out) {
  out.clear();
  requests_.clear();
  residency_.beginFrame();

  // 1. Upload finished loads within the per-frame budget.
  size_t budget = uploadBudget_;
  uploadsThisFrame_ = 0;
  while (true) {
    auto batch = results_->drain(1);
    if (batch.empty()) break;
    Result& r = batch.front();
    const size_t bytes = mipBytes(r.mips);
    if (r.level == kFullLevel) {
      for (int i = 0; i < kFullSlots; ++i) {
        Full& f = full_[size_t(i)];
        if (!f.used || f.id != r.id || !f.loading) continue;
        if (r.mips.empty()) {
          f.used = false;
        } else {
          sink_.uploadFull(i, r.mips);
          f.loading = false;
          f.w = r.mips[0].w;
          f.h = r.mips[0].h;
        }
      }
    } else if (r.mips.empty()) {
      residency_.requestFailed(r.id, r.level);
    } else if (auto slot = residency_.place(r.id, r.level, r.mips[0].w, r.mips[0].h)) {
      applyPageEvents();
      sink_.upload(r.level, slot->page, residency_.pool(r.level).origin(*slot), r.mips);
    }
    ++uploadsThisFrame_;
    if (bytes >= budget) break;
    budget -= bytes;
  }

  // 2. Decide what each visible photo (plus a one-cell ring) needs.
  const Rect view = cam.visibleWorld();
  visible_.clear();
  layout.visible({view.x0 - 1, view.y0 - 1, view.x1 + 1, view.y1 + 1}, visible_);
  const Vec2d mid = cam.viewport * 0.5;
  const uint32_t windowLong = uint32_t(std::max(cam.viewport.x, cam.viewport.y));
  for (uint32_t pos : visible_) {
    const PhotoId id = catalog_.order[pos];
    const PhotoRecord& rec = catalog_.photos[id];
    const Placement pl = layout.place(pos);
    const bool onScreen = pl.cell.intersects(view);
    const bool focused = focusedPos && *focusedPos == pos;
    const double tilePx = pl.photo.width() * cam.scale;
    const double a = rec.aspect();
    // Crop shows the short edge across the tile, so the long edge needs more pixels.
    double needed = tilePx / std::min(a, 1 / a);
    if (focused) {
      const Rect fit = fitRect({0, 0, cam.viewport.x, cam.viewport.y}, a);
      needed = std::max(fit.width(), fit.height());
    }
    const int want = desiredLevel(needed, desired_[id]);
    desired_[id] = int8_t(want);
    const double dist = length(cam.worldToScreen(pl.cell.center()) - mid);
    auto src = residency_.touch(id, want, dist + (onScreen ? 0 : kRingPenalty), requests_);

    if (!onScreen) continue;
    TileDraw d;
    d.pos = pos;
    d.id = id;
    std::optional<TexRef> cur;
    const bool wantFull = want == kFullLevel || focused;
    if (wantFull) cur = fullFor(id, now, std::max(uint32_t(needed), windowLong / 2), true);
    if (!cur && src) cur = TexRef{src->level, src->slot.page, residency_.pool(src->level).uv(src->slot, src->w, src->h)};
    if (!cur) cur = l0Ref(id);
    d.cur = *cur;

    // Cross-fade when a sharper level replaces a real image (not the placeholder).
    Shown& sh = shown_[id];
    if (d.cur.level != sh.level) {
      sh.from = sh.level;
      sh.level = int8_t(d.cur.level);
      sh.since = sh.from >= 0 && d.cur.level > sh.from ? float(now) : -1e9f;
    }
    d.fade = std::clamp(float(now - sh.since) / kFadeSeconds, 0.0f, 1.0f);
    d.prev = d.fade < 1 ? refFor(id, sh.from) : d.cur;
    out.push_back(d);
  }

  // 3. Hand new requests to the workers.
  for (const auto& r : requests_) submit(r);
  residency_.endFrame();
  applyPageEvents();
}

StreamStats Streamer::stats() const {
  return {jobs_.pending(), residency_.pendingRequests(), residency_.bytesUsed(), uploadsThisFrame_};
}

}  // namespace pb
