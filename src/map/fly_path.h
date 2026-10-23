#pragma once

#include "core/geom.h"

namespace pb {

// "Smooth and efficient zooming and panning" (van Wijk & Nuij 2003), as in d3-zoom and
// Mapbox flyTo: between two views (centre, w = visible width in world units) it zooms
// out as far as the distance needs, travels, and zooms back in. rho trades zooming for
// panning (1.42 is the paper's and d3's choice).
class FlyPath {
 public:
  FlyPath(Vec2d c0, double w0, Vec2d c1, double w1, double rho = 1.42);
  double length() const { return s_; }  // S: path length, roughly "seconds at natural speed"
  // View at s in [0, S]; exactly the given views at 0 and S.
  void at(double s, Vec2d& c, double& w) const;

 private:
  Vec2d c0_, c1_;
  double w0_, w1_, rho_, d_, r0_ = 0, s_ = 0;
  bool zoomOnly_ = false;
};

}  // namespace pb
