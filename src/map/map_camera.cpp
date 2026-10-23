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
  momentum_.stop();
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
  momentum_.stop();
  zoomPivot_ = px;
  zoomSmooth_.target = std::clamp(zoomSmooth_.target + notches * kWheelZoom, minZoom, maxZoom);
  userMoved_ = true;
}

void MapCamera::update(double /*t*/, double dt) {
  if (!zoomSmooth_.settled()) {
    zoomSmooth_.step(dt);
    applyZoom(zoomPivot_, zoomSmooth_.current);
  }
  if (momentum_.active && !pressed_) {
    center = center - momentum_.step(dt) / worldPx();
    normalize();
  }
}

bool MapCamera::idle() const { return !pressed_ && !momentum_.active && zoomSmooth_.settled(); }

}  // namespace pb
