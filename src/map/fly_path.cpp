#include "map/fly_path.h"

#include <algorithm>
#include <cmath>

namespace pb {

FlyPath::FlyPath(Vec2d c0, double w0, Vec2d c1, double w1, double rho)
    : c0_(c0), c1_(c1), w0_(w0), w1_(w1), rho_(rho), d_(pb::length(c1 - c0)) {
  if (d_ < 1e-12) {
    zoomOnly_ = true;
    s_ = std::abs(std::log(w1 / w0)) / rho;
    return;
  }
  const double rho2 = rho * rho, rho4 = rho2 * rho2;
  const double b0 = (w1 * w1 - w0 * w0 + rho4 * d_ * d_) / (2 * w0 * rho2 * d_);
  const double b1 = (w1 * w1 - w0 * w0 - rho4 * d_ * d_) / (2 * w1 * rho2 * d_);
  r0_ = std::log(std::sqrt(b0 * b0 + 1) - b0);
  const double r1 = std::log(std::sqrt(b1 * b1 + 1) - b1);
  s_ = (r1 - r0_) / rho;
}

void FlyPath::at(double s, Vec2d& c, double& w) const {
  if (s <= 0) {
    c = c0_, w = w0_;
    return;
  }
  if (s >= s_) {
    c = c1_, w = w1_;
    return;
  }
  if (zoomOnly_) {
    c = c0_;
    w = w0_ * std::exp((w1_ > w0_ ? 1 : -1) * rho_ * s);
    return;
  }
  const double rho2 = rho_ * rho_;
  const double u = w0_ / rho2 * (std::cosh(r0_) * std::tanh(rho_ * s + r0_) - std::sinh(r0_));
  c = c0_ + (c1_ - c0_) * (u / d_);
  w = w0_ * std::cosh(r0_) / std::cosh(rho_ * s + r0_);
}

}  // namespace pb
