#pragma once

#include <span>
#include <vector>

#include "core/geom.h"
#include "gfx/gl.h"

namespace pb {

struct MapDot {
  float center[2];  // window px
  float radius;     // px of the filled disc
  float ring;       // px of the white ring around it
  float color[4];   // linear RGBA of the disc
  float halo = 0;   // 0 = none, else pulse phase 0..1: a ring growing from radius to 3 * radius, fading out
};

// Draws the map's location dots (signed-distance circles) in one instanced draw.
class MapRenderer {
 public:
  ~MapRenderer();
  bool init();
  void draw(Vec2d viewportPx, std::span<const MapDot> dots);

 private:
  GLuint program_ = 0, vao_ = 0, vbo_ = 0;
  GLint uViewport_ = -1;
  size_t capacity_ = 0;
};

}  // namespace pb
