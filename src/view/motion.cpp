#include "view/motion.h"

#include <algorithm>
#include <cmath>

namespace pb {

double easeInOutCubic(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

double Tween::value(double now) const {
  if (duration <= 0) return to;
  return from + (to - from) * easeInOutCubic((now - t0) / duration);
}

void SmoothValue::step(double dt) {
  current += (target - current) * (1 - std::exp(-rate * dt));
  if (std::abs(target - current) <= 1e-6 * std::max(1.0, std::abs(target))) current = target;
}

void VelocityTracker::add(double t, Vec2d p) {
  samples_.emplace_back(t, p);
  while (samples_.size() > 2 && samples_.front().first < t - 0.05) samples_.pop_front();
}

Vec2d VelocityTracker::velocity(double now) const {
  if (samples_.size() < 2 || now - samples_.back().first > 0.08) return {};
  const auto& [t0, p0] = samples_.front();
  const auto& [t1, p1] = samples_.back();
  if (t1 - t0 <= 0) return {};
  return (p1 - p0) / (t1 - t0);
}

void Momentum::start(Vec2d v) {
  if (length(v) < 50) {
    stop();
    return;
  }
  velocity = v;
  active = true;
}

Vec2d Momentum::step(double dt) {
  if (!active) return {};
  const double decay = std::exp(-lambda * dt);
  // Exact integral of v*exp(-lambda*t) over the step.
  const Vec2d d = velocity * ((1 - decay) / lambda);
  velocity = velocity * decay;
  if (length(velocity) < 5) stop();
  return d;
}

Vec2d Momentum::landingOffset() const { return active ? velocity / lambda : Vec2d{}; }

}  // namespace pb
