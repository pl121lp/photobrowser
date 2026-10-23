#include <doctest/doctest.h>

#include <vector>

#include "layout/content.h"
#include "layout/square_grid.h"

TEST_CASE("grid: 20 photos is 5x4, 10000 is 100x100") {
  pb::SquareGridLayout g;
  g.build(20);
  CHECK(g.cols() == 5);
  CHECK(g.rows() == 4);
  g.build(10000);
  CHECK(g.cols() == 100);
  CHECK(g.rows() == 100);
}

TEST_CASE("grid: small counts") {
  pb::SquareGridLayout g;
  for (uint32_t n : {0u, 1u, 2u}) {
    g.build(n);
    std::vector<uint32_t> v;
    g.visible({-10, -10, 10, 10}, v);
    CHECK(v.size() == n);
    CHECK(g.count() == n);
  }
}

TEST_CASE("grid: placement row-major with gap") {
  pb::SquareGridLayout g;
  g.build(20);
  auto p = g.place(7);
  CHECK(p.cell.x0 == 2);
  CHECK(p.cell.y0 == 1);
  CHECK(p.photo.x0 == doctest::Approx(2.03));
  CHECK(p.photo.x1 == doctest::Approx(2.97));
}

TEST_CASE("grid: hit on empty cell") {
  pb::SquareGridLayout g;
  g.build(18);
  CHECK(g.hit({2.5, 1.5}).value() == 7);
  CHECK_FALSE(g.hit({4.5, 3.5}));   // trailing empty cell
  CHECK_FALSE(g.hit({2.01, 1.5}));  // in gap
  CHECK_FALSE(g.hit({-1, 0}));
}

TEST_CASE("grid: visible is a tight range") {
  pb::SquareGridLayout g;
  g.build(10000);
  std::vector<uint32_t> v;
  g.visible({10.5, 20.5, 12.5, 21.5}, v);
  CHECK(v.size() == 6);
}

TEST_CASE("grid: center position") {
  pb::SquareGridLayout g;
  g.build(20);
  CHECK(g.centerPosition() == 7);  // (2,1) and (2,2) tie -> lower
  g.build(10000);
  CHECK(g.centerPosition() == 4949);  // four cells tie around (50,50) -> lowest (49,49)
  g.build(18);  // 5x4, nearest occupied to (2.5,2) is (2,1)=7 or (2,2)=12
  CHECK(g.centerPosition() == 7);
}

TEST_CASE("grid: bounds") {
  pb::SquareGridLayout g;
  g.build(20);
  CHECK(g.bounds() == pb::Rect{0, 0, 5, 4});
}

TEST_CASE("content: crop portrait to square") {
  auto uv = pb::cropUv(3.0 / 4.0, 1.0);
  CHECK(uv.u0 == 0);
  CHECK(uv.u1 == 1);
  CHECK(uv.v0 == doctest::Approx(0.125));
  CHECK(uv.v1 == doctest::Approx(0.875));
}

TEST_CASE("content: crop landscape to square") {
  auto uv = pb::cropUv(4.0 / 3.0, 1.0);
  CHECK(uv.u0 == doctest::Approx(0.125));
  CHECK(uv.v0 == 0);
}

TEST_CASE("content: fit landscape into square") {
  auto r = pb::fitRect({0, 0, 1, 1}, 4.0 / 3.0);
  CHECK(r.y0 == doctest::Approx(0.125));
  CHECK(r.x0 == 0);
  CHECK(r.x1 == 1);
}

TEST_CASE("layout: factory") {
  CHECK(pb::makeLayout("square-crop") != nullptr);
  CHECK(pb::makeLayout("nope") == nullptr);
}

TEST_CASE("content: whole-photo morph endpoints and no distortion") {
  const pb::Rect cell{100, 100, 300, 300};
  const pb::Vec2d vp{1000, 800};
  const double a = 3.0 / 4.0;
  auto g0 = pb::focusMorph(cell, a, vp, 0);
  CHECK(g0.rect == cell);
  CHECK(g0.uv.v0 == doctest::Approx(0.125));
  auto g1 = pb::focusMorph(cell, a, vp, 1);
  CHECK(g1.rect.height() == doctest::Approx(800 - 2 * 16));  // 2% margin of the short side
  CHECK(g1.rect.width() / g1.rect.height() == doctest::Approx(a));
  CHECK(g1.rect.center().x == doctest::Approx(500));
  CHECK(g1.uv.u0 == 0);
  CHECK(g1.uv.v0 == doctest::Approx(0));
  auto gm = pb::focusMorph(cell, a, vp, 0.5);
  // The visible part of the image always has the rect's aspect ratio.
  const double visibleAspect = a * (gm.uv.u1 - gm.uv.u0) / (gm.uv.v1 - gm.uv.v0);
  CHECK(visibleAspect == doctest::Approx(gm.rect.width() / gm.rect.height()));
}

TEST_CASE("grid: zero gap means photos touch") {
  pb::SquareGridLayout g(0.0);
  g.build(4);
  CHECK(g.place(3).photo == g.place(3).cell);
  CHECK(g.hit({1.0001, 1.5}).value() == 3);
  CHECK(pb::makeLayout("square-crop", 0.1) != nullptr);
}

TEST_CASE("content: letterbox a landscape photo in a square tile") {
  const auto lb = pb::letterbox(2.0, 1.0);  // 2:1 photo, square tile
  CHECK(lb.content.u0 == 0);
  CHECK(lb.content.u1 == 1);
  CHECK(lb.content.v0 == doctest::Approx(0.25));
  CHECK(lb.content.v1 == doctest::Approx(0.75));
  // Image coordinates across the whole tile: the photo maps exactly onto `content`.
  CHECK(lb.uv.u0 == doctest::Approx(0));
  CHECK(lb.uv.u1 == doctest::Approx(1));
  CHECK(lb.uv.v0 == doctest::Approx(-0.5));
  CHECK(lb.uv.v1 == doctest::Approx(1.5));
}

TEST_CASE("content: letterbox a portrait photo, and a photo that already fits") {
  const auto lb = pb::letterbox(0.75, 1.0);
  CHECK(lb.content.u0 == doctest::Approx(0.125));
  CHECK(lb.content.u1 == doctest::Approx(0.875));
  CHECK(lb.content.v0 == 0);
  CHECK(lb.uv.u0 == doctest::Approx(-0.125 / 0.75));
  CHECK(lb.uv.u1 == doctest::Approx(1 + 0.125 / 0.75));
  const auto same = pb::letterbox(1.0, 1.0);
  CHECK(same.content.u0 == 0);
  CHECK(same.content.v1 == 1);
  CHECK(same.uv.u0 == doctest::Approx(0));
  CHECK(same.uv.v1 == doctest::Approx(1));
}

TEST_CASE("content: letterboxed whole-photo morph starts at the letterboxed photo") {
  const pb::Rect cell{100, 100, 300, 300};
  const double a = 2.0;
  const pb::Rect start = pb::fitRect(cell, a);
  auto g0 = pb::focusMorph(start, a, {1000, 800}, 0);
  CHECK(g0.rect == start);
  CHECK(g0.uv.u0 == doctest::Approx(0));
  CHECK(g0.uv.v0 == doctest::Approx(0));
  CHECK(g0.uv.v1 == doctest::Approx(1));
}
