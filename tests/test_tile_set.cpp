#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "map/map_camera.h"
#include "map/mercator.h"
#include "map/tile_set.h"

namespace {

pb::MapCamera camera(pb::Vec2d world, double zoom, pb::Vec2d viewport = {800, 600}) {
  pb::MapCamera c;
  c.viewport = viewport;
  c.maxZoom = 19;
  c.jumpTo(world, zoom);
  return c;
}

pb::UvRect compose(const pb::UvRect& o, const pb::UvRect& i) {
  const float w = o.u1 - o.u0, h = o.v1 - o.v0;
  return {o.u0 + w * i.u0, o.v0 + h * i.v0, o.u0 + w * i.u1, o.v0 + h * i.v1};
}

pb::UvRect slotUv(pb::SlotRef s) { return pb::SlotAllocator(256).uv(s, 256, 256); }

void checkUv(const pb::UvRect& a, const pb::UvRect& b) {
  CHECK(a.u0 == doctest::Approx(b.u0));
  CHECK(a.v0 == doctest::Approx(b.v0));
  CHECK(a.u1 == doctest::Approx(b.u1));
  CHECK(a.v1 == doctest::Approx(b.v1));
}

bool requested(const std::vector<pb::TileRequest>& rs, pb::TileKey k) {
  return std::any_of(rs.begin(), rs.end(), [&](const pb::TileRequest& r) { return r.key == k; });
}

// The draw covering the wanted tile `c` (same screen rect).
const pb::MapTileDraw* drawFor(const std::vector<pb::MapTileDraw>& ds, const pb::MapCamera& cam,
                               const pb::TileCover& c) {
  const pb::Vec2d a = cam.worldToScreen({c.world.x0, c.world.y0});
  for (const auto& d : ds) {
    if (std::abs(d.screen.x0 - a.x) < 1e-6 && std::abs(d.screen.y0 - a.y) < 1e-6) return &d;
  }
  return nullptr;
}

struct Fixture {
  pb::TileSet set{0, 19};
  std::vector<pb::MapTileDraw> draws;
  std::vector<pb::TileRequest> requests;
  void frame(const pb::MapCamera& cam, double now) {
    draws.clear();
    requests.clear();
    set.frame(cam, now, draws, requests);
  }
};

const pb::Vec2d kOslo = pb::toWorld(59.9139, 10.7522);

}  // namespace

TEST_CASE("tile set: wanted tiles for a view, coarse levels first") {
  Fixture f;
  const auto cam = camera(kOslo, 10);
  f.frame(cam, 0);
  for (const auto& c : pb::tilesCovering(cam.visibleWorld(), 10)) CHECK(requested(f.requests, c.key));
  CHECK(std::any_of(f.requests.begin(), f.requests.end(), [](const auto& r) { return r.key.z == 2; }));
  CHECK(std::any_of(f.requests.begin(), f.requests.end(), [](const auto& r) { return r.key.z == 7; }));
  REQUIRE(std::is_sorted(f.requests.begin(), f.requests.end(),
                         [](const auto& a, const auto& b) { return a.priority < b.priority; }));
  CHECK(f.requests.front().key.z == 2);
  bool seen10 = false;
  for (const auto& r : f.requests) {
    if (r.key.z == 10) seen10 = true;
    if (r.key.z == 7) CHECK_FALSE(seen10);
  }
  CHECK(f.set.pending() == f.requests.size());
  // Wanted again next frame: not requested twice.
  f.frame(cam, 0.016);
  CHECK(f.requests.empty());
  CHECK(f.draws.empty());  // nothing loaded yet
}

TEST_CASE("tile set: ancestor fallback picks the nearest loaded ancestor and its sub-rect") {
  Fixture f;
  const auto cam = camera(kOslo, 10);
  f.frame(cam, 0);
  const pb::TileCover c = pb::tilesCovering(cam.visibleWorld(), 10).front();
  const int x = c.key.x, y = c.key.y;
  auto s8 = f.set.loaded({8, x >> 2, y >> 2}, 0);
  REQUIRE(s8);
  f.frame(cam, 1);
  const pb::MapTileDraw* d = drawFor(f.draws, cam, c);
  REQUIRE(d);
  const float q0 = float(x % 4) / 4, r0 = float(y % 4) / 4;
  checkUv(d->uv, compose(slotUv(*s8), {q0, r0, q0 + 0.25f, r0 + 0.25f}));
  CHECK(d->fade == 1);

  auto s9 = f.set.loaded({9, x >> 1, y >> 1}, 1);
  REQUIRE(s9);
  f.frame(cam, 2);
  d = drawFor(f.draws, cam, c);
  REQUIRE(d);
  const float h0 = float(x % 2) / 2, v0 = float(y % 2) / 2;
  checkUv(d->uv, compose(slotUv(*s9), {h0, v0, h0 + 0.5f, v0 + 0.5f}));
}

