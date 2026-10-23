#pragma once

#include <array>
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
  float content[4] = {0, 0, 1, 1};  // image area in tile coordinates; black bars outside
};

// Draws textured screen-space quads with one instanced draw per texture pair.
class TileRenderer {
 public:
  // Photo effects (shaders/photo/*.frag), numbered from 1; 0 = none.
  static constexpr int kPhotoEffects = 8;

  ~TileRenderer();
  bool init();
  void beginFrame(Vec2d viewportPx, const float clearRgb[3]);
  // Rounded corners: radius as a fraction of each tile's shorter side (0 = square).
  void setCornerRadius(float fraction) { cornerRadius_ = fraction; }
  // Letterbox bars (outside a tile's image area) transparent instead of black.
  void setTransparentBars(bool on) { transparentBars_ = on; }
  // `effect`: photo effect 1..kPhotoEffects for these tiles, 0 (or out of range) = none.
  void draw(GLuint tex, GLuint texPrev, std::span<const TileInstance> tiles, int effect = 0);

 private:
  struct Program {
    GLuint id = 0;
    GLint uViewport = -1, uCornerRadius = -1, uTransparentBars = -1;
  };
  bool buildTileProgram(Program& p, const char* effectSource);

  std::array<Program, kPhotoEffects + 1> programs_;  // [0] plain, [i] photo effect i
  GLuint vao_ = 0, vbo_ = 0;
  float cornerRadius_ = 0;
  bool transparentBars_ = false;
  size_t capacity_ = 0;
  Vec2d viewport_;
};

}  // namespace pb
