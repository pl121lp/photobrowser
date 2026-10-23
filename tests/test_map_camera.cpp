#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "map/map_camera.h"
#include "map/mercator.h"

namespace {

struct Fixture {
  pb::MapCamera cam;
  double t = 0;
  Fixture() { cam.viewport = {800, 600}; }
  void run(double seconds) {
    const double dt = 1.0 / 120;
    for (double e = 0; e < seconds; e += dt) {
      t += dt;
      cam.update(t, dt);
    }
  }
  void drag(pb::Vec2d from, pb::Vec2d to, double duration, int steps = 10) {
    cam.onPointerDown(from, t);
    for (int i = 1; i <= steps; ++i) {
      t += duration / steps;
      cam.onPointerMove(from + (to - from) * (double(i) / steps), t);
    }
    cam.onPointerUp(to, t);
  }
};

}  // namespace

TEST_CASE("map camera: world and screen round trip") {
  Fixture f;
  CHECK(f.cam.worldPx() == doctest::Approx(1024));
  CHECK(f.cam.worldToScreen(f.cam.center) == pb::Vec2d{400, 300});
  const pb::Vec2d w{0.43, 0.61};
  const pb::Vec2d back = f.cam.screenToWorld(f.cam.worldToScreen(w));
  CHECK(back.x == doctest::Approx(w.x));
  CHECK(back.y == doctest::Approx(w.y));
  f.cam.uiScale = 2;
  CHECK(f.cam.worldPx() == doctest::Approx(2048));
  const pb::Rect r = f.cam.visibleWorld();
  CHECK(r.width() == doctest::Approx(800.0 / 2048));
}

TEST_CASE("map camera: zoom about a pivot keeps the pivot's place") {
  Fixture f;
  f.cam.jumpTo(pb::toWorld(59.91, 10.75), 9);
  const pb::GeoPos before = pb::toLatLon(f.cam.screenToWorld({100, 100}));
  f.cam.zoomAbout({100, 100}, 5.3);
  CHECK(f.cam.zoom == doctest::Approx(5.3));
  const pb::GeoPos after = pb::toLatLon(f.cam.screenToWorld({100, 100}));
  CHECK(after.lat == doctest::Approx(before.lat).epsilon(1e-9));
  CHECK(after.lon == doctest::Approx(before.lon).epsilon(1e-9));
  f.cam.zoomAbout({100, 100}, 30);
  CHECK(f.cam.zoom == 19);
  f.cam.zoomAbout({100, 100}, 0);
  CHECK(f.cam.zoom == 2);
}

TEST_CASE("map camera: drag pans 1:1 and wraps x") {
  Fixture f;
  f.cam.jumpTo({0.5, 0.5}, 2);
  f.drag({300, 300}, {500, 300}, 1.0);  // slow: no fling
  CHECK(f.cam.center.x == doctest::Approx(0.5 - 200.0 / 1024));
  CHECK(f.cam.center.y == doctest::Approx(0.5));
  f.drag({100, 300}, {700, 300}, 1.0);  // past x = 0
  CHECK(f.cam.center.x >= 0);
  CHECK(f.cam.center.x < 1);
  CHECK(f.cam.center.x == doctest::Approx(0.5 - 800.0 / 1024 + 1));
  f.drag({400, 100}, {400, 590}, 1.0);
  f.drag({400, 100}, {400, 590}, 1.0);
  CHECK(f.cam.center.y == 0);
}

TEST_CASE("map camera: fling glides then stops") {
  Fixture f;
  f.cam.jumpTo({0.5, 0.5}, 10);
  f.drag({600, 300}, {300, 300}, 0.05, 5);
  const double released = f.cam.center.x;
  CHECK_FALSE(f.cam.idle());
  f.run(0.1);
  CHECK(f.cam.center.x > released);
  f.run(3);
  CHECK(f.cam.idle());
}

