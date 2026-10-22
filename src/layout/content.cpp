#include "layout/content.h"

#include <algorithm>

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

FocusMorph focusMorph(const Rect& cell, double imageAspect, Vec2d viewport, double progress) {
  const double margin = 0.02 * std::min(viewport.x, viewport.y);
  const Rect target = fitRect(Rect{0, 0, viewport.x, viewport.y}.inset(margin), imageAspect);
  const auto mix = [&](double a, double b) { return a + (b - a) * progress; };
  const Rect r{mix(cell.x0, target.x0), mix(cell.y0, target.y0), mix(cell.x1, target.x1), mix(cell.y1, target.y1)};
  return {r, cropUv(imageAspect, r.width() / r.height())};
}

}  // namespace pb
