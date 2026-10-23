#include "library/catalog.h"

#include <algorithm>
#include <cstring>

#include "platform/paths.h"

namespace pb {

namespace {

constexpr char kMagic[8] = {'P', 'B', 'C', 'A', 'T', 0, 0, 3};
constexpr uint32_t kEndMarker = 0x21444E45;  // "END!"

class Writer {
 public:
  std::vector<uint8_t> out;
  template <class T>
  void put(T v) {
    static_assert(std::is_integral_v<T>);
    for (size_t i = 0; i < sizeof(T); ++i) out.push_back(uint8_t(uint64_t(v) >> (8 * i)));
  }
  void str(const std::string& s) {
    put(uint32_t(s.size()));
    out.insert(out.end(), s.begin(), s.end());
  }
};

class Reader {
 public:
  explicit Reader(const std::vector<uint8_t>& d) : d_(d) {}
  bool ok = true;
  template <class T>
  T get() {
    if (pos_ + sizeof(T) > d_.size()) {
      ok = false;
      return T{};
    }
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(T); ++i) v |= uint64_t(d_[pos_ + i]) << (8 * i);
    pos_ += sizeof(T);
    return T(v);
  }
  std::string str() {
    const uint32_t n = get<uint32_t>();
    if (!ok || pos_ + n > d_.size()) {
      ok = false;
      return {};
    }
    std::string s(d_.begin() + long(pos_), d_.begin() + long(pos_ + n));
    pos_ += n;
    return s;
  }
  bool magic() {
    if (d_.size() < sizeof kMagic || std::memcmp(d_.data(), kMagic, sizeof kMagic) != 0) return false;
    pos_ = sizeof kMagic;
    return true;
  }

 private:
  const std::vector<uint8_t>& d_;
  size_t pos_ = 0;
};

}  // namespace

uint64_t cacheKey(std::string_view absPathUtf8, uint64_t size, int64_t mtime) {
  uint64_t h = fnv1a(absPathUtf8.data(), absPathUtf8.size());
  h = fnv1a(&size, sizeof size, h);
  return fnv1a(&mtime, sizeof mtime, h);
}

void Catalog::sortOrder(bool newestFirst) {
  order.clear();
  for (PhotoId i = 0; i < photos.size(); ++i) {
    if (!(photos[i].flags & kBroken)) order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [&](PhotoId a, PhotoId b) {
    const auto& pa = photos[a];
    const auto& pb = photos[b];
    if (pa.takenAt != pb.takenAt) return pa.takenAt < pb.takenAt;
    return pa.relPath < pb.relPath;
  });
  if (newestFirst) std::reverse(order.begin(), order.end());
}

bool saveCatalog(const Catalog& c, const std::filesystem::path& path) {
  Writer w;
  w.out.assign(kMagic, kMagic + sizeof kMagic);
  w.str(c.formats);
  w.put(uint8_t(c.recursive));
  w.put(uint32_t(c.photos.size()));
  for (const auto& p : c.photos) {
    w.str(p.relPath);
    w.put(p.fileSize);
    w.put(p.mtime);
    w.put(p.takenAt);
    w.put(p.width);
    w.put(p.height);
    w.put(p.orientation);
    w.put(p.flags);
    w.put(p.key);
    w.put(p.thumbOffset);
    w.put(p.thumbLength);
  }
  w.put(uint32_t(c.order.size()));
  for (PhotoId id : c.order) w.put(id);
  w.put(kEndMarker);
  return atomicWriteFile(path, w.out);
}

std::optional<Catalog> loadCatalog(const std::filesystem::path& path) {
  auto data = readWholeFile(path);
  if (!data) return std::nullopt;
  Reader r(*data);
  if (!r.magic()) return std::nullopt;
  Catalog c;
  c.formats = r.str();
  c.recursive = r.get<uint8_t>() != 0;
  const uint32_t n = r.get<uint32_t>();
  if (!r.ok || n > data->size()) return std::nullopt;
  c.photos.resize(n);
  for (auto& p : c.photos) {
    p.relPath = r.str();
    p.fileSize = r.get<uint64_t>();
    p.mtime = r.get<int64_t>();
    p.takenAt = r.get<int64_t>();
    p.width = r.get<uint32_t>();
    p.height = r.get<uint32_t>();
    p.orientation = r.get<uint8_t>();
    p.flags = r.get<uint8_t>();
    p.key = r.get<uint64_t>();
    p.thumbOffset = r.get<uint64_t>();
    p.thumbLength = r.get<uint64_t>();
    if (!r.ok) return std::nullopt;
  }
  const uint32_t m = r.get<uint32_t>();
  if (!r.ok || m > n) return std::nullopt;
  c.order.resize(m);
  for (auto& id : c.order) {
    id = r.get<uint32_t>();
    if (id >= n) return std::nullopt;
  }
  if (r.get<uint32_t>() != kEndMarker || !r.ok) return std::nullopt;
  return c;
}

}  // namespace pb
