#pragma once

#include <array>
#include <optional>
#include <string>


#include "core/geom.h"
#include "gfx/gl.h"

namespace pb {

// Fills the frame behind the photos: the clear colour alone, or a fullscreen
// fragment shader (a built-in preset or the user's) given uResolution, uTime,
// uColor and uColor2.
class BackgroundRenderer {
 public:
  ~BackgroundRenderer();
  // Empty `shaderSource` = solid colour. Returns false (falling back to solid) if
  // the shader fails to compile; the compiler log goes to stderr.
  bool init(std::array<float, 3> color, std::array<float, 3> color2, const std::string& shaderSource, bool animated);
  void draw(Vec2d viewportPx, double timeSeconds);
  // GPU time of the most recent completed background pass (ms), when measured.
  void enableTiming() { timing_ = true; }
  std::optional<double> takeGpuMs();
  bool animated() const { return animated_; }  // needs continuous redraw
  const std::array<float, 3>& clearColor() const { return color_; }

 private:
  bool animated_ = false;
  std::array<float, 3> color_{}, color2_{};
  GLuint program_ = 0, vao_ = 0;
  GLint uResolution_ = -1, uTime_ = -1, uColor_ = -1, uColor2_ = -1;
  bool timing_ = false;
  GLuint queries_[3] = {};  // ring: results are read a couple of frames later
  int queryNext_ = 0, queriesIssued_ = 0;
};

}  // namespace pb
