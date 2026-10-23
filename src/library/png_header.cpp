#include "library/png_header.h"

#include <cstring>

namespace pb {

namespace {
constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

uint32_t be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
}  // namespace

std::optional<PngHeaderInfo> parsePngHeader(std::span<const uint8_t> head) {
  if (head.size() < 8 || std::memcmp(head.data(), kSignature, 8) != 0) return std::nullopt;
  PngHeaderInfo info;
  bool sawIhdr = false;
  size_t pos = 8;
  while (pos + 8 <= head.size()) {
    const uint32_t len = be32(&head[pos]);
    const char* type = reinterpret_cast<const char*>(&head[pos + 4]);
    if (std::memcmp(type, "IDAT", 4) == 0 || std::memcmp(type, "IEND", 4) == 0) break;
    const size_t data = pos + 8;
    if (len > head.size() - data) break;  // truncated chunk
    if (std::memcmp(type, "IHDR", 4) == 0) {
      if (len != 13) return std::nullopt;
      info.width = be32(&head[data]);
      info.height = be32(&head[data + 4]);
      sawIhdr = true;
    } else if (sawIhdr && std::memcmp(type, "eXIf", 4) == 0 && !info.exif) {
      info.exif = parseExifBlock(head.subspan(data, len));
    }
    if (len > head.size() - data || head.size() - data - len < 4) break;  // no room for CRC
    pos = data + len + 4;
  }
  if (!sawIhdr) return std::nullopt;
  return info;
}

}  // namespace pb
