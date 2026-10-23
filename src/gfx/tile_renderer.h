#pragma once

#include <span>

#include "core/geom.h"
#include "gfx/gl.h"

namespace pb {

struct TileInstance {
  float rect[4];    // screen px x0, y0, x1, y1
  float uv[4];      // in the current texture
  float uvPrev[4];  // in the previous (coarser) texture, for cross-fades
  float fade;       // weight of the current texture, 0..1
  float dim;        // 0 = normal, 1 = black
  float alpha = 1;  // opacity; below 1 blends over the background
};

// Draws textured screen-space quads with one instanced draw per texture pair.
class TileRenderer {
 public:
  ~TileRenderer();
  bool init();
  void beginFrame(Vec2d viewportPx, const float clearRgb[3]);
  // Rounded corners: radius as a fraction of each tile's shorter side (0 = square).
  void setCornerRadius(float fraction) { cornerRadius_ = fraction; }
  void draw(GLuint tex, GLuint texPrev, std::span<const TileInstance> tiles);

 private:
  GLuint program_ = 0, vao_ = 0, vbo_ = 0;
  GLint uViewport_ = -1, uCornerRadius_ = -1;
  float cornerRadius_ = 0;
  size_t capacity_ = 0;
  Vec2d viewport_;
};

}  // namespace pb
