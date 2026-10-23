#include "gfx/tile_renderer.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "gfx/shader.h"
#include "photo_shaders.h"
#include "shaders_embedded.h"

namespace pb {

namespace {

// Effect sources by number (TileRenderer::kPhotoEffects of them, from 1).
// Same order as the labels in src/view/overlay.cpp.
constexpr const char* kPhotoEffectSources[] = {
    kShader_sharpen_frag, kShader_sobel_frag, kShader_saturate_frag, kShader_bw_frag,
    kShader_posterize_frag, kShader_sketch_frag, kShader_oilpaint_frag, kShader_tiltshift_frag, kShader_fsr_frag,
};
static_assert(std::size(kPhotoEffectSources) == TileRenderer::kPhotoEffects);

// tile.frag with the effect spliced in at its `//@photo_effect` marker.
std::string tileFragment(const char* effectSource) {
  std::string src = kShader_tile_frag;
  if (!effectSource) return src;
  const std::string marker = "//@photo_effect";
  src.replace(src.find(marker), marker.size(), std::string("#define PHOTO_EFFECT\n") + effectSource);
  return src;
}

}  // namespace

TileRenderer::~TileRenderer() {
  if (vbo_) gl::DeleteBuffers(1, &vbo_);
  if (vao_) gl::DeleteVertexArrays(1, &vao_);
  for (const Program& p : programs_) {
    if (p.id) gl::DeleteProgram(p.id);
  }
}

bool TileRenderer::buildTileProgram(Program& p, const char* effectSource) {
  p.id = buildProgram(kShader_tile_vert, tileFragment(effectSource).c_str());
  if (!p.id) return false;
  p.uViewport = gl::GetUniformLocation(p.id, "uViewport");
  p.uCornerRadius = gl::GetUniformLocation(p.id, "uCornerRadius");
  p.uTransparentBars = gl::GetUniformLocation(p.id, "uTransparentBars");
  gl::UseProgram(p.id);
  gl::Uniform1i(gl::GetUniformLocation(p.id, "uTex"), 0);
  gl::Uniform1i(gl::GetUniformLocation(p.id, "uTexPrev"), 1);
  return true;
}

bool TileRenderer::init() {
  // All built up front: compiling on first use would hitch the frame the key is pressed.
  if (!buildTileProgram(programs_[0], nullptr)) return false;
  for (int i = 1; i <= kPhotoEffects; ++i) {
    if (!buildTileProgram(programs_[i], kPhotoEffectSources[i - 1])) return false;
  }

  gl::GenVertexArrays(1, &vao_);
  gl::GenBuffers(1, &vbo_);
  gl::BindVertexArray(vao_);
  gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
  const GLsizei stride = sizeof(TileInstance);
  auto attr = [&](GLuint loc, GLint n, size_t offset) {
    gl::EnableVertexAttribArray(loc);
    gl::VertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offset));
    gl::VertexAttribDivisor(loc, 1);
  };
  attr(0, 4, offsetof(TileInstance, rect));
  attr(1, 4, offsetof(TileInstance, uv));
  attr(2, 4, offsetof(TileInstance, uvPrev));
  attr(3, 3, offsetof(TileInstance, fade));
  attr(4, 4, offsetof(TileInstance, content));
  return true;
}

void TileRenderer::beginFrame(Vec2d viewportPx, const float clearRgb[3]) {
  gl::Viewport(0, 0, GLsizei(viewportPx.x), GLsizei(viewportPx.y));
  gl::ClearColor(clearRgb[0], clearRgb[1], clearRgb[2], 1);
  gl::Clear(GL_COLOR_BUFFER_BIT);
  viewport_ = viewportPx;
}

void TileRenderer::draw(GLuint tex, GLuint texPrev, std::span<const TileInstance> tiles, int effect) {
  if (tiles.empty()) return;
  // Bound per draw: other passes (background, overlay) use their own programs.
  const Program& p = programs_[effect >= 1 && effect <= kPhotoEffects ? effect : 0];
  gl::UseProgram(p.id);
  gl::Uniform2f(p.uViewport, float(viewport_.x), float(viewport_.y));
  gl::Uniform1f(p.uCornerRadius, cornerRadius_);
  gl::Uniform1f(p.uTransparentBars, transparentBars_ ? 1.0f : 0.0f);
  gl::BindVertexArray(vao_);
  gl::ActiveTexture(GL_TEXTURE0);
  gl::BindTexture(GL_TEXTURE_2D, tex);
  gl::ActiveTexture(GL_TEXTURE1);
  gl::BindTexture(GL_TEXTURE_2D, texPrev ? texPrev : tex);
  gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
  const size_t bytes = tiles.size_bytes();
  capacity_ = std::max(capacity_, bytes);
  // Orphan the buffer so the driver never waits on the previous draw's data.
  gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(capacity_), nullptr, GL_STREAM_DRAW);
  gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), tiles.data());
  // Blending only when needed (rounded corners, transparent bars, fading tiles); otherwise opaque tiles
  // skip the framebuffer read.
  const bool blend = cornerRadius_ > 0 || transparentBars_ ||
                     std::any_of(tiles.begin(), tiles.end(), [](const TileInstance& t) { return t.alpha < 1; });
  if (blend) {
    gl::Enable(GL_BLEND);
    gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(tiles.size()));
  if (blend) gl::Disable(GL_BLEND);
}

}  // namespace pb