TEST_CASE("map camera: double-click zooms in one level about the cursor") {
  Fixture f;
  f.cam.jumpTo(pb::toWorld(40.7, -74), 10);
  const pb::Vec2d p{200, 150}, q = p + pb::Vec2d{1, 0};  // q: the second click, the zoom's pivot
  const pb::GeoPos before = pb::toLatLon(f.cam.screenToWorld(q));
  f.cam.onPointerDown(p, f.t);
  f.cam.onPointerUp(p, f.t + 0.05);
  f.t += 0.15;
  f.cam.onPointerDown(q, f.t);
  f.cam.onPointerUp(q, f.t + 0.05);
  f.t += 0.05;
  f.run(1.5);
  CHECK(f.cam.zoom == doctest::Approx(11).epsilon(1e-6));
  const pb::GeoPos after = pb::toLatLon(f.cam.screenToWorld(q));
  CHECK(after.lat == doctest::Approx(before.lat).epsilon(1e-7));
  CHECK(after.lon == doctest::Approx(before.lon).epsilon(1e-7));
  CHECK(f.cam.idle());
}

TEST_CASE("map camera: two slow clicks do not zoom") {
  Fixture f;
  f.cam.jumpTo({0.5, 0.5}, 10);
  f.cam.onPointerDown({200, 150}, f.t);
  f.cam.onPointerUp({200, 150}, f.t + 0.05);
  f.t += 0.6;
  f.cam.onPointerDown({200, 150}, f.t);
  f.cam.onPointerUp({200, 150}, f.t + 0.05);
  f.run(1);
  CHECK(f.cam.zoom == 10);
}

TEST_CASE("map camera: wheel zooms smoothly about the cursor") {
  Fixture f;
  f.cam.jumpTo({0.5, 0.5}, 10);
  const pb::Vec2d p{650, 420};
  const pb::Vec2d before = f.cam.screenToWorld(p);
  f.cam.onWheel(p, 1);
  CHECK(f.cam.zoom == 10);
  CHECK_FALSE(f.cam.idle());
  f.run(1);
  CHECK(f.cam.zoom == doctest::Approx(10.5).epsilon(1e-6));
  const pb::Vec2d after = f.cam.screenToWorld(p);
  CHECK(after.x == doctest::Approx(before.x).epsilon(1e-9));
  CHECK(after.y == doctest::Approx(before.y).epsilon(1e-9));
}

TEST_CASE("map camera: user moves are reported once") {
  Fixture f;
  CHECK_FALSE(f.cam.takeUserMoved());
  f.cam.onWheel({10, 10}, -1);
  CHECK(f.cam.takeUserMoved());
  CHECK_FALSE(f.cam.takeUserMoved());
  f.drag({300, 300}, {350, 300}, 0.5);
  CHECK(f.cam.takeUserMoved());
}

namespace {
// Runs until the flight ends (or `limit` seconds); returns the time it took.
double flyUntilDone(Fixture& f, double limit = 10) {
  const double t0 = f.t, dt = 1.0 / 120;
  while (f.cam.flying() && f.t - t0 < limit) {
    f.t += dt;
    f.cam.update(f.t, dt);
  }
  return f.t - t0;
}
}  // namespace

TEST_CASE("map camera: flyTo lands on the target and the duration is clamped") {
  Fixture f;
  f.cam.jumpTo(pb::toWorld(40.71, -74.0), 15);
  const pb::Vec2d sydney = pb::toWorld(-33.87, 151.21);
  f.cam.flyTo(sydney, 14, f.t);
  CHECK(f.cam.flying());
  CHECK_FALSE(f.cam.idle());
  const double took = flyUntilDone(f);
  CHECK(took <= 3.0 + 1.0 / 60);
  CHECK(took >= 0.6);
  CHECK_FALSE(f.cam.flying());
  CHECK(f.cam.center.x == doctest::Approx(sydney.x).epsilon(1e-12));
  CHECK(f.cam.center.y == doctest::Approx(sydney.y).epsilon(1e-12));
  CHECK(f.cam.zoom == doctest::Approx(14).epsilon(1e-12));

  const pb::Vec2d near = f.cam.screenToWorld({401, 300});
  f.cam.flyTo(near, 14, f.t);
  CHECK(flyUntilDone(f) >= 0.6 - 1e-9);
  CHECK(f.cam.center.x == doctest::Approx(near.x).epsilon(1e-12));
}

