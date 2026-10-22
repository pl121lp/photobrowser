#include "app/gpu_textures.h"

#include <algorithm>
#include <bit>
#include <chrono>

namespace pb {

GpuTextures::GpuTextures() {
  for (int l = 0; l < kLevelCount; ++l) levels_[size_t(l)] = std::make_unique<PagedTexture>(kSlotSize[l], ring_);
  placeholder_ = createImageTexture(Image{1, 1, {38, 38, 40, 255}}, false);
}

GpuTextures::~GpuTextures() {
  for (int l = 0; l < kLevelCount; ++l) {
    for (uint16_t p = 0; p < 1024; ++p) {
      if (GLuint t = levels_[size_t(l)]->releasePage(p)) pool_.push_back(t);
    }
  }
  if (!pool_.empty()) gl::DeleteTextures(GLsizei(pool_.size()), pool_.data());
  for (auto& f : full_) {
    if (f.tex) gl::DeleteTextures(1, &f.tex);
  }
  gl::DeleteTextures(1, &placeholder_);
}

bool GpuTextures::prewarm(size_t pages, Vec2d viewport) {
  fullCapacity_ = {std::max(fullCapacity_.x, viewport.x), std::max(fullCapacity_.y, viewport.y)};
  for (int i = 0; i < Streamer::kFullSlots; ++i) {
    if (ensureFull(i, uint32_t(viewport.x), uint32_t(viewport.y))) return true;
  }
  if (pagesCreated_ < pages) {
    pool_.push_back(createPageTexture());
    ++pagesCreated_;
    return true;
  }
  return false;
}

void GpuTextures::addPage(int level, uint16_t page) {
  GLuint t;
  if (!pool_.empty()) {
    t = pool_.back();
    pool_.pop_back();
  } else {
    t = createPageTexture();  // not prewarmed yet: pay the allocation now
    ++pagesCreated_;
  }
  levels_[size_t(level)]->addPage(page, t);
}

void GpuTextures::releasePage(int level, uint16_t page) {
  if (GLuint t = levels_[size_t(level)]->releasePage(page)) pool_.push_back(t);
}

void GpuTextures::upload(int level, uint16_t page, Vec2d origin, const std::vector<Image>& mips) {
  const auto t0 = std::chrono::steady_clock::now();
  levels_[size_t(level)]->upload(page, origin, mips);
  uploadMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool GpuTextures::ensureFull(int index, uint32_t w, uint32_t h) {
  FullTexture& f = full_[size_t(index)];
  if (f.tex && f.w >= w && f.h >= h) return false;
  if (f.tex) gl::DeleteTextures(1, &f.tex);
  f.w = std::max(w, f.w);
  f.h = std::max(h, f.h);
  f.tex = createEmptyTexture(f.w, f.h);
  f.maxLevel = std::bit_width(std::max(f.w, f.h)) - 1;
  return true;
}

UvRect GpuTextures::beginFull(int index, uint32_t w, uint32_t h) {
  ensureFull(index, w, h);
  const FullTexture& f = full_[size_t(index)];
  return {0, 0, float(w) / float(f.w), float(h) / float(f.h)};
}

void GpuTextures::uploadFullRows(int index, int mip, uint32_t y0, uint32_t rows, const Image& m) {
  const FullTexture& f = full_[size_t(index)];
  // Texture levels halve rounding down, image mips round up: clip the last texel.
  if (mip > f.maxLevel) return;
  const uint32_t lw = std::max(1u, f.w >> mip), lh = std::max(1u, f.h >> mip);
  if (y0 >= lh) return;
  rows = std::min(rows, lh - y0);
  const auto t0 = std::chrono::steady_clock::now();
  ring_.upload(f.tex, mip, 0, y0, std::min(m.w, lw), rows, m.w, m.rgba.data() + size_t(y0) * m.w * 4);
  uploadMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

GLuint GpuTextures::texture(const TexRef& r) const {
  if (r.level < 0) return placeholder_;
  if (r.level == kFullLevel) return full_[r.page].tex ? full_[r.page].tex : placeholder_;
  const GLuint t = levels_[size_t(r.level)]->texture(r.page);
  return t ? t : placeholder_;
}

}  // namespace pb
