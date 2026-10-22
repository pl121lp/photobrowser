#include "view/interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pb {

namespace {
constexpr double kClickSlopPx = 4;
constexpr double kDoubleClickS = 0.35;
constexpr double kGlideS = 0.4;
constexpr double kFocusS = 0.35;
constexpr double kWheelFactor = 1.25;
constexpr double kFitMargin = 0.95;
}  // namespace

Interaction::Interaction(Camera& camera, const Layout& layout) : cam_(camera), layout_(layout) {
  computeLimits();
  resetView();
}

void Interaction::computeLimits() {
  const Vec2d vp = cam_.viewport;
  const double shortSide = std::min(vp.x, vp.y);
  maxScale_ = shortSide;
  const Rect b = layout_.bounds();
  if (b.width() > 0 && b.height() > 0) {
    minScale_ = std::min(vp.x / b.width(), vp.y / b.height()) * kFitMargin;
  } else {
    minScale_ = shortSide / 3;
  }
  minScale_ = std::min(minScale_, maxScale_);
}

double Interaction::startScale() const {
  return std::clamp(std::min(cam_.viewport.x, cam_.viewport.y) / 3, minScale_, maxScale_);
}

void Interaction::setViewport(Vec2d px) {
  cam_.viewport = px;  // camera center stays at the screen center
  computeLimits();
  const double lo = std::log(minScale_), hi = std::log(maxScale_);
  logZoom_.target = std::clamp(logZoom_.target, lo, hi);
  if (tween_.active) tween_.logScale.to = std::clamp(tween_.logScale.to, lo, hi);
  if (!tween_.active && logZoom_.settled()) {
    logZoom_.current = logZoom_.target;
    cam_.scale = std::exp(logZoom_.current);
  }
}

void Interaction::layoutChanged() { computeLimits(); }

void Interaction::resetView() {
  stopMotion();
  const Vec2d c = layout_.count() > 0 ? layout_.place(layout_.centerPosition()).cell.center()
                                      : layout_.bounds().center();
  cam_.center = c;
  cam_.scale = startScale();
  logZoom_.current = logZoom_.target = std::log(cam_.scale);
}

void Interaction::stopMotion() {
  momentum_.stop();
  tween_.active = false;
  logZoom_.target = logZoom_.current = std::log(cam_.scale);
}

void Interaction::glideTo(Vec2d center, double scale, double t) {
  stopMotion();
  scale = std::clamp(scale, minScale_, maxScale_);
  tween_.active = true;
  tween_.x = {cam_.center.x, center.x, t, kGlideS};
  tween_.y = {cam_.center.y, center.y, t, kGlideS};
  tween_.logScale = {std::log(cam_.scale), std::log(scale), t, kGlideS};
}

void Interaction::focus(uint32_t pos, double t) {
  focused_ = pos;
  focusTarget_ = true;
  focusTween_ = {focusProgress_, 1, t, kFocusS};
  glideTo(layout_.place(pos).cell.center(), cam_.scale, t);
}

void Interaction::unfocus() {
  if (!focusTarget_) return;
  focusTarget_ = false;
  focusTween_ = {focusProgress_, 0, now_, kFocusS};
}

std::optional<uint32_t> Interaction::focused() const {
  if (focusTarget_ || focusProgress_ > 0) return focused_;
  return std::nullopt;
}

std::optional<uint32_t> Interaction::nearestPosition(Vec2d world) const {
  std::vector<uint32_t> cand;
  layout_.visible({world.x - 1, world.y - 1, world.x + 1, world.y + 1}, cand);
  std::optional<uint32_t> best;
  double bestD = std::numeric_limits<double>::max();
  for (uint32_t p : cand) {
    const double d = length(layout_.place(p).cell.center() - world);
    if (d < bestD) {
      bestD = d;
      best = p;
    }
  }
  return best;
}

void Interaction::clampCenter() {
  if (layout_.count() == 0) return;
  const Rect b = layout_.bounds();
  const Vec2d c = cam_.center;
  cam_.center = {std::clamp(c.x, b.x0, b.x1), std::clamp(c.y, b.y0, b.y1)};
  if (cam_.center.x != c.x) momentum_.velocity.x = 0;
  if (cam_.center.y != c.y) momentum_.velocity.y = 0;
  if (momentum_.active && length(momentum_.velocity) < 5) momentum_.stop();
}

void Interaction::onPointerDown(Vec2d px, double t) {
  now_ = t;
  pressed_ = true;
  dragging_ = false;
  pressPos_ = lastPos_ = px;
  stopMotion();
  tracker_.reset();
  tracker_.add(t, px);
}

void Interaction::onPointerMove(Vec2d px, double t) {
  now_ = t;
  if (!pressed_) return;
  if (!dragging_ && length(px - pressPos_) >= kClickSlopPx) {
    dragging_ = true;
    unfocus();
  }
  if (dragging_) {
    cam_.center = cam_.center - (px - lastPos_) / cam_.scale;
    lastPos_ = px;
    clampCenter();
  }
  tracker_.add(t, px);
}

