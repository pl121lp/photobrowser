#pragma once

#include <array>
#include <memory>

#include "app/streamer.h"
#include "gfx/paged_texture.h"

namespace pb {

// OpenGL implementation of TextureSink: paged atlases for levels 0..4, a few
// full-resolution textures, and a placeholder for photos with nothing loaded.
class GpuTextures final : public TextureSink {
 public:
  GpuTextures();
  ~GpuTextures() override;

  void addPage(int level, uint16_t page) override;
  void releasePage(int level, uint16_t page) override;
  void upload(int level, uint16_t page, Vec2d origin, const std::vector<Image>& mips) override;
  void uploadFull(int index, const std::vector<Image>& mips) override;

  GLuint texture(const TexRef& r) const;

 private:
  std::array<std::unique_ptr<PagedTexture>, kLevelCount> levels_;
  std::array<GLuint, Streamer::kFullSlots> full_{};
  GLuint placeholder_ = 0;
};

}  // namespace pb
