#include "view/interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace pb {

namespace {
constexpr double kClickSlopPx = 4;
constexpr double kDoubleClickS = 0.35;
constexpr double kGlideS = 0.4;
constexpr double kFocusS = 0.35;
constexpr double kWheelFactor = 1.25;
constexpr double kFitMargin = 0.95;
constexpr double kFlyMinS = 2, kFlyMaxS = 6;  // flyTo duration: 1.5 s + 0.8 s per doubling of distance
}  // namespace

Interaction::Interaction(Camera& camera, const Layout& layout, double maxPhotoZoom)
    : cam_(camera), layout_(layout), maxPhotoZoom_(std::max(1.0, maxPhotoZoom)) {
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
  if (startZoom_ == StartZoom::Full) return minScale_;  // Photo: the usual grid zoom behind it
  return std::clamp(std::min(cam_.viewport.x, cam_.viewport.y) / 3, minScale_, maxScale_);
}

void Interaction::setViewport(Vec2d px) {
  cam_.viewport = px;  // camera center stays at the screen center
  if (focusTarget_) focusRectValid_ = false;
  computeLimits();
  const double lo = std::log(minScale_), hi = std::log(maxScale_);
  // A zoom at rest that the new limits push out of range snaps to them at once (about
  // the screen centre); easing would zoom about a stale pivot and shift the view.
  const bool wasSettled = logZoom_.settled();
  logZoom_.target = std::clamp(logZoom_.target, lo, hi);
  if (tween_.active) tween_.logScale.to = std::clamp(tween_.logScale.to, lo, hi);
  if (!tween_.active && wasSettled) {
    logZoom_.current = logZoom_.target;
    cam_.scale = std::exp(logZoom_.current);
  }
  clampFocusPan();
}

void Interaction::layoutChanged() { computeLimits(); }

uint32_t startPosition(StartAt at, uint32_t count, uint32_t centerPos, bool newestFirst, uint32_t randomValue) {
  if (count == 0) return 0;
  switch (at) {
    case StartAt::Oldest: return newestFirst ? count - 1 : 0;
    case StartAt::Newest: return newestFirst ? 0 : count - 1;
    case StartAt::Random: return randomValue % count;
    case StartAt::Center: break;
  }
  return centerPos;
}

uint32_t Interaction::startPos() const {
  return start_ && *start_ < layout_.count() ? *start_ : layout_.centerPosition();
}

Vec2d Interaction::startCenter() const {
  if (layout_.count() == 0 || startZoom_ == StartZoom::Full) return layout_.bounds().center();
  return layout_.place(startPos()).cell.center();
}

void Interaction::resetView() {
  stopMotion();
  cam_.center = startCenter();
  if (startZoom_ == StartZoom::Photo && layout_.count() > 0) {
    // Start in whole-photo view on the start photo, already open (no transition).
    focus(startPos(), now_);
    focusTween_ = {1, 1, now_, 0};
    focusProgress_ = 1;
    tween_.active = false;
  }
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
  tween_.pullback = 0;
}

void Interaction::flyTo(uint32_t pos, double t) {
  if (pos >= layout_.count()) return;
  const Vec2d dest = layout_.place(pos).cell.center();
  const double dist = length(dest - cam_.center);  // world units = cells
  const double duration = std::clamp(1.5 + 0.8 * std::log2(1 + dist), kFlyMinS, kFlyMaxS);
  glideTo(dest, maxScale_, t);
  tween_.x.duration = tween_.y.duration = tween_.logScale.duration = duration;
  // At mid-flight, pull back far enough to see both ends (never past the whole-grid fit).
  const double mid = 0.5 * (tween_.logScale.from + tween_.logScale.to);
  const double wide = std::clamp(std::min(cam_.viewport.x, cam_.viewport.y) / (dist + 2), minScale_, maxScale_);
  tween_.pullback = std::min(0.0, std::log(wide) - mid);
}

void Interaction::focus(uint32_t pos, double t) {
  resetFocusZoom(true);
  focusRectValid_ = false;
  focused_ = pos;
  focusTarget_ = true;
  focusTween_ = {focusProgress_, 1, t, kFocusS};
  glideTo(layout_.place(pos).cell.center(), cam_.scale, t);
}

