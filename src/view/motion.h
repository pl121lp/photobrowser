#pragma once

#include <deque>
#include <utility>

#include "core/geom.h"

namespace pb {

double easeInOutCubic(double t);  // clamps t to [0,1]

// Eased (ease-in-out cubic) interpolation from `from` to `to` over [t0, t0+duration].
struct Tween {
  double from = 0, to = 0, t0 = 0, duration = 0;
  double value(double now) const;
  bool done(double now) const { return now >= t0 + duration; }
};

// Exponential approach to `target`, independent of frame rate.
struct SmoothValue {
  double current = 0, target = 0;
  double rate = 18;  // 1/s
  void step(double dt);
  bool settled() const { return current == target; }
};

// Estimates pointer velocity (px/s) from the last 50 ms of samples.
class VelocityTracker {
 public:
  void reset() { samples_.clear(); }
  void add(double t, Vec2d p);
  Vec2d velocity(double now) const;

 private:
  std::deque<std::pair<double, Vec2d>> samples_;
};

// Fling glide with exponential friction. Units are whatever `start` uses (px/s).
struct Momentum {
  Vec2d velocity;
  double lambda = 5.0;  // 1/s
  bool active = false;
  void start(Vec2d v);           // only starts if |v| >= 50
  Vec2d step(double dt);         // displacement this step; decays velocity
  Vec2d landingOffset() const;   // total remaining displacement (analytic)
  void stop() { active = false; velocity = {}; }
};

}  // namespace pb
