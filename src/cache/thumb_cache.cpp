#include "cache/thumb_cache.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "imaging/jpeg.h"
#include "platform/paths.h"

namespace pb {

namespace fs = std::filesystem;

ThumbCache::ThumbCache(fs::path dir) : dir_(std::move(dir)) {}

fs::path ThumbCache::levelPath(int level, uint64_t key) const {
  char hex[17];
  std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(key));
  return dir_ / ("l" + std::to_string(level)) / std::string(hex, 2) / (std::string(hex) + ".jpg");
}

bool ThumbCache::write(int level, uint64_t key, const Image& img) const {
  auto bytes = encodeJpeg(img, 88);
  return bytes && atomicWriteFile(levelPath(level, key), *bytes);
}

std::optional<Image> ThumbCache::read(int level, uint64_t key) const {
  const fs::path p = levelPath(level, key);
  auto bytes = readWholeFile(p);
  if (!bytes || bytes->empty()) return std::nullopt;
  std::error_code ec;
  fs::last_write_time(p, fs::file_time_type::clock::now(), ec);  // LRU for sweep()
  return decodeJpeg(*bytes);
}

uint64_t ThumbCache::sweep(uint64_t maxBytes) const {
  struct F {
    fs::path path;
    fs::file_time_type time;
    uint64_t size;
  };
  std::vector<F> files;
  uint64_t total = 0;
  std::error_code ec;
  for (int level = 2; level <= 4; ++level) {
    fs::recursive_directory_iterator it(dir_ / ("l" + std::to_string(level)), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      std::error_code e2;
      if (!it->is_regular_file(e2)) continue;
      F f{it->path(), it->last_write_time(e2), it->file_size(e2)};
      if (e2) continue;
      total += f.size;
      files.push_back(std::move(f));
    }
    ec.clear();
  }
  std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.time < b.time; });
  uint64_t freed = 0;
  for (const auto& f : files) {
    if (total <= maxBytes) break;
    if (fs::remove(f.path, ec)) {
      total -= f.size;
      freed += f.size;
    }
  }
  return freed;
}

}  // namespace pb
