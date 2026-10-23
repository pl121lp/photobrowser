#include <doctest/doctest.h>

#include <cmath>

#include "map/mercator.h"

namespace {

pb::TileKey tileOf(double lat, double lon, int z) {
  const pb::Vec2d w = pb::toWorld(lat, lon);
  const double n = std::exp2(z);
  return {z, int(std::floor(w.x * n)), int(std::floor(w.y * n))};
}

}  // namespace

TEST_CASE("mercator: known positions") {
  CHECK(pb::toWorld(0, 0) == pb::Vec2d{0.5, 0.5});
  CHECK(tileOf(59.9139, 10.7522, 10) == pb::TileKey{10, 542, 297});    // Oslo
  CHECK(tileOf(40.7128, -74.0060, 12) == pb::TileKey{12, 1205, 1540});  // New York
  CHECK(tileOf(-33.8688, 151.2093, 8) == pb::TileKey{8, 235, 153});     // Sydney
  for (auto [lat, lon] : {std::pair{59.9139, 10.7522}, {40.7128, -74.0060}, {-33.8688, 151.2093}}) {
    const pb::GeoPos p = pb::toLatLon(pb::toWorld(lat, lon));
    CHECK(p.lat == doctest::Approx(lat).epsilon(1e-12));
    CHECK(p.lon == doctest::Approx(lon).epsilon(1e-12));
  }
}

TEST_CASE("mercator: latitude clamps, non-finite positions are rejected") {
  const double top = pb::toWorld(pb::kMaxMercatorLat, 0).y;
  CHECK(top == doctest::Approx(0).epsilon(1e-9));
  CHECK(top >= 0);
  CHECK(pb::toWorld(89.9, 0).y == top);
  CHECK(pb::toWorld(90, 0).y == top);
  CHECK(pb::toWorld(-90, 0).y == doctest::Approx(1).epsilon(1e-9));
  CHECK_FALSE(pb::toWorld(pb::GeoPos{NAN, 3, {}}));
  CHECK_FALSE(pb::toWorld(pb::GeoPos{3, INFINITY, {}}));
  REQUIRE(pb::toWorld(pb::GeoPos{0, 0, {}}));
  CHECK(*pb::toWorld(pb::GeoPos{0, 0, {}}) == pb::Vec2d{0.5, 0.5});
}

TEST_CASE("mercator: tile x wraps") {
  CHECK(pb::wrapTileX(-1, 2) == 3);
  CHECK(pb::wrapTileX(4, 2) == 0);
  CHECK(pb::wrapTileX(5, 0) == 0);
  CHECK(pb::wrapTileX(-9, 3) == 7);
}

TEST_CASE("mercator: tiles covering a view") {
  auto one = pb::tilesCovering({0.25, 0.25, 0.5, 0.5}, 2);
  REQUIRE(one.size() == 1);
  CHECK(one[0].key == pb::TileKey{2, 1, 1});
  CHECK(one[0].world == pb::Rect{0.25, 0.25, 0.5, 0.5});
  CHECK(pb::tilesCovering({0.3, 0.3, 0.6, 0.6}, 2).size() == 4);
}

TEST_CASE("mercator: tiles covering a view across the antimeridian wrap") {
  auto covers = pb::tilesCovering({-0.1, 0.4, 0.1, 0.6}, 3);
  int west = 0, east = 0;
  for (const auto& c : covers) {
    if (c.world.x0 < 0) {
      CHECK(c.key.x == 7);
      ++west;
    } else {
      CHECK(c.key.x == 0);
      ++east;
    }
  }
  CHECK(west == 2);
  CHECK(east == 2);
  auto clipped = pb::tilesCovering({0.2, -0.5, 0.3, 0.1}, 1);
  REQUIRE(clipped.size() == 1);
  CHECK(clipped[0].key == pb::TileKey{1, 0, 0});
}

TEST_CASE("mercator: tile keys pack uniquely") {
  CHECK(pb::TileKey{3, 1, 2}.packed() != pb::TileKey{3, 2, 1}.packed());
  CHECK(pb::TileKey{3, 1, 2}.packed() != pb::TileKey{2, 1, 2}.packed());
}

TEST_CASE("mercator: tile level") {
  CHECK(pb::tileLevel(15, 1, 0, 19) == 15);
  CHECK(pb::tileLevel(15.5, 1, 0, 19) == 16);
  CHECK(pb::tileLevel(15.4, 1, 0, 19) == 15);
  CHECK(pb::tileLevel(15, 2, 0, 19) == 16);
  CHECK(pb::tileLevel(19, 2, 0, 19) == 19);
  CHECK(pb::tileLevel(1, 1, 2, 19) == 2);
}
