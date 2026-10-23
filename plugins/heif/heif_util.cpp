#include "heif_util.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace pbheif {

std::span<const uint8_t> exifTiffFromItem(std::span<const uint8_t> item) {
  if (item.size() < 4) return {};
  const uint64_t offset = (uint64_t(item[0]) << 24) | (uint64_t(item[1]) << 16) | (uint64_t(item[2]) << 8) | item[3];
  if (offset > item.size() - 4) return {};
  auto tiff = item.subspan(4 + size_t(offset));
  const auto isTiff = [](std::span<const uint8_t> s) {
    if (s.size() < 4) return false;
    const bool ii = s[0] == 'I' && s[1] == 'I' && s[2] == 42 && s[3] == 0;
    const bool mm = s[0] == 'M' && s[1] == 'M' && s[2] == 0 && s[3] == 42;
    return ii || mm;
  };
  if (isTiff(tiff)) return tiff;
  // Some writers leave offset 0 and keep the "Exif\0\0" prefix in the payload.
  static constexpr uint8_t kExifPrefix[6] = {'E', 'x', 'i', 'f', 0, 0};
  if (tiff.size() > 6 && std::equal(std::begin(kExifPrefix), std::end(kExifPrefix), tiff.begin())) {
    tiff = tiff.subspan(6);
    if (isTiff(tiff)) return tiff;
  }
  return {};
}

int chooseThumbnail(std::span<const std::pair<uint32_t, uint32_t>> thumbSizes, uint32_t minLongEdge) {
  int best = -1;
  uint32_t bestEdge = 0;
  for (size_t i = 0; i < thumbSizes.size(); ++i) {
    const uint32_t edge = std::max(thumbSizes[i].first, thumbSizes[i].second);
    if (edge >= minLongEdge && (best < 0 || edge < bestEdge)) {
      best = int(i);
      bestEdge = edge;
    }
  }
  return best;
}

std::optional<std::vector<uint8_t>> readFileUtf8(const char* pathUtf8) {
  if (!pathUtf8) return std::nullopt;
  const std::u8string u8(reinterpret_cast<const char8_t*>(pathUtf8));
  std::ifstream f(std::filesystem::path(u8), std::ios::binary | std::ios::ate);
  if (!f) return std::nullopt;
  const std::streamoff size = f.tellg();
  if (size < 0) return std::nullopt;
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  f.seekg(0);
  if (size > 0 && !f.read(reinterpret_cast<char*>(bytes.data()), size)) return std::nullopt;
  return bytes;
}

}  // namespace pbheif
