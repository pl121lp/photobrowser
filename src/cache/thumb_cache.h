#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "imaging/image.h"

namespace pb {

// Per-photo JPEG thumbnails for levels 1..4 at <dir>/l<level>/<hh>/<16 hex key>.jpg.
// Thread-safe for different keys (files are written atomically).
class ThumbCache {
 public:
  explicit ThumbCache(std::filesystem::path dir);

  const std::filesystem::path& dir() const { return dir_; }
  std::filesystem::path levelPath(int level, uint64_t key) const;
  bool write(int level, uint64_t key, const Image& img) const;
  std::optional<Image> read(int level, uint64_t key) const;  // refreshes the file's mtime
  // Deletes least recently used lazy files (levels >= 2) until they total <= maxBytes.
  uint64_t sweep(uint64_t maxBytes) const;

 private:
  std::filesystem::path dir_;
};

}  // namespace pb