void Interaction::unfocus(bool smoothZoom) {
  if (!focusTarget_) return;
  focusTarget_ = false;
  focusRectValid_ = false;
  resetFocusZoom(!smoothZoom);
  focusTween_ = {focusProgress_, 0, now_, kFocusS};
}

double Interaction::focusZoom() const { return std::exp(focusLogZoom_.current); }

void Interaction::setFocusZoom(double zoom) {
  focusLogZoom_.current = focusLogZoom_.target = std::log(std::clamp(zoom, 1.0, maxPhotoZoom_));
  clampFocusPan();
}

void Interaction::setFocusRect(const Rect& screen) {
  focusRect_ = screen;
  focusRectValid_ = true;
  clampFocusPan();
}

Rect Interaction::focusDisplayRect() const {
  const double z = focusZoom();
  const Vec2d c = focusRect_.center() + focusPan_;
  const double hw = focusRect_.width() * z * 0.5;
  const double hh = focusRect_.height() * z * 0.5;
  return {c.x - hw, c.y - hh, c.x + hw, c.y + hh};
}

void Interaction::resetFocusZoom(bool immediate) {
  focusZoomPivot_ = cam_.viewport * 0.5;
  focusLogZoom_.target = 0;
  if (immediate) focusLogZoom_.current = 0;
  if (immediate) focusPan_ = {};
}

void Interaction::clampFocusPan() {
  const double x = std::max(0.0, (focusRect_.width() * focusZoom() - cam_.viewport.x) * 0.5);
  const double y = std::max(0.0, (focusRect_.height() * focusZoom() - cam_.viewport.y) * 0.5);
  focusPan_.x = std::clamp(focusPan_.x, -x, x);
  focusPan_.y = std::clamp(focusPan_.y, -y, y);
}