void Interaction::onPointerUp(Vec2d px, double t) {
  now_ = t;
  if (!pressed_) return;
  pressed_ = false;
  if (!dragging_ && length(px - pressPos_) >= kClickSlopPx) {
    dragging_ = true;
    cam_.center = cam_.center - (px - lastPos_) / cam_.scale;
  }
  if (dragging_) {
    tracker_.add(t, px);
    momentum_.start(tracker_.velocity(t));
    return;
  }
  handleClick(px, t);
}

void Interaction::handleClick(Vec2d px, double t) {
  if (focusTarget_) {
    if (!focusRect_.contains(px)) unfocus();
    return;
  }
  const auto hit = layout_.hit(cam_.screenToWorld(px));
  if (!hit) {
    lastClickPos_.reset();
    return;
  }
  const bool isDouble = lastClickPos_ == hit && t - lastClickTime_ <= kDoubleClickS;
  if (isDouble) {
    lastClickPos_.reset();
    focus(*hit, t);
    return;
  }
  lastClickPos_ = hit;
  lastClickTime_ = t;
  glideTo(layout_.place(*hit).cell.center(), cam_.scale, t);
}

void Interaction::onWheel(Vec2d px, double notches) {
  if (focusTarget_) {
    if (notches < 0) unfocus();
    return;
  }
  const double hi = std::log(maxScale_), lo = std::log(minScale_);
  if (notches > 0 && logZoom_.target >= hi - 1e-9) {
    if (auto hit = layout_.hit(cam_.screenToWorld(px))) focus(*hit, now_);
    return;
  }
  momentum_.stop();
  if (tween_.active) {
    tween_.active = false;
    logZoom_.current = std::log(cam_.scale);
    logZoom_.target = logZoom_.current;
  }
  zoomPivot_ = px;
  logZoom_.target = std::clamp(logZoom_.target + notches * std::log(kWheelFactor), lo, hi);
}

bool Interaction::onKey(Key k, double t) {
  now_ = t;
  const Vec2d screenMid = cam_.viewport * 0.5;
  if (focusTarget_) {
    switch (k) {
      case Key::Escape: unfocus(); return true;
      case Key::Left:
      case Key::Right: {
        const int64_t step = k == Key::Left ? -1 : 1;
        const int64_t next = std::clamp<int64_t>(int64_t(*focused_) + step, 0, int64_t(layout_.count()) - 1);
        focused_ = uint32_t(next);
        glideTo(layout_.place(*focused_).cell.center(), cam_.scale, t);
        return true;
      }
      case Key::ZoomOut: unfocus(); return true;
      default: return true;
    }
  }
  switch (k) {
    case Key::Escape: return false;
    case Key::Left:
    case Key::Right:
    case Key::Up:
    case Key::Down: {
      const Vec2d dir = k == Key::Left ? Vec2d{-1, 0} : k == Key::Right ? Vec2d{1, 0}
                      : k == Key::Up   ? Vec2d{0, -1} : Vec2d{0, 1};
      const Vec2d from = tween_.active ? Vec2d{tween_.x.to, tween_.y.to} : cam_.center;
      if (auto p = nearestPosition(from + dir)) glideTo(layout_.place(*p).cell.center(), cam_.scale, t);
      return true;
    }
    case Key::ZoomIn: onWheel(screenMid, 1); return true;
    case Key::ZoomOut: onWheel(screenMid, -1); return true;
    case Key::Home: {
      const Vec2d c = layout_.count() > 0 ? layout_.place(layout_.centerPosition()).cell.center()
                                          : layout_.bounds().center();
      glideTo(c, startScale(), t);
      return true;
    }
    case Key::Enter:
      if (auto p = nearestPosition(cam_.center)) focus(*p, t);
      return true;
  }
  return false;
}

void Interaction::update(double t, double dt) {
  now_ = t;
  const Vec2d before = cam_.center;
  if (tween_.active) {
    cam_.center = {tween_.x.value(t), tween_.y.value(t)};
    cam_.scale = std::exp(tween_.logScale.value(t));
    if (tween_.x.done(t)) {
      tween_.active = false;
      logZoom_.current = logZoom_.target = std::log(cam_.scale);
    }
  } else if (!logZoom_.settled()) {
    logZoom_.step(dt);
    cam_.zoomAbout(zoomPivot_, std::exp(logZoom_.current));
  }
  if (momentum_.active && !pressed_) cam_.center = cam_.center - momentum_.step(dt) / cam_.scale;
  clampCenter();

  focusProgress_ = std::clamp(focusTween_.value(t), 0.0, 1.0);
  if (!focusTarget_ && focusTween_.done(t)) focusProgress_ = 0;

  velocityPx_ = dt > 0 ? (cam_.center - before) * (cam_.scale / dt) : Vec2d{};
}

bool Interaction::idle() const {
  const bool focusSettled = focusTween_.done(now_);
  return !pressed_ && !momentum_.active && !tween_.active && logZoom_.settled() && focusSettled;
}

}  // namespace pb
