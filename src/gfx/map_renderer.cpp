#include "gfx/map_renderer.h"

#include <algorithm>
#include <cstddef>

#include "gfx/shader.h"
#include "shaders_embedded.h"

namespace pb {

MapRenderer::~MapRenderer() {
  if (vbo_) gl::DeleteBuffers(1, &vbo_);
  if (vao_) gl::DeleteVertexArrays(1, &vao_);
  if (program_) gl::DeleteProgram(program_);
}

bool MapRenderer::init() {
  program_ = buildProgram(kShader_map_dot_vert, kShader_map_dot_frag);
  if (!program_) return false;
  uViewport_ = gl::GetUniformLocation(program_, "uViewport");
  gl::GenVertexArrays(1, &vao_);
  gl::GenBuffers(1, &vbo_);
  gl::BindVertexArray(vao_);
  gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
  const GLsizei stride = sizeof(MapDot);
  auto attr = [&](GLuint loc, GLint n, size_t offset) {
    gl::EnableVertexAttribArray(loc);
    gl::VertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offset));
    gl::VertexAttribDivisor(loc, 1);
  };
  attr(0, 4, offsetof(MapDot, center));  // center, radius, ring
  attr(1, 4, offsetof(MapDot, color));
  attr(2, 1, offsetof(MapDot, halo));
  return true;
}

void MapRenderer::draw(Vec2d viewportPx, std::span<const MapDot> dots) {
  if (dots.empty()) return;
  gl::UseProgram(program_);
  gl::Uniform2f(uViewport_, float(viewportPx.x), float(viewportPx.y));
  gl::BindVertexArray(vao_);
  gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
  capacity_ = std::max(capacity_, dots.size_bytes());
  gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(capacity_), nullptr, GL_STREAM_DRAW);
  gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(dots.size_bytes()), dots.data());
  gl::Enable(GL_BLEND);
  gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(dots.size()));
  gl::Disable(GL_BLEND);
}

}  // namespace pb