void Interaction::beginDrag() {
  dragging_ = true;
  focusDragging_ = focusTarget_ && focusZoom() > 1 + 1e-6;
  if (!focusDragging_) unfocus();
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

std::optional<uint32_t> Interaction::closestInRow(uint32_t pos, int dir) const {
  const Rect cell = layout_.place(pos).cell;
  const double y = cell.center().y + dir * cell.height(), cx = cell.center().x;
  const Rect b = layout_.bounds();
  std::vector<uint32_t> cand;
  layout_.visible({b.x0, y - cell.height() * 0.25, b.x1, y + cell.height() * 0.25}, cand);
  std::optional<uint32_t> best;
  double bestD = std::numeric_limits<double>::max();
  for (uint32_t p : cand) {
    const double d = std::abs(layout_.place(p).cell.center().x - cx);
    if (d < bestD) {
      bestD = d;
      best = p;
    }
  }
  return best;
}

void Interaction::showFocused(uint32_t pos, double t) {
  focused_ = pos;
  resetFocusZoom(true);
  focusRectValid_ = false;
  glideTo(layout_.place(pos).cell.center(), cam_.scale, t);
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
  focusDragging_ = false;
  pressPos_ = lastPos_ = px;
  stopMotion();
  focusLogZoom_.target = focusLogZoom_.current;
  tracker_.reset();
  tracker_.add(t, px);
}

void Interaction::onPointerMove(Vec2d px, double t) {
  now_ = t;
  if (!pressed_) return;
  if (!dragging_ && length(px - pressPos_) >= kClickSlopPx) beginDrag();
  if (dragging_) {
    if (focusDragging_) {
      focusPan_ = focusPan_ + (px - lastPos_);
      clampFocusPan();
    } else {
      cam_.center = cam_.center - (px - lastPos_) / cam_.scale;
    }
    lastPos_ = px;
    if (!focusDragging_) clampCenter();
  }
  tracker_.add(t, px);
}

void Interaction::onPointerUp(Vec2d px, double t) {
  now_ = t;
  if (!pressed_) return;
  pressed_ = false;
  if (!dragging_ && length(px - pressPos_) >= kClickSlopPx) {
    beginDrag();
    if (focusDragging_) {
      focusPan_ = focusPan_ + (px - lastPos_);
      clampFocusPan();
    } else {
      cam_.center = cam_.center - (px - lastPos_) / cam_.scale;
    }
  }
  if (dragging_) {
    if (focusDragging_) return;
    tracker_.add(t, px);
    momentum_.start(tracker_.velocity(t));
    return;
  }
  handleClick(px, t);
}

void Interaction::handleClick(Vec2d px, double t) {
  if (focusTarget_) {
    if (focusDisplayRect().contains(px)) {
      activated_ = true;
    } else {
      unfocus();
    }
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

void Interaction::onSecondaryClick() {
  if (focusTarget_) resetFocusZoom(false);
}

void Interaction::onWheel(Vec2d px, double notches) {
  if (focusTarget_) {
    const bool zoomed = focusLogZoom_.current > 1e-6 || focusLogZoom_.target > 1e-6;
    if (notches > 0 || zoomed) {
      focusZoomPivot_ = px;
      focusLogZoom_.target = std::clamp(focusLogZoom_.target + notches * std::log(kWheelFactor), 0.0,
                                        std::log(maxPhotoZoom_));
    } else if (notches < 0) {
      unfocus();
    }
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

bool Interaction::onKey(Key k, double t, int steps) {
  steps = std::max(1, steps);
  now_ = t;
  const Vec2d screenMid = cam_.viewport * 0.5;
  if (focusTarget_) {
    switch (k) {
      case Key::Escape: unfocus(); return true;
      case Key::Left:
      case Key::Right: {
        const int64_t step = k == Key::Left ? -steps : steps;
        showFocused(uint32_t(std::clamp<int64_t>(int64_t(*focused_) + step, 0, int64_t(layout_.count()) - 1)), t);
        return true;
      }
      case Key::Up:
      case Key::Down:
      {
        std::optional<uint32_t> to;
        for (int i = 0; i < steps; ++i) {
          auto next = closestInRow(to.value_or(*focused_), k == Key::Up ? -1 : 1);
          if (!next) break;  // top or bottom row
          to = next;
        }
        if (to) showFocused(*to, t);
        return true;
      }
      case Key::ZoomOut: unfocus(); return true;
      case Key::Home:  // start view "photo": back to the start photo, still whole
        if (startZoom_ == StartZoom::Photo) showFocused(startPos(), t);
        return true;
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
      Vec2d at = tween_.active ? Vec2d{tween_.x.to, tween_.y.to} : cam_.center;
      std::optional<uint32_t> to;
      for (int i = 0; i < steps; ++i) {
        auto p = nearestPosition(at + dir);
        if (!p) break;
        to = p;
        at = layout_.place(*p).cell.center();
      }
      if (to) glideTo(layout_.place(*to).cell.center(), cam_.scale, t);
      return true;
    }
    case Key::ZoomIn: onWheel(screenMid, 1); return true;
    case Key::ZoomOut: onWheel(screenMid, -1); return true;
    case Key::Home:
      if (startZoom_ == StartZoom::Photo && layout_.count() > 0) {
        focus(startPos(), t);
        return true;
      }
      glideTo(startCenter(), startScale(), t);
      return true;
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
    const double u = std::clamp((t - tween_.x.t0) / tween_.x.duration, 0.0, 1.0);
    cam_.scale = std::exp(tween_.logScale.value(t) + tween_.pullback * std::sin(std::numbers::pi * u));
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

  if (!focusLogZoom_.settled() && !focusRectValid_ && !focusTarget_) {
    focusLogZoom_.step(dt);  // closing: ease back to fit even before the next rect
  } else if (!focusLogZoom_.settled() && focusRectValid_) {
    const double before = focusZoom();
    const Vec2d oldCenter = focusRect_.center() + focusPan_;
    focusLogZoom_.step(dt);
    const double ratio = focusZoom() / before;
    const Vec2d newCenter = focusZoomPivot_ + (oldCenter - focusZoomPivot_) * ratio;
    focusPan_ = newCenter - focusRect_.center();
    clampFocusPan();
  }

  focusProgress_ = std::clamp(focusTween_.value(t), 0.0, 1.0);
  if (!focusTarget_ && focusTween_.done(t)) focusProgress_ = 0;

  velocityPx_ = dt > 0 ? (cam_.center - before) * (cam_.scale / dt) : Vec2d{};
}

bool Interaction::idle() const {
  const bool focusSettled = focusTween_.done(now_);
  return !pressed_ && !momentum_.active && !tween_.active && logZoom_.settled() && focusLogZoom_.settled() &&
         focusSettled;
}

}  // namespace pb
