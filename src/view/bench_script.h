#pragma once

#include <functional>
#include <vector>

#include "core/geom.h"
#include "view/interaction.h"

namespace pb {

// Scripted ~10 s tour for --bench: zoom out to the whole grid, fling across it,
// zoom into a corner, open the whole-photo view, step through photos, leave.
class BenchScript {
 public:
  BenchScript();
  // Feeds the input events due by time t (seconds, any monotonic clock).
  void drive(Interaction& ui, Vec2d viewport, double t);
  bool done(double t) const { return started_ && t - t0_ >= kDuration; }

 private:
  static constexpr double kDuration = 10.5;
  struct Step {
    double at;
    std::function<void(Interaction&, Vec2d, double)> act;
  };
  std::vector<Step> steps_;
  size_t next_ = 0;
  bool started_ = false;
  double t0_ = 0;
};

}  // namespace pb