TEST_CASE("map camera: flyTo the current view is not a flight") {
  Fixture f;
  f.cam.jumpTo({0.4, 0.4}, 8);
  f.cam.flyTo({0.4, 0.4}, 8, f.t);
  CHECK_FALSE(f.cam.flying());
}

TEST_CASE("fly: takes the short way across the antimeridian") {
  Fixture f;
  f.cam.jumpTo(pb::toWorld(10, 179.9), 10);
  f.cam.flyTo(pb::toWorld(10, -179.9), 10, f.t);
  const double dt = 1.0 / 120;
  while (f.cam.flying()) {
    f.t += dt;
    f.cam.update(f.t, dt);
    const double lon = pb::toLatLon(f.cam.center).lon;
    CHECK_FALSE((lon > -170 && lon < 170));
  }
  CHECK(pb::toLatLon(f.cam.center).lon == doctest::Approx(-179.9));
}

TEST_CASE("map camera: a new flyTo mid-flight starts from the current state") {
  Fixture f;
  f.cam.jumpTo(pb::toWorld(59.9, 10.7), 12);
  f.cam.flyTo(pb::toWorld(48.85, 2.35), 12, f.t);
  f.run(0.8);
  const pb::Vec2d before = f.cam.center;
  const double zoomBefore = f.cam.zoom;
  f.cam.flyTo(pb::toWorld(41.9, 12.5), 12, f.t);
  f.t += 1.0 / 120;
  f.cam.update(f.t, 1.0 / 120);
  CHECK(length(f.cam.center - before) * f.cam.worldPx() < 10);
  CHECK(std::abs(f.cam.zoom - zoomBefore) < 0.05);
}

TEST_CASE("map camera: dragging stops a flight") {
  Fixture f;
  f.cam.jumpTo({0.3, 0.4}, 8);
  f.cam.flyTo({0.7, 0.4}, 8, f.t);
  f.run(0.3);
  f.drag({300, 300}, {320, 300}, 0.5);
  CHECK_FALSE(f.cam.flying());
}

TEST_CASE("map camera: fitView contains every point with padding and respects maxZoom") {
  const pb::Vec2d vp{800, 600};
  const std::vector<pb::Vec2d> city = {pb::toWorld(59.92, 10.70), pb::toWorld(59.90, 10.80), pb::toWorld(59.95, 10.75)};
  auto v = pb::MapCamera::fitView(city, vp, 1, 0.1, 16, 15, 2);
  REQUIRE(v);
  CHECK(v->zoom <= 16);
  pb::MapCamera cam;
  cam.viewport = vp;
  cam.jumpTo(v->center, v->zoom);
  for (const pb::Vec2d& p : city) {
    const pb::Vec2d s = cam.worldToScreen(p);
    CHECK(s.x >= 80 - 1e-6);
    CHECK(s.x <= 720 + 1e-6);
    CHECK(s.y >= 60 - 1e-6);
    CHECK(s.y <= 540 + 1e-6);
  }
  const std::vector<pb::Vec2d> close = {pb::toWorld(59.9, 10.7), pb::toWorld(59.90001, 10.70001)};
  CHECK(pb::MapCamera::fitView(close, vp, 1, 0.1, 16, 15, 2)->zoom == 16);
  const std::vector<pb::Vec2d> one = {pb::toWorld(59.9, 10.7)};
  auto single = pb::MapCamera::fitView(one, vp, 1, 0.1, 16, 15, 2);
  CHECK(single->zoom == 15);
  CHECK(single->center == one[0]);
  CHECK_FALSE(pb::MapCamera::fitView({}, vp, 1, 0.1, 16, 15, 2));
  const std::vector<pb::Vec2d> world = {pb::toWorld(60, -170), pb::toWorld(-40, 170)};
  CHECK(pb::MapCamera::fitView(world, vp, 1, 0.1, 16, 15, 2)->zoom == 2);
}
