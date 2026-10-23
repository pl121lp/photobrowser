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
  const auto tiff = item.subspan(4 + size_t(offset));
  if (tiff.size() < 4) return {};
  const bool ii = tiff[0] == 'I' && tiff[1] == 'I' && tiff[2] == 42 && tiff[3] == 0;
  const bool mm = tiff[0] == 'M' && tiff[1] == 'M' && tiff[2] == 0 && tiff[3] == 42;
  return ii || mm ? tiff : std::span<const uint8_t>{};
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
