#include "map/map_camera.h"

#include <algorithm>
#include <cmath>

#include "map/mercator.h"

namespace pb {

namespace {
constexpr double kClickSlopPx = 4;
constexpr double kDoubleClickS = 0.35;
constexpr double kWheelZoom = 0.5;  // zoom levels per wheel notch
}  // namespace

double MapCamera::worldPx() const { return kTilePx * std::exp2(zoom) * uiScale; }

Vec2d MapCamera::worldToScreen(Vec2d w) const { return (w - center) * worldPx() + viewport * 0.5; }

Vec2d MapCamera::screenToWorld(Vec2d s) const { return (s - viewport * 0.5) / worldPx() + center; }

Rect MapCamera::visibleWorld() const {
  const Vec2d a = screenToWorld({0, 0}), b = screenToWorld(viewport);
  return {a.x, a.y, b.x, b.y};
}

void MapCamera::normalize() {
  center.x -= std::floor(center.x);
  center.y = std::clamp(center.y, 0.0, 1.0);
}

void MapCamera::applyZoom(Vec2d pivot, double newZoom) {
  const Vec2d w = screenToWorld(pivot);
  zoom = std::clamp(newZoom, minZoom, maxZoom);
  center = w - (pivot - viewport * 0.5) / worldPx();
  normalize();
}

void MapCamera::zoomAbout(Vec2d screenPivot, double newZoom) {
  applyZoom(screenPivot, newZoom);
  zoomSmooth_.current = zoomSmooth_.target = zoom;
}

void MapCamera::stopMotion() {
  flight_.reset();
  momentum_.stop();
  zoomSmooth_.current = zoomSmooth_.target = zoom;
}

void MapCamera::jumpTo(Vec2d worldCenter, double newZoom) {
  stopMotion();
  center = worldCenter;
  zoom = std::clamp(newZoom, minZoom, maxZoom);
  zoomSmooth_.current = zoomSmooth_.target = zoom;
  normalize();
}

void MapCamera::onPointerDown(Vec2d px, double t) {
  pressed_ = true;
  dragging_ = false;
  pressPos_ = lastPos_ = px;
  stopMotion();
  tracker_.reset();
  tracker_.add(t, px);
}

void MapCamera::onPointerMove(Vec2d px, double t) {
  if (!pressed_) return;
  if (!dragging_ && length(px - pressPos_) >= kClickSlopPx) dragging_ = true;
  if (dragging_) {
    center = center - (px - lastPos_) / worldPx();
    normalize();
    lastPos_ = px;
    userMoved_ = true;
  }
  tracker_.add(t, px);
}

void MapCamera::onPointerUp(Vec2d px, double t) {
  if (!pressed_) return;
  pressed_ = false;
  if (!dragging_ && length(px - pressPos_) >= kClickSlopPx) {
    dragging_ = true;
    center = center - (px - lastPos_) / worldPx();
    normalize();
    userMoved_ = true;
  }
  if (dragging_) {
    tracker_.add(t, px);
    momentum_.start(tracker_.velocity(t));
    lastClickTime_ = -1e9;
    return;
  }
  if (t - lastClickTime_ <= kDoubleClickS && length(px - lastClickPos_) < kClickSlopPx) {
    lastClickTime_ = -1e9;
    zoomPivot_ = px;
    zoomSmooth_.target = std::clamp(zoomSmooth_.target + 1, minZoom, maxZoom);
    userMoved_ = true;
    return;
  }
  lastClickTime_ = t;
  lastClickPos_ = px;
}

void MapCamera::onWheel(Vec2d px, double notches) {
  if (flight_) stopMotion();
  momentum_.stop();
  zoomPivot_ = px;
  zoomSmooth_.target = std::clamp(zoomSmooth_.target + notches * kWheelZoom, minZoom, maxZoom);
  userMoved_ = true;
}

double MapCamera::widthAt(double z) const { return viewport.x / (kTilePx * std::exp2(z) * uiScale); }

void MapCamera::flyTo(Vec2d worldCenter, double newZoom, double t) {
  stopMotion();
  newZoom = std::clamp(newZoom, minZoom, maxZoom);
  if (viewport.x < 1) {  // nothing to see (e.g. a minimised window): no path to fly
    jumpTo(worldCenter, newZoom);
    return;
  }
  Vec2d target = worldCenter;
  target.x -= std::floor(target.x);
  if (target.x - center.x > 0.5) target.x -= 1;  // the short way around the world
  if (center.x - target.x > 0.5) target.x += 1;
  FlyPath path(center, widthAt(zoom), target, widthAt(newZoom));
  if (path.length() < 1e-9) {
    jumpTo(worldCenter, newZoom);
    return;
  }
  flight_ = Flight{path, target, newZoom, t, std::clamp(path.length(), kFlyMinS, kFlyMaxS)};
}

std::optional<MapCamera::View> MapCamera::fitView(std::span<const Vec2d> points, Vec2d viewport, double uiScale,
                                                  double padding, double maxZoom, double singleZoom, double minZoom) {
  if (points.empty()) return std::nullopt;
  Rect box{points[0].x, points[0].y, points[0].x, points[0].y};
  for (const Vec2d& p : points) {
    box = {std::min(box.x0, p.x), std::min(box.y0, p.y), std::max(box.x1, p.x), std::max(box.y1, p.y)};
  }
  if (box.width() < 1e-12 && box.height() < 1e-12) return View{box.center(), singleZoom};
  if (viewport.x < 1 || viewport.y < 1) return View{box.center(), minZoom};
  const double fill = 1 - 2 * padding;
  double scale = 1e300;  // world px per world unit that fits the box
  if (box.width() > 0) scale = std::min(scale, viewport.x * fill / box.width());
  if (box.height() > 0) scale = std::min(scale, viewport.y * fill / box.height());
  const double zoom = std::log2(scale / (kTilePx * uiScale));
  return View{box.center(), std::clamp(zoom, minZoom, maxZoom)};
}

void MapCamera::update(double t, double dt) {
  if (flight_) {
    const double u = std::clamp((t - flight_->t0) / flight_->duration, 0.0, 1.0);
    if (u >= 1) {
      center = flight_->target;
      zoom = flight_->targetZoom;
      flight_.reset();
      zoomSmooth_.current = zoomSmooth_.target = zoom;
    } else {
      double w = 0;
      flight_->path.at(flight_->path.length() * easeInOutCubic(u), center, w);
      if (viewport.x >= 1 && w > 0) zoom = std::log2(viewport.x / (kTilePx * uiScale * w));
    }
    normalize();
    return;
  }
  if (!zoomSmooth_.settled()) {
    zoomSmooth_.step(dt);
    applyZoom(zoomPivot_, zoomSmooth_.current);
  }
  if (momentum_.active && !pressed_) {
    center = center - momentum_.step(dt) / worldPx();
    normalize();
  }
}

bool MapCamera::idle() const { return !pressed_ && !momentum_.active && zoomSmooth_.settled() && !flight_; }

}  // namespace pb
