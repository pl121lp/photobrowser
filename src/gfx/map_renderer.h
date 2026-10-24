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

struct MapLine {
  float ends[4];    // a.x, a.y, b.x, b.y in window px
  float colorA[4];  // linear RGBA at a, blended to colorB at b
  float colorB[4];
  float width;      // px
};

// Draws the map's location dots (signed-distance circles) and trip path segments
// (signed-distance capsules), each in one instanced draw.
class MapRenderer {
 public:
  ~MapRenderer();
  bool init();
  void draw(Vec2d viewportPx, std::span<const MapDot> dots);
  void drawLines(Vec2d viewportPx, std::span<const MapLine> lines);

 private:
  struct Batch {
    GLuint program = 0, vao = 0, vbo = 0;
    GLint uViewport = -1;
    size_t capacity = 0;
  };
  static void release(Batch& b);
  static void draw(Batch& b, Vec2d viewportPx, const void* data, size_t bytes, size_t count);
  Batch dots_, lines_;
};

}  // namespace pb
