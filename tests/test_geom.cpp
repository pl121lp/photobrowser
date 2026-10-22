#include <doctest/doctest.h>

#include "core/geom.h"

TEST_CASE("geom: rect basics") {
  pb::Rect r{1, 2, 5, 4};
  CHECK(r.width() == 4);
  CHECK(r.height() == 2);
  CHECK(r.center() == pb::Vec2d{3, 3});
  CHECK(r.contains({1, 2}));
  CHECK_FALSE(r.contains({5.5, 3}));
  CHECK(r.intersects({4, 3, 9, 9}));
  CHECK_FALSE(r.intersects({6, 0, 7, 1}));
  CHECK(r.inset(0.5).x0 == doctest::Approx(1.5));
}
