#pragma once

#include <array>
#include <string>

#include "core/geom.h"
#include "gfx/gl.h"

namespace pb {

// Fills the frame behind the photos: a solid clear colour, a built-in gradient or
// vignette, or a user-supplied GLSL fragment shader.
class BackgroundRenderer {
 public:
  enum class Mode { Solid, Gradient, Vignette, Shader };

  ~BackgroundRenderer();
  // `userShader` is the source for Mode::Shader. Returns false (and falls back to
  // solid) if a shader fails to compile; `error` then says why.
  bool init(Mode mode, std::array<float, 3> color, std::array<float, 3> color2, const std::string& userShader,
            std::string& error);
  void draw(Vec2d viewportPx, double timeSeconds);
  bool animated() const { return mode_ == Mode::Shader; }  // needs continuous redraw
  const std::array<float, 3>& clearColor() const { return color_; }

 private:
  Mode mode_ = Mode::Solid;
  std::array<float, 3> color_{}, color2_{};
  GLuint program_ = 0, vao_ = 0;
  GLint uResolution_ = -1, uTime_ = -1, uColor_ = -1, uColor2_ = -1, uMode_ = -1;
};

}  // namespace pb
