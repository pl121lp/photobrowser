#include "gfx/paged_texture.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>

#include "imaging/resize.h"

namespace pb {

namespace {
void setSampling(int maxLevel) {
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, maxLevel > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
}  // namespace

UploadRing::~UploadRing() {
  for (int i = 0; i < kBuffers; ++i) {
    if (fences_[i]) gl::DeleteSync(fences_[i]);
  }
  if (buffers_[0]) gl::DeleteBuffers(kBuffers, buffers_);
}

bool UploadRing::begin() {
  if (mapped_) return true;
  if (!buffers_[0]) {
    gl::GenBuffers(kBuffers, buffers_);
    for (GLuint b : buffers_) {
      gl::BindBuffer(GL_PIXEL_UNPACK_BUFFER, b);
      gl::BufferData(GL_PIXEL_UNPACK_BUFFER, GLsizeiptr(kBufferBytes), nullptr, GL_STREAM_DRAW);
    }
  }
  if (GLsync& f = fences_[current_]) {
    // Normally long signalled (two frames ago); bounded wait otherwise.
    gl::ClientWaitSync(f, GL_SYNC_FLUSH_COMMANDS_BIT, 50'000'000);
    gl::DeleteSync(f);
    f = nullptr;
  }
  gl::BindBuffer(GL_PIXEL_UNPACK_BUFFER, buffers_[current_]);
  mapped_ = static_cast<uint8_t*>(gl::MapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, GLsizeiptr(kBufferBytes),
                                                     GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT |
                                                         GL_MAP_INVALIDATE_BUFFER_BIT));
  gl::BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  used_ = 0;
  return mapped_ != nullptr;
}

void UploadRing::upload(GLuint texture, int level, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t rowLength, const uint8_t* rgba) {
  const size_t rowBytes = size_t(w) * 4;
  const size_t bytes = rowBytes * h;
  gl::BindTexture(GL_TEXTURE_2D, texture);
  gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
  if (!begin() || used_ + bytes > kBufferBytes) {
    gl::PixelStorei(GL_UNPACK_ROW_LENGTH, GLint(rowLength));
    gl::TexSubImage2D(GL_TEXTURE_2D, level, GLint(x), GLint(y), GLsizei(w), GLsizei(h), GL_RGBA, GL_UNSIGNED_BYTE,
                      rgba);
    gl::PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return;
  }
  // Pack rows tightly into the staging buffer, then upload from it after unmapping.
  uint8_t* dst = mapped_ + used_;
  for (uint32_t r = 0; r < h; ++r) std::memcpy(dst + r * rowBytes, rgba + size_t(r) * rowLength * 4, rowBytes);
  pending_.push_back({texture, level, x, y, w, h, used_});
  used_ += (bytes + 255) & ~size_t(255);
}

void UploadRing::endFrame() {
  if (!mapped_) return;
  gl::BindBuffer(GL_PIXEL_UNPACK_BUFFER, buffers_[current_]);
  gl::UnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  mapped_ = nullptr;
  gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
  for (const Pending& p : pending_) {
    gl::BindTexture(GL_TEXTURE_2D, p.texture);
    gl::TexSubImage2D(GL_TEXTURE_2D, p.level, GLint(p.x), GLint(p.y), GLsizei(p.w), GLsizei(p.h), GL_RGBA,
                      GL_UNSIGNED_BYTE, reinterpret_cast<const void*>(p.offset));
  }
  pending_.clear();
  gl::BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  fences_[current_] = gl::FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  current_ = (current_ + 1) % kBuffers;
}

GLuint createEmptyTexture(uint32_t w, uint32_t h) {
  GLuint t = 0;
  gl::GenTextures(1, &t);
  gl::BindTexture(GL_TEXTURE_2D, t);
  gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
  const uint8_t texel[4] = {0, 0, 0, 255};
  int levels = 0;
  for (uint32_t lw = w, lh = h;; lw = std::max(1u, lw / 2), lh = std::max(1u, lh / 2), ++levels) {
    gl::TexImage2D(GL_TEXTURE_2D, levels, GL_SRGB8_ALPHA8, GLsizei(lw), GLsizei(lh), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   nullptr);
    gl::TexSubImage2D(GL_TEXTURE_2D, levels, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, texel);
    if (lw == 1 && lh == 1) break;
  }
  setSampling(levels);
  return t;
}

GLuint createPageTexture() { return createEmptyTexture(kPageSize, kPageSize); }

PagedTexture::PagedTexture(uint32_t slotSize, UploadRing& ring)
    : slotSize_(slotSize), maxLevel_(std::countr_zero(slotSize)), ring_(ring) {}

void PagedTexture::addPage(uint16_t page, GLuint texture) {
  if (page >= pages_.size()) pages_.resize(page + 1, 0);
  pages_[page] = texture;
  gl::BindTexture(GL_TEXTURE_2D, texture);
  setSampling(maxLevel_);
}

GLuint PagedTexture::releasePage(uint16_t page) {
  if (page >= pages_.size()) return 0;
  return std::exchange(pages_[page], 0);
}

void PagedTexture::upload(uint16_t page, Vec2d origin, const std::vector<Image>& mips) {
  const GLuint t = texture(page);
  if (!t) return;
  const int levels = std::min<int>(int(mips.size()), maxLevel_ + 1);
  for (int l = 0; l < levels; ++l) {
    const Image& m = mips[size_t(l)];
    ring_.upload(t, l, uint32_t(origin.x) >> l, uint32_t(origin.y) >> l, m.w, m.h, m.w, m.rgba.data());
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
  setSampling(int(mips.size()) - 1);
  return t;
}

GLuint createImageTexture(const Image& img, bool mipmaps) {
  return createTexture(mipmaps ? buildMips(img) : std::vector<Image>{img});
}

}  // namespace pb
