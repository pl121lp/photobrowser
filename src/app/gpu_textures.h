#pragma once

#include <array>
#include <memory>
#include <utility>

#include "app/streamer.h"
#include "gfx/paged_texture.h"

namespace pb {

// OpenGL implementation of TextureSink: paged atlases for levels 0..4 (page textures
// from a pre-allocated pool), fixed-capacity full-resolution textures, and a
// placeholder for photos with nothing loaded.
class GpuTextures final : public TextureSink {
 public:
  GpuTextures();
  ~GpuTextures() override;

  // Pre-allocates GPU memory a little at a time (call once per frame when not busy):
  // page textures until `pages` exist, and full textures large enough for `viewport`
  // (pass the display size so going fullscreen needs no reallocation).
  // Returns true if it did work this call.
  bool prewarm(size_t pages, Vec2d viewport);

  void addPage(int level, uint16_t page) override;
  void releasePage(int level, uint16_t page) override;
  void upload(int level, uint16_t page, Vec2d origin, const std::vector<Image>& mips) override;
  Vec2d fullCapacity() const override { return fullCapacity_; }
  UvRect beginFull(int index, uint32_t w, uint32_t h) override;
  void uploadFullRows(int index, int mip, uint32_t y0, uint32_t rows, const Image& m) override;

  GLuint texture(const TexRef& r) const;
  void endFrame() { ring_.endFrame(); }  // submit this frame's uploads
  double takeUploadMs() { return std::exchange(uploadMs_, 0.0); }  // time spent in GL uploads

 private:
  struct FullTexture {
    GLuint tex = 0;
    uint32_t w = 0, h = 0;  // capacity
    int maxLevel = 0;
  };
  bool ensureFull(int index, uint32_t w, uint32_t h);  // true if (re)allocated

  UploadRing ring_;
  std::array<std::unique_ptr<PagedTexture>, kLevelCount> levels_;
  std::vector<GLuint> pool_;  // free page textures
  size_t pagesCreated_ = 0;
  std::array<FullTexture, Streamer::kFullSlots> full_{};
  GLuint placeholder_ = 0;
  double uploadMs_ = 0;
  Vec2d fullCapacity_{1920, 1080};
};

}  // namespace pb
