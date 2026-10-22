#include "gfx/background_renderer.h"

#include "gfx/shader.h"
#include "shaders_embedded.h"

namespace pb {

BackgroundRenderer::~BackgroundRenderer() {
  if (vao_) gl::DeleteVertexArrays(1, &vao_);
  if (program_) gl::DeleteProgram(program_);
}

bool BackgroundRenderer::init(std::array<float, 3> color, std::array<float, 3> color2,
                              const std::string& shaderSource, bool animated) {
  color_ = color;
  color2_ = color2;
  if (shaderSource.empty()) return true;
  program_ = buildProgram(kShader_fullscreen_vert, shaderSource.c_str());
  if (!program_) return false;
  animated_ = animated;
  uResolution_ = gl::GetUniformLocation(program_, "uResolution");
  uTime_ = gl::GetUniformLocation(program_, "uTime");
  uColor_ = gl::GetUniformLocation(program_, "uColor");
  uColor2_ = gl::GetUniformLocation(program_, "uColor2");
  gl::GenVertexArrays(1, &vao_);
  return true;
}

void BackgroundRenderer::draw(Vec2d viewportPx, double timeSeconds) {
  if (!program_) return;  // solid: the clear colour is the background
  gl::UseProgram(program_);
  gl::Uniform2f(uResolution_, float(viewportPx.x), float(viewportPx.y));
  gl::Uniform1f(uTime_, float(timeSeconds));
  gl::Uniform3f(uColor_, color_[0], color_[1], color_[2]);
  gl::Uniform3f(uColor2_, color2_[0], color2_[1], color2_[2]);
  gl::BindVertexArray(vao_);
  gl::DrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace pb
