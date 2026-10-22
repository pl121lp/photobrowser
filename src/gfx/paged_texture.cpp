#include "gfx/paged_texture.h"

#include <bit>

#include "imaging/resize.h"

namespace pb {

PagedTexture::PagedTexture(uint32_t slotSize)
    : slotSize_(slotSize), maxLevel_(std::countr_zero(slotSize)) {}

PagedTexture::~PagedTexture() {
  for (GLuint t : pages_) {
    if (t) gl::DeleteTextures(1, &t);
  }
}

void PagedTexture::addPage(uint16_t page) {
  if (page >= pages_.size()) pages_.resize(page + 1, 0);
  if (pages_[page]) return;
  GLuint t = 0;
  gl::GenTextures(1, &t);
  gl::BindTexture(GL_TEXTURE_2D, t);
  for (int l = 0; l <= maxLevel_; ++l) {
    const GLsizei s = GLsizei(kPageSize >> l);
    gl::TexImage2D(GL_TEXTURE_2D, l, GL_SRGB8_ALPHA8, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  }
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel_);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  pages_[page] = t;
}

void PagedTexture::releasePage(uint16_t page) {
  if (page < pages_.size() && pages_[page]) {
    gl::DeleteTextures(1, &pages_[page]);
    pages_[page] = 0;
  }
}

void PagedTexture::upload(uint16_t page, Vec2d origin, const std::vector<Image>& mips) {
  const GLuint t = texture(page);
  if (!t) return;
  gl::BindTexture(GL_TEXTURE_2D, t);
  gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
  const int levels = std::min<int>(int(mips.size()), maxLevel_ + 1);
  for (int l = 0; l < levels; ++l) {
    const Image& m = mips[size_t(l)];
    gl::TexSubImage2D(GL_TEXTURE_2D, l, GLint(origin.x) >> l, GLint(origin.y) >> l, GLsizei(m.w), GLsizei(m.h),
                      GL_RGBA, GL_UNSIGNED_BYTE, m.rgba.data());
  }
}

GLuint createTexture(const std::vector<Image>& mips) {
  GLuint t = 0;
  gl::GenTextures(1, &t);
  gl::BindTexture(GL_TEXTURE_2D, t);
  gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
  for (size_t l = 0; l < mips.size(); ++l) {
    gl::TexImage2D(GL_TEXTURE_2D, GLint(l), GL_SRGB8_ALPHA8, GLsizei(mips[l].w), GLsizei(mips[l].h), 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, mips[l].rgba.data());
  }
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(mips.size() - 1));
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips.size() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

GLuint createImageTexture(const Image& img, bool mipmaps) {
  return createTexture(mipmaps ? buildMips(img) : std::vector<Image>{img});
}

}  // namespace pb
