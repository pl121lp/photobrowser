#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "imaging/image.h"
#include "library/exif.h"

namespace pb {

struct MediaProbe {
  uint32_t width = 0, height = 0;  // as displayed
  int64_t takenAt = 0;             // seconds, local wall-clock as UTC; 0 = unknown
  std::optional<GeoPos> gps;
};

// A non-JPEG file type the library can show (provided by a plugin). Called from
// worker threads, concurrently.
class MediaHandler {
 public:
  virtual ~MediaHandler() = default;
  virtual std::optional<MediaProbe> probe(const std::filesystem::path& abs) const = 0;
  // Upright still image, long edge >= minLongEdge (or natural size if smaller).
  virtual std::optional<Image> decodeStill(const std::filesystem::path& abs, uint32_t minLongEdge) const = 0;
};

// File extensions the library accepts beyond JPEG, and who handles them. Set up once
// before indexing starts; read-only (and so thread-safe) afterwards.
class MediaFormats {
 public:
  static const MediaFormats& none();  // JPEG only

  // `ext` like ".mp4" (any case). A later handler for the same extension is ignored.
  // `origin` names who handles it (e.g. "video 1.1"); it is part of the signature.
  void add(std::string ext, const MediaHandler* handler, std::string origin = {});
  // Handler for a path's extension; nullptr for JPEG and unknown types.
  const MediaHandler* find(std::string_view path) const;
  bool accepts(std::string_view path) const;  // JPEG or a registered extension
  // Stable text naming the registered extensions and their origins ("" if none);
  // stored with the catalog so enabling, disabling or upgrading a plugin re-indexes
  // the library.
  std::string signature() const;

 private:
  struct Entry {
    std::string ext;
    const MediaHandler* handler;
    std::string origin;
  };
  std::vector<Entry> handlers_;
};

std::string lowerExtension(std::string_view path);  // ".jpg" for "a/B.JPG"; "" if none

}  // namespace pb
