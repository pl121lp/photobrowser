#include <doctest/doctest.h>

#include <cmath>

#include "map/fly_path.h"

namespace {
void checkView(const pb::FlyPath& p, double s, pb::Vec2d c, double w) {
  pb::Vec2d gotC;
  double gotW = 0;
  p.at(s, gotC, gotW);
  CHECK(gotC.x == doctest::Approx(c.x).epsilon(1e-12));
  CHECK(gotC.y == doctest::Approx(c.y).epsilon(1e-12));
  CHECK(gotW == doctest::Approx(w).epsilon(1e-12));
}
}  // namespace

TEST_CASE("fly: starts and ends exactly at the given views") {
  const pb::FlyPath p({0.1, 0.2}, 0.01, {0.6, 0.3}, 0.001);
  CHECK(p.length() > 0);
  checkView(p, 0, {0.1, 0.2}, 0.01);
  checkView(p, p.length(), {0.6, 0.3}, 0.001);
}

TEST_CASE("fly: zooms out mid-way for long distances, not for short ones") {
  const pb::FlyPath far({0.2, 0.5}, 0.001, {0.7, 0.5}, 0.001);
  pb::Vec2d c;
  double w = 0;
  far.at(far.length() / 2, c, w);
  CHECK(w > 10 * 0.001);
  CHECK(c.x == doctest::Approx(0.45));

  const double w0 = 0.01;
  const pb::FlyPath near({0.5, 0.5}, w0, {0.5 + 0.2 * w0, 0.5}, w0);
  for (int i = 0; i <= 20; ++i) {
    near.at(near.length() * i / 20, c, w);
    CHECK(w <= w0 * 1.05);
  }
}

TEST_CASE("fly: pure zoom and zero length") {
  const pb::FlyPath zoomOnly({0.5, 0.5}, 0.01, {0.5, 0.5}, 0.0001);
  CHECK(zoomOnly.length() == doctest::Approx(std::abs(std::log(0.0001 / 0.01)) / 1.42));
  checkView(zoomOnly, zoomOnly.length(), {0.5, 0.5}, 0.0001);
  checkView(zoomOnly, 0, {0.5, 0.5}, 0.01);
  const pb::FlyPath none({0.3, 0.3}, 0.01, {0.3, 0.3}, 0.01);
  CHECK(none.length() == 0);
  checkView(none, 0, {0.3, 0.3}, 0.01);
}
