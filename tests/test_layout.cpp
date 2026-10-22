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
