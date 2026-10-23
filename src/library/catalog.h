#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "library/exif.h"

namespace pb {

using PhotoId = uint32_t;  // index into Catalog::photos (stable within a session)

enum PhotoFlags : uint8_t {
  kBroken = 1,          // unreadable / not a JPEG: excluded from the layout
  kNeedsDecodeL1 = 2,   // no embedded preview: L0/L1 must come from the original
};

struct PhotoRecord {
  std::string relPath;  // UTF-8, '/' separators, relative to the library root
  uint64_t fileSize = 0;
  int64_t mtime = 0;    // seconds since 1970
  int64_t takenAt = 0;  // EXIF DateTimeOriginal (naive, as UTC) or mtime
  uint32_t width = 0, height = 0;  // after EXIF orientation; 0 if unknown
  uint8_t orientation = 1;
  uint8_t flags = 0;
  uint64_t key = 0;  // cacheKey(): names this photo's files in the thumbnail cache
  uint64_t thumbOffset = 0, thumbLength = 0;  // embedded preview JPEG in the file
  std::optional<GeoPos> gps;                  // EXIF GPS position, if any

  double aspect() const { return width && height ? double(width) / height : 1.0; }
};

uint64_t cacheKey(std::string_view absPathUtf8, uint64_t size, int64_t mtime);

struct Catalog {
  std::vector<PhotoRecord> photos;  // index == PhotoId
  std::vector<PhotoId> order;       // layout order: (takenAt, relPath), broken excluded
  std::string formats;              // MediaFormats::signature() it was indexed with
  bool recursive = true;            // whether subfolders were scanned
  void sortOrder(bool newestFirst = false);  // oldest first unless asked otherwise
};

bool saveCatalog(const Catalog& c, const std::filesystem::path& path);
std::optional<Catalog> loadCatalog(const std::filesystem::path& path);

}  // namespace pb
