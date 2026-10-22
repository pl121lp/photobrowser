#pragma once

#include <cstdint>
#include <vector>

#include "gfx/gl.h"
#include "imaging/image.h"
#include "stream/slot_allocator.h"

namespace pb {

// Streams texture uploads through a ring of persistent pixel-unpack buffers so
// neither buffer allocation nor GPU synchronization happens on the frame's path.
// Each frame's uploads go to one buffer; it is reused only after the GPU passed
// that frame's fence.
class UploadRing {
 public:
  static constexpr int kBuffers = 3;
  static constexpr size_t kBufferBytes = 16u << 20;

  ~UploadRing();
  // `rowLength` is the source row length in pixels. Falls back to a direct upload
  // when the frame's buffer is full.
  void upload(GLuint texture, int level, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rowLength,
              const uint8_t* rgba);
  void endFrame();  // fence this frame's buffer, move to the next

 private:
  bool begin();
  struct Pending {
    GLuint texture;
    int level;
    uint32_t x, y, w, h;
    size_t offset;
  };
  std::vector<Pending> pending_;
  GLuint buffers_[kBuffers] = {};
  GLsync fences_[kBuffers] = {};
  uint8_t* mapped_ = nullptr;
  size_t used_ = 0;
  int current_ = 0;
};

// A 4096x4096 sRGB texture with a full mip chain. Allocating one costs ~100 ms on
// some drivers, so pages are pooled and created ahead of need.
GLuint createPageTexture();

// GPU side of one pyramid level: page textures (taken from a pool) split into
// fixed slots. GL_TEXTURE_MAX_LEVEL is limited to the slot size so mips never bleed.
class PagedTexture {
 public:
  PagedTexture(uint32_t slotSize, UploadRing& ring);

  void addPage(uint16_t page, GLuint texture);
  GLuint releasePage(uint16_t page);  // returns the texture to the caller's pool
  // Uploads `mips` (mips[0] <= slot size) into the slot at `origin` (texels).
  void upload(uint16_t page, Vec2d origin, const std::vector<Image>& mips);
  GLuint texture(uint16_t page) const { return page < pages_.size() ? pages_[page] : 0; }

 private:
  uint32_t slotSize_;
  int maxLevel_;
  UploadRing& ring_;
  std::vector<GLuint> pages_;
};

// Single-image texture from a prepared mip chain (mips[0] = full size).
GLuint createTexture(const std::vector<Image>& mips);
GLuint createImageTexture(const Image& img, bool mipmaps);
// Empty sRGB texture with a full mip chain, every level touched so the driver
// allocates it now rather than on first use.
GLuint createEmptyTexture(uint32_t w, uint32_t h);

}  // namespace pb
