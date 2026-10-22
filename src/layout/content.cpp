#include "layout/content.h"

namespace pb {

Rect fitRect(const Rect& box, double aspect) {
  double w = box.width(), h = box.height();
  if (w / h > aspect) {
    w = h * aspect;
  } else {
    h = w / aspect;
  }
  const Vec2d c = box.center();
  return {c.x - w / 2, c.y - h / 2, c.x + w / 2, c.y + h / 2};
}

UvRect cropUv(double imageAspect, double targetAspect) {
  if (imageAspect > targetAspect) {
    const float keep = float(targetAspect / imageAspect);
    return {(1 - keep) / 2, 0, (1 + keep) / 2, 1};
  }
  const float keep = float(imageAspect / targetAspect);
  return {0, (1 - keep) / 2, 1, (1 + keep) / 2};
}

}  // namespace pb
