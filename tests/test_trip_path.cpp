#include <doctest/doctest.h>

#include <vector>

#include "map/trip_path.h"

namespace {

constexpr int64_t kHour = 3600;

pb::Vec2d identity(pb::Vec2d w) { return w; }

}  // namespace

TEST_CASE("trip path: fewer than two stops is no path") {
  CHECK(pb::buildTripPath({}).empty());
  CHECK(pb::buildTripPath({{{0.5, 0.5}, 0}}).empty());
}

TEST_CASE("trip path: stops in time order, dates 0..1 from first to last") {
  const auto p = pb::buildTripPath({{{0.3, 0.5}, 2 * kHour}, {{0.1, 0.5}, 0}, {{0.2, 0.5}, kHour}});
  REQUIRE(p.size() == 3);
  CHECK(p[0].world == pb::Vec2d{0.1, 0.5});
  CHECK(p[1].world == pb::Vec2d{0.2, 0.5});
  CHECK(p[2].world == pb::Vec2d{0.3, 0.5});
  CHECK(p[0].date == 0);
  CHECK(p[1].date == doctest::Approx(0.5));
  CHECK(p[2].date == 1);
  CHECK(p[0].breakBefore);
  CHECK_FALSE(p[1].breakBefore);
  CHECK_FALSE(p[2].breakBefore);
}

TEST_CASE("trip path: same time keeps the given order; all at one time is date 0") {
  const auto p = pb::buildTripPath({{{0.3, 0.5}, 5}, {{0.1, 0.5}, 5}});
  REQUIRE(p.size() == 2);
  CHECK(p[0].world == pb::Vec2d{0.3, 0.5});
  CHECK(p[0].date == 0);
  CHECK(p[1].date == 0);
}

TEST_CASE("trip path: photos in a row at one spot are one point") {
  const auto p = pb::buildTripPath({{{0.1, 0.5}, 0}, {{0.1, 0.5}, 10}, {{0.2, 0.5}, 20}, {{0.1, 0.5}, 30}});
  REQUIRE(p.size() == 3);
  CHECK(p[0].world == pb::Vec2d{0.1, 0.5});
  CHECK(p[1].world == pb::Vec2d{0.2, 0.5});
  CHECK(p[2].world == pb::Vec2d{0.1, 0.5});  // back there later: a new point
}

TEST_CASE("trip path: more than 10 hours between photos breaks the line by default") {
  CHECK(pb::kTripMaxGapS == 10 * kHour);
  const auto p = pb::buildTripPath({{{0.1, 0.5}, 0}, {{0.2, 0.5}, 10 * kHour}, {{0.3, 0.5}, 20 * kHour + 1}});
  REQUIRE(p.size() == 3);
  CHECK_FALSE(p[1].breakBefore);  // exactly 10 h: joined
  CHECK(p[2].breakBefore);
  const auto segs = pb::tripSegments(p, identity, 0);
  REQUIRE(segs.size() == 1);
  CHECK(segs[0].b == pb::Vec2d{0.2, 0.5});
}

TEST_CASE("trip path: the gap is a parameter") {
  const std::vector<pb::TripStop> stops = {{{0.1, 0.5}, 0}, {{0.2, 0.5}, 2 * kHour}};
  CHECK_FALSE(pb::buildTripPath(stops)[1].breakBefore);
  CHECK(pb::buildTripPath(stops, kHour)[1].breakBefore);
}

TEST_CASE("trip path: a gap ends a run at a repeated spot too") {
  // At the same spot, but a day later: a new run starting there.
  const auto p = pb::buildTripPath({{{0.1, 0.5}, 0}, {{0.1, 0.5}, 24 * kHour}, {{0.2, 0.5}, 25 * kHour}});
  REQUIRE(p.size() == 3);
  CHECK(p[1].breakBefore);
  CHECK_FALSE(p[2].breakBefore);
  CHECK(p[1].date == doctest::Approx(24.0 / 25));
  const auto segs = pb::tripSegments(p, identity, 0);
  REQUIRE(segs.size() == 1);  // the lone first photo draws nothing
  CHECK(segs[0].a == pb::Vec2d{0.1, 0.5});
  CHECK(segs[0].dateA == doctest::Approx(24.0 / 25));
}

TEST_CASE("trip path: crossing the antimeridian takes the short way") {
  // 179°E to 179°W: the second point is moved one world east, just past x = 1.
  const auto p = pb::buildTripPath({{{0.995, 0.5}, 0}, {{0.005, 0.5}, 10}, {{0.995, 0.5}, 20}});
  REQUIRE(p.size() == 3);
  CHECK(p[1].world.x == doctest::Approx(1.005));
  CHECK(p[2].world.x == doctest::Approx(0.995));
}

TEST_CASE("trip path: segments join consecutive points with their dates") {
  const auto p = pb::buildTripPath({{{0, 0}, 0}, {{0.25, 0}, 10}, {{0.25, 0.25}, 20}});
  const auto segs = pb::tripSegments(p, [](pb::Vec2d w) { return w * 40.0; }, 0);
  REQUIRE(segs.size() == 2);
  CHECK(segs[0].a == pb::Vec2d{0, 0});
  CHECK(segs[0].b == pb::Vec2d{10, 0});
  CHECK(segs[0].dateA == 0);
  CHECK(segs[0].dateB == doctest::Approx(0.5));
  CHECK(segs[1].a == pb::Vec2d{10, 0});
  CHECK(segs[1].dateB == 1);
}

TEST_CASE("trip path: points closer than the minimum on screen are merged, ends kept") {
  const auto p = pb::buildTripPath({{{0, 0}, 0}, {{0.1, 0}, 1}, {{0.15, 0}, 2}, {{0.3, 0}, 3}, {{0.35, 0}, 4}});
  const auto segs = pb::tripSegments(p, [](pb::Vec2d w) { return w * 20.0; }, 2.5);
  // Screen x 0, 2, 3, 6, 7: 2 is too close to 0, 3 is far enough; 7 is the end, kept.
  REQUIRE(segs.size() == 3);
  CHECK(segs[0].a.x == 0);
  CHECK(segs[0].b.x == 3);
  CHECK(segs[1].b.x == 6);
  CHECK(segs[2].b.x == 7);
  CHECK(segs[2].dateB == 1);
}

TEST_CASE("trip path: colours run blue, yellow, red") {
  const auto blue = pb::tripColor(0), yellow = pb::tripColor(0.5), red = pb::tripColor(1);
  CHECK(blue[2] > blue[0]);
  CHECK(yellow[0] > 0.8f);
  CHECK(yellow[1] > 0.7f);
  CHECK(yellow[2] < 0.3f);
  CHECK(red[0] > red[1]);
  CHECK(red[0] > red[2]);
  CHECK(pb::tripColor(-1) == blue);
  CHECK(pb::tripColor(2) == red);
}
