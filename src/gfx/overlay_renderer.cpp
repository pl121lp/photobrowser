#include "gfx/overlay_renderer.h"

#include "gfx/clock_font_data.h"
#include "gfx/font_data.h"
#include "gfx/shader.h"
#include "shaders_embedded.h"

namespace pb {

namespace {
constexpr int kFloatsPerQuad = 12;

GLuint atlasTexture(uint32_t w, uint32_t h, const uint8_t* texels) {
  GLuint tex = 0;
  gl::GenTextures(1, &tex);
  gl::BindTexture(GL_TEXTURE_2D, tex);
  gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl::TexImage2D(GL_TEXTURE_2D, 0, GL_R8, GLsizei(w), GLsizei(h), 0, GL_RED, GL_UNSIGNED_BYTE, texels);
  gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
}
}  // namespace

FontInfo OverlayRenderer::font() {
  return {float(font::kCellW), float(font::kCellH), float(font::kAtlasW), float(font::kAtlasH), int(font::kCols),
          font::kFirstChar,    font::kLastChar,     font::kSolidIndex,   font::kExtraChars,
          font::kExtraCount,   font::kExtraFirstIndex};
}

FontInfo OverlayRenderer::clockFont() {
  namespace cf = clock_font;
  return {float(cf::kCellW), float(cf::kCellH), float(cf::kAtlasW), float(cf::kAtlasH), int(cf::kCols),
          cf::kFirstChar,    cf::kLastChar,     cf::kSolidIndex};
}

OverlayRenderer::~OverlayRenderer() {
  if (fontTex_) gl::DeleteTextures(1, &fontTex_);
  if (clockTex_) gl::DeleteTextures(1, &clockTex_);
  if (vbo_) gl::DeleteBuffers(1, &vbo_);
  if (vao_) gl::DeleteVertexArrays(1, &vao_);
  if (program_) gl::DeleteProgram(program_);
}

bool OverlayRenderer::init() {
  program_ = buildProgram(kShader_overlay_vert, kShader_overlay_frag);
  if (!program_) return false;
  uViewport_ = gl::GetUniformLocation(program_, "uViewport");
  gl::UseProgram(program_);
  gl::Uniform1i(gl::GetUniformLocation(program_, "uFont"), 0);

  fontTex_ = atlasTexture(font::kAtlasW, font::kAtlasH, font::kAtlas);
  clockTex_ = atlasTexture(clock_font::kAtlasW, clock_font::kAtlasH, clock_font::kAtlas);

  gl::GenVertexArrays(1, &vao_);
  gl::GenBuffers(1, &vbo_);
  gl::BindVertexArray(vao_);
  gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
  const GLsizei stride = kFloatsPerQuad * sizeof(float);
  for (GLuint loc = 0; loc < 3; ++loc) {
    gl::EnableVertexAttribArray(loc);
    gl::VertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(loc * 4 * sizeof(float)));
    gl::VertexAttribDivisor(loc, 1);
  }
  return true;
}

void OverlayRenderer::draw(Vec2d viewportPx, const std::vector<OverlayQuad>& quads, Atlas atlas) {
  if (quads.empty()) return;
  scratch_.clear();
  for (const OverlayQuad& q : quads) {
    scratch_.insert(scratch_.end(), {float(q.rect.x0), float(q.rect.y0), float(q.rect.x1), float(q.rect.y1), q.uv.u0,
                                     q.uv.v0, q.uv.u1, q.uv.v1, q.color[0], q.color[1], q.color[2], q.color[3]});
  }
  gl::UseProgram(program_);
  gl::Uniform2f(uViewport_, float(viewportPx.x), float(viewportPx.y));
  gl::BindVertexArray(vao_);
  gl::ActiveTexture(GL_TEXTURE0);
  gl::BindTexture(GL_TEXTURE_2D, atlas == Atlas::Clock ? clockTex_ : fontTex_);
  gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
  gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(scratch_.size() * sizeof(float)), scratch_.data(), GL_STREAM_DRAW);
  gl::Enable(GL_BLEND);
  gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(quads.size()));
  gl::Disable(GL_BLEND);
}

}  // namespace pb
