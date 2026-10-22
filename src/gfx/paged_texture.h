#pragma once

#include <cstdint>
#include <vector>

#include "gfx/gl.h"
#include "imaging/image.h"
#include "stream/slot_allocator.h"

namespace pb {

// GPU side of one pyramid level: one 4096x4096 sRGB GL_TEXTURE_2D per page, with a
// mip chain down to 1 texel per slot (power-of-two slots keep mips from bleeding).
class PagedTexture {
 public:
  explicit PagedTexture(uint32_t slotSize);
  ~PagedTexture();
  PagedTexture(const PagedTexture&) = delete;
  PagedTexture& operator=(const PagedTexture&) = delete;

  void addPage(uint16_t page);
  void releasePage(uint16_t page);
  // Uploads `mips` (mips[0] <= slot size) into the slot at `origin` (texels).
  void upload(uint16_t page, Vec2d origin, const std::vector<Image>& mips);
  GLuint texture(uint16_t page) const { return page < pages_.size() ? pages_[page] : 0; }
  uint32_t slotSize() const { return slotSize_; }

 private:
  uint32_t slotSize_;
  int maxLevel_;
  std::vector<GLuint> pages_;
};

// Single-image texture with mips (whole-photo view, placeholders).
GLuint createImageTexture(const Image& img, bool mipmaps);

}  // namespace pb