TEST_CASE("tile set: loaded tiles cross-fade from the ancestor") {
  Fixture f;
  const auto cam = camera(kOslo, 10);
  f.frame(cam, 0);
  const pb::TileCover c = pb::tilesCovering(cam.visibleWorld(), 10).front();
  auto s8 = f.set.loaded({8, c.key.x >> 2, c.key.y >> 2}, 0);
  auto s10 = f.set.loaded(c.key, 1);
  REQUIRE(s10);
  f.frame(cam, 1);
  const pb::MapTileDraw* d = drawFor(f.draws, cam, c);
  REQUIRE(d);
  CHECK(d->fade == doctest::Approx(0));
  checkUv(d->uv, slotUv(*s10));
  const float q0 = float(c.key.x % 4) / 4, r0 = float(c.key.y % 4) / 4;
  checkUv(d->uvPrev, compose(slotUv(*s8), {q0, r0, q0 + 0.25f, r0 + 0.25f}));
  CHECK(f.set.fading(1.1));
  f.frame(cam, 1.2);
  d = drawFor(f.draws, cam, c);
  CHECK(d->fade == 1);
  CHECK_FALSE(f.set.fading(1.2));
}

TEST_CASE("tile set: missing is not retried, offline is retried after 5 s") {
  Fixture f;
  const auto cam = camera(kOslo, 10);
  f.frame(cam, 0);
  const auto covers = pb::tilesCovering(cam.visibleWorld(), 10);
  const pb::TileKey missing = covers[0].key, offline = covers[1].key;
  const size_t pending = f.set.pending();
  f.set.failed(missing, pb::TileStatus::Missing, 0.5);
  f.set.failed(offline, pb::TileStatus::Offline, 0.5);
  CHECK(f.set.pending() == pending - 2);
  f.frame(cam, 5.4);
  CHECK_FALSE(requested(f.requests, missing));
  CHECK_FALSE(requested(f.requests, offline));
  f.frame(cam, 5.6);
  CHECK_FALSE(requested(f.requests, missing));
  CHECK(requested(f.requests, offline));
  f.set.failed(offline, pb::TileStatus::Refused, 6);
  f.frame(cam, 30);
  CHECK_FALSE(requested(f.requests, offline));
  f.frame(cam, 66.1);
  CHECK(requested(f.requests, offline));
}

TEST_CASE("tile set: unwanted requests are cancelled") {
  Fixture f;
  f.frame(camera(kOslo, 10), 0);
  const auto first = f.requests;
  f.frame(camera(pb::toWorld(-33.87, 151.21), 10), 1);
  for (const auto& r : first) CHECK(r.cancel->cancelled);  // Sydney shares no tile with Oslo
  CHECK(f.set.pending() == f.requests.size());
}

TEST_CASE("tile set: LRU evicts least recently used first and keeps tiles in use") {
  Fixture f;
  auto view = [](int i) { return camera({0.05 + 0.0123 * i, 0.4}, 10, {200, 200}); };
  f.frame(view(0), 0);
  const auto first = pb::tilesCovering(view(0).visibleWorld(), 10).front().key;
  size_t loaded = 0;
  int i = 0;
  double now = 0;
  for (; loaded < 256; ++i) {
    f.frame(view(i), now);
    for (const auto& r : f.requests) {
      if (loaded < 256 && f.set.loaded(r.key, now)) ++loaded;
    }
    now += 0.01;
  }
  // Full. The last view is fully resident; loading a new tile evicts the oldest, not these.
  const pb::MapCamera last = view(i - 1);
  f.frame(last, now);
  const auto lastRequests = f.requests;
  for (const auto& r : lastRequests) f.set.loaded(r.key, now);
  f.frame(last, now + 0.01);
  REQUIRE(f.requests.empty());
  CHECK(f.set.loaded({10, 1000, 1000}, now + 0.02));
  f.frame(last, now + 0.03);
  CHECK(f.requests.empty());  // nothing of the current view was evicted
  f.frame(view(0), now + 0.04);
  CHECK(requested(f.requests, first));  // the least recently used was
}

TEST_CASE("tile set: tiles across the antimeridian use wrapped keys") {
  Fixture f;
  const auto cam = camera({0.0, 0.4}, 3);
  f.frame(cam, 0);
  const int y = int(0.4 * 8);
  REQUIRE(requested(f.requests, {3, 7, y}));
  REQUIRE(f.set.loaded({3, 7, y}, 0));
  f.frame(cam, 1);
  CHECK(std::any_of(f.draws.begin(), f.draws.end(), [](const auto& d) { return d.screen.x1 <= 400 + 1e-6; }));
}
