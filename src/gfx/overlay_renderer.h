#pragma once

#include <vector>

#include "gfx/gl.h"
#include "view/overlay.h"

namespace pb {

// Draws overlay quads (text, panels, graph bars) alpha-blended over the frame,
// using the embedded bitmap font (src/gfx/font_data.h), or the large clock digits
// (src/gfx/clock_font_data.h) for quads laid out with clockFont().
class OverlayRenderer {
 public:
  enum class Atlas { Ui, Clock };
  ~OverlayRenderer();
  bool init();
  void draw(Vec2d viewportPx, const std::vector<OverlayQuad>& quads, Atlas atlas = Atlas::Ui);
  static FontInfo font();
  static FontInfo clockFont();

 private:
  GLuint program_ = 0, vao_ = 0, vbo_ = 0, fontTex_ = 0, clockTex_ = 0;
  GLint uViewport_ = -1;
  std::vector<float> scratch_;
};

}  // namespace pb
