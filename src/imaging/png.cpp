#include "imaging/png.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace pb {

std::optional<Image> decodePng(std::span<const uint8_t> data) {
  if (data.empty() || data.size() > size_t(INT32_MAX)) return std::nullopt;
  int w = 0, h = 0, comp = 0;
  stbi_uc* px = stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &comp, 4);
  if (!px) return std::nullopt;
  Image img;
  img.w = uint32_t(w);
  img.h = uint32_t(h);
  img.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
  stbi_image_free(px);
  return img;
}

}  // namespace pb
