#include <doctest/doctest.h>

#include <chrono>
#include <vector>

#include "layout/content.h"
#include "layout/group_year.h"
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
  CHECK(pb::makeLayout("square", 0.1) != nullptr);
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

namespace {
int64_t dayOf(int y, unsigned m = 6, unsigned d = 15) {
  using namespace std::chrono;
  return duration_cast<seconds>(sys_days{year{y} / month{m} / day{d}}.time_since_epoch()).count();
}
}  // namespace

TEST_CASE("layouts: registry names, aliases and F4 cycling order") {
  REQUIRE(pb::layoutNames().size() >= 2);
  CHECK(pb::layoutNames()[0] == "square");  // the default
  CHECK(pb::canonicalLayoutName("square") == "square");
  CHECK(pb::canonicalLayoutName("square-crop") == "square");  // the old name still works
  CHECK(pb::canonicalLayoutName("group_year") == "group_year");
  CHECK(pb::canonicalLayoutName("spiral").empty());
  CHECK(pb::nextLayoutName("square") == "group_year");
  CHECK(pb::nextLayoutName("group_year") == "square");  // wraps around
  CHECK(pb::nextLayoutName("spiral") == "square");
  for (std::string_view n : pb::layoutNames()) CHECK(pb::makeLayout(n) != nullptr);
  CHECK(pb::makeLayout("spiral") == nullptr);
}

TEST_CASE("group_year: one square grid per year, side by side with a gap") {
  // 5 photos in 2019 (3x2 grid), 1 in 2020 (1x1), 10 in 2022 (4x3).
  std::vector<int64_t> taken;
  for (int i = 0; i < 5; ++i) taken.push_back(dayOf(2019, 3, 1 + i));
  taken.push_back(dayOf(2020));
  for (int i = 0; i < 10; ++i) taken.push_back(dayOf(2022, 1, 1 + i));
  pb::GroupYearLayout g(0.0);
  g.build({uint32_t(taken.size()), taken});
  CHECK(g.count() == 16);
  const double gap = pb::GroupYearLayout::kGroupGap;
  CHECK(g.place(0).cell == pb::Rect{0, 0, 1, 1});
  CHECK(g.place(2).cell == pb::Rect{2, 0, 3, 1});
  CHECK(g.place(3).cell == pb::Rect{0, 1, 1, 2});  // 2019 wraps after 3 columns
  CHECK(g.place(5).cell == pb::Rect{3 + gap, 0, 4 + gap, 1});           // 2020
  const double x2022 = 4 + 2 * gap;
  CHECK(g.place(6).cell == pb::Rect{x2022, 0, x2022 + 1, 1});
  CHECK(g.place(15).cell == pb::Rect{x2022 + 1, 2, x2022 + 2, 3});     // 10th of 4 columns
  CHECK(g.bounds() == pb::Rect{0, -pb::GroupYearLayout::kLabelBand, x2022 + 4, 3});  // room for labels

  const auto labels = g.labels();
  REQUIRE(labels.size() == 3);
  CHECK(labels[0].text == "2019");
  CHECK(labels[0].group == pb::Rect{0, 0, 3, 2});
  CHECK(labels[0].space == pb::GroupYearLayout::kLabelBand);
  CHECK(labels[1].text == "2020");
  CHECK(labels[2].text == "2022");
  CHECK(labels[2].group == pb::Rect{x2022, 0, x2022 + 4, 3});
}

TEST_CASE("group_year: visibility and hit testing follow the groups") {
  std::vector<int64_t> taken = {dayOf(2019), dayOf(2019), dayOf(2019), dayOf(2019), dayOf(2021)};
  pb::GroupYearLayout g(0.1);
  g.build({uint32_t(taken.size()), taken});  // 2019: 2x2 at x 0..2, 2021: 1x1 after the gap
  const double x2021 = 2 + pb::GroupYearLayout::kGroupGap;
  std::vector<uint32_t> v;
  g.visible({-10, -10, 100, 100}, v);
  CHECK(v.size() == 5);
  v.clear();
  g.visible({1.5, 1.5, x2021 + 0.5, 1.8}, v);  // bottom-right of 2019; 2021 has no second row
  CHECK(v == std::vector<uint32_t>{3});
  v.clear();
  g.visible({x2021 - 0.2, 0.2, x2021 + 0.2, 0.4}, v);
  CHECK(v == std::vector<uint32_t>{4});
  CHECK(g.hit({0.5, 1.5}).value() == 2);
  CHECK(g.hit({x2021 + 0.5, 0.5}).value() == 4);
  CHECK_FALSE(g.hit({2.0 + pb::GroupYearLayout::kGroupGap / 2, 0.5}));  // in the gap
  CHECK_FALSE(g.hit({x2021 + 0.5, 1.5}));                                // below a short group
  CHECK_FALSE(g.hit({0.02, 0.5}));                                       // in a photo's inset
}

TEST_CASE("group_year: newest first, missing dates and empty") {
  std::vector<int64_t> taken = {dayOf(2021), dayOf(2019), dayOf(2019)};
  pb::GroupYearLayout g;
  g.build({3, taken});
  REQUIRE(g.labels().size() == 2);
  CHECK(g.labels()[0].text == "2021");
  CHECK(g.labels()[1].text == "2019");
  CHECK(g.place(1).cell.x0 == doctest::Approx(1 + pb::GroupYearLayout::kGroupGap));
  CHECK(g.centerPosition() < 3);

  g.build(4);  // no dates: a single unlabelled group, like the square grid
  CHECK(g.labels().empty());
  CHECK(g.bounds() == pb::Rect{0, 0, 2, 2});
  g.build(0);
  CHECK(g.count() == 0);
  CHECK(g.labels().empty());
  std::vector<uint32_t> v;
  g.visible({-10, -10, 10, 10}, v);
  CHECK(v.empty());
  CHECK_FALSE(g.hit({0.5, 0.5}));
}

TEST_CASE("group_year: the centre position is a photo near the middle") {
  std::vector<int64_t> taken;
  for (int y = 2010; y < 2015; ++y)
    for (int i = 0; i < 9; ++i) taken.push_back(dayOf(y, 1, 1 + i));
  pb::GroupYearLayout g;
  g.build({uint32_t(taken.size()), taken});
  CHECK(g.centerPosition() == 2 * 9 + 4);  // middle of the middle year
}
