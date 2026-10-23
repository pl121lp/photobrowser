#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pb {

// GPS position: degrees, north and east positive; altitude in metres above sea level.
struct GeoPos {
  double lat = 0, lon = 0;
  std::optional<double> altitude;
};

struct JpegHeaderInfo {
  std::optional<std::string> dateTimeOriginal;  // "YYYY:MM:DD HH:MM:SS"
  int orientation = 1;                          // EXIF 1..8
  uint32_t width = 0, height = 0;               // from SOF, before orientation
  uint64_t thumbOffset = 0, thumbLength = 0;    // embedded preview JPEG, absolute file offset
  bool sawSof = false;
  std::optional<GeoPos> gps;  // only when latitude and longitude are both present
};

// Parses the start of a JPEG file (markers up to SOS). Returns nullopt if the
// data does not start with SOI. Truncated or corrupt data yields what was found.
std::optional<JpegHeaderInfo> parseJpegHeader(std::span<const uint8_t> head);

// "YYYY:MM:DD HH:MM:SS" -> seconds since 1970, treating the naive time as UTC.
std::optional<int64_t> parseExifDateTime(std::string_view s);

}  // namespace pb
