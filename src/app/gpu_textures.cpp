#include "app/gpu_textures.h"

namespace pb {

GpuTextures::GpuTextures() {
  for (int l = 0; l < kLevelCount; ++l) levels_[size_t(l)] = std::make_unique<PagedTexture>(kSlotSize[l]);
  placeholder_ = createImageTexture(Image{1, 1, {38, 38, 40, 255}}, false);
}

GpuTextures::~GpuTextures() {
  for (GLuint& t : full_) {
    if (t) gl::DeleteTextures(1, &t);
  }
  gl::DeleteTextures(1, &placeholder_);
}

void GpuTextures::addPage(int level, uint16_t page) { levels_[size_t(level)]->addPage(page); }
void GpuTextures::releasePage(int level, uint16_t page) { levels_[size_t(level)]->releasePage(page); }

void GpuTextures::upload(int level, uint16_t page, Vec2d origin, const std::vector<Image>& mips) {
  levels_[size_t(level)]->upload(page, origin, mips);
}

void GpuTextures::uploadFull(int index, const std::vector<Image>& mips) {
  GLuint& t = full_[size_t(index)];
  if (t) gl::DeleteTextures(1, &t);
  t = createTexture(mips);
}

GLuint GpuTextures::texture(const TexRef& r) const {
  if (r.level < 0) return placeholder_;
  if (r.level == kFullLevel) return full_[r.page] ? full_[r.page] : placeholder_;
  const GLuint t = levels_[size_t(r.level)]->texture(r.page);
  return t ? t : placeholder_;
}

}  // namespace pb
