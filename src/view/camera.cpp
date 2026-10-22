#include "view/camera.h"

namespace pb {

Rect Camera::visibleWorld() const {
  const Vec2d a = screenToWorld({0, 0});
  const Vec2d b = screenToWorld(viewport);
  return {a.x, a.y, b.x, b.y};
}

void Camera::zoomAbout(Vec2d screenPivot, double newScale) {
  const Vec2d anchor = screenToWorld(screenPivot);
  scale = newScale;
  center = anchor - (screenPivot - viewport * 0.5) / scale;
}

}  // namespace pb
