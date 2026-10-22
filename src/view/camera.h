#pragma once

#include "core/geom.h"

namespace pb {

// Maps world units to drawable pixels. The viewport center shows `center`.
class Camera {
 public:
  Vec2d center;
  double scale = 100;  // drawable pixels per world unit
  Vec2d viewport{1280, 720};

  Vec2d worldToScreen(Vec2d w) const { return (w - center) * scale + viewport * 0.5; }
  Vec2d screenToWorld(Vec2d s) const { return (s - viewport * 0.5) / scale + center; }
  Rect visibleWorld() const;
  // Changes the scale keeping the world point under `screenPivot` fixed.
  void zoomAbout(Vec2d screenPivot, double newScale);
};

}  // namespace pb
