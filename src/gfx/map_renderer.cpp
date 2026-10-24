#include "gfx/map_renderer.h"

#include <algorithm>
#include <cstddef>

#include "gfx/shader.h"
#include "shaders_embedded.h"

namespace pb {

void MapRenderer::release(Batch& b) {
  if (b.vbo) gl::DeleteBuffers(1, &b.vbo);
  if (b.vao) gl::DeleteVertexArrays(1, &b.vao);
  if (b.program) gl::DeleteProgram(b.program);
}

MapRenderer::~MapRenderer() {
  release(dots_);
  release(lines_);
}

bool MapRenderer::init() {
  auto setup = [](Batch& b, const char* vert, const char* frag) {
    b.program = buildProgram(vert, frag);
    if (!b.program) return false;
    b.uViewport = gl::GetUniformLocation(b.program, "uViewport");
    gl::GenVertexArrays(1, &b.vao);
    gl::GenBuffers(1, &b.vbo);
    gl::BindVertexArray(b.vao);
    gl::BindBuffer(GL_ARRAY_BUFFER, b.vbo);
    return true;
  };
  auto attr = [](GLuint loc, GLint n, GLsizei stride, size_t offset) {
    gl::EnableVertexAttribArray(loc);
    gl::VertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offset));
    gl::VertexAttribDivisor(loc, 1);
  };
  if (!setup(dots_, kShader_map_dot_vert, kShader_map_dot_frag)) return false;
  attr(0, 4, sizeof(MapDot), offsetof(MapDot, center));  // center, radius, ring
  attr(1, 4, sizeof(MapDot), offsetof(MapDot, color));
  attr(2, 1, sizeof(MapDot), offsetof(MapDot, halo));
  if (!setup(lines_, kShader_map_line_vert, kShader_map_line_frag)) return false;
  attr(0, 4, sizeof(MapLine), offsetof(MapLine, ends));
  attr(1, 4, sizeof(MapLine), offsetof(MapLine, colorA));
  attr(2, 4, sizeof(MapLine), offsetof(MapLine, colorB));
  attr(3, 1, sizeof(MapLine), offsetof(MapLine, width));
  return true;
}

void MapRenderer::draw(Batch& b, Vec2d viewportPx, const void* data, size_t bytes, size_t count) {
  if (!count) return;
  gl::UseProgram(b.program);
  gl::Uniform2f(b.uViewport, float(viewportPx.x), float(viewportPx.y));
  gl::BindVertexArray(b.vao);
  gl::BindBuffer(GL_ARRAY_BUFFER, b.vbo);
  b.capacity = std::max(b.capacity, bytes);
  gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(b.capacity), nullptr, GL_STREAM_DRAW);
  gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), data);
  gl::Enable(GL_BLEND);
  gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(count));
  gl::Disable(GL_BLEND);
}

void MapRenderer::draw(Vec2d viewportPx, std::span<const MapDot> dots) {
  draw(dots_, viewportPx, dots.data(), dots.size_bytes(), dots.size());
}

void MapRenderer::drawLines(Vec2d viewportPx, std::span<const MapLine> lines) {
  draw(lines_, viewportPx, lines.data(), lines.size_bytes(), lines.size());
}

}  // namespace pb
