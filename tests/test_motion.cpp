#include <doctest/doctest.h>

#include "view/motion.h"

TEST_CASE("motion: easing endpoints and symmetry") {
  CHECK(pb::easeInOutCubic(0) == 0);
  CHECK(pb::easeInOutCubic(1) == 1);
  CHECK(pb::easeInOutCubic(0.5) == doctest::Approx(0.5));
  CHECK(pb::easeInOutCubic(0.25) == doctest::Approx(1 - pb::easeInOutCubic(0.75)));
  CHECK(pb::easeInOutCubic(-1) == 0);
  CHECK(pb::easeInOutCubic(2) == 1);
}

TEST_CASE("motion: tween") {
  pb::Tween t{10, 20, 1.0, 0.5};
  CHECK(t.value(0.5) == 10);
  CHECK(t.value(1.25) == doctest::Approx(15));
  CHECK_FALSE(t.done(1.25));
  CHECK(t.value(2) == 20);
  CHECK(t.done(1.5));
}

TEST_CASE("motion: momentum travels to analytic landing point") {
  pb::Momentum m;
  m.start({500, 0});
  REQUIRE(m.active);
  auto land = m.landingOffset();
  pb::Vec2d sum{};
  for (int i = 0; i < 2000 && m.active; ++i) sum = sum + m.step(1.0 / 120);
  CHECK(sum.x == doctest::Approx(land.x).epsilon(0.02));
  CHECK_FALSE(m.active);
}

TEST_CASE("motion: slow release does not start momentum") {
  pb::Momentum m;
  m.start({30, 0});
  CHECK_FALSE(m.active);
}

TEST_CASE("motion: smooth value is frame-rate independent") {
  pb::SmoothValue a{0, 1}, b{0, 1};
  for (int i = 0; i < 6; ++i) a.step(1.0 / 60);
  for (int i = 0; i < 12; ++i) b.step(1.0 / 120);
  CHECK(a.current == doctest::Approx(b.current));
  for (int i = 0; i < 600; ++i) a.step(1.0 / 60);
  CHECK(a.current == 1);
}

TEST_CASE("motion: velocity tracker averages recent samples") {
  pb::VelocityTracker v;
  for (int i = 0; i <= 10; ++i) v.add(i * 0.01, {i * 10.0, 0});  // 1000 px/s
  CHECK(v.velocity(0.1).x == doctest::Approx(1000));
  CHECK(v.velocity(0.3).x == 0);  // stale: finger stopped before release
}
