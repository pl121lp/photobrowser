#include "gfx/tile_renderer.h"

#include <algorithm>
#include <cstddef>

#include "gfx/shader.h"
#include "shaders_embedded.h"

namespace pb {

TileRenderer::~TileRenderer() {
  if (vbo_) gl::DeleteBuffers(1, &vbo_);
  if (vao_) gl::DeleteVertexArrays(1, &vao_);
  if (program_) gl::DeleteProgram(program_);
}

bool TileRenderer::init() {
  program_ = buildProgram(kShader_tile_vert, kShader_tile_frag);
  if (!program_) return false;
  uViewport_ = gl::GetUniformLocation(program_, "uViewport");
  gl::UseProgram(program_);
  gl::Uniform1i(gl::GetUniformLocation(program_, "uTex"), 0);
  gl::Uniform1i(gl::GetUniformLocation(program_, "uTexPrev"), 1);

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
  attr(3, 2, offsetof(TileInstance, fade));
  return true;
}

void TileRenderer::beginFrame(Vec2d viewportPx, const float clearRgb[3]) {
  gl::Viewport(0, 0, GLsizei(viewportPx.x), GLsizei(viewportPx.y));
  gl::ClearColor(clearRgb[0], clearRgb[1], clearRgb[2], 1);
  gl::Clear(GL_COLOR_BUFFER_BIT);
  gl::UseProgram(program_);
  gl::Uniform2f(uViewport_, float(viewportPx.x), float(viewportPx.y));
  gl::BindVertexArray(vao_);
}

void TileRenderer::draw(GLuint tex, GLuint texPrev, std::span<const TileInstance> tiles) {
  if (tiles.empty()) return;
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
  gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(tiles.size()));
}

}  // namespace pb
