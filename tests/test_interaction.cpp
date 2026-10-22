#include <doctest/doctest.h>

#include <cmath>

#include "layout/square_grid.h"
#include "view/camera.h"
#include "view/interaction.h"

namespace {

struct Fixture {
  pb::SquareGridLayout layout;
  pb::Camera cam;
  pb::Interaction ui;
  double t = 0;
  explicit Fixture(uint32_t n = 20) : ui(setup(n), layout) {}
  pb::Camera& setup(uint32_t n) {
    layout.build(n);
    cam.viewport = {1000, 800};
    return cam;
  }
  void run(double seconds) {
    const double dt = 1.0 / 120;
    for (double e = 0; e < seconds; e += dt) {
      t += dt;
      ui.update(t, dt);
    }
  }
  pb::Vec2d screenOf(uint32_t pos) const { return cam.worldToScreen(layout.place(pos).cell.center()); }
  void click(pb::Vec2d p) {
    ui.onPointerDown(p, t);
    ui.onPointerUp(p + pb::Vec2d{2, 0}, t + 0.05);
    t += 0.05;
  }
};

}  // namespace

TEST_CASE("interaction: start view centers the center photo at 1/3 short side") {
  Fixture f;
  CHECK(f.cam.scale == doctest::Approx(800.0 / 3));
  CHECK(f.cam.center == f.layout.place(f.layout.centerPosition()).cell.center());
}

TEST_CASE("interaction: zoom limits") {
  Fixture f;
  CHECK(f.ui.maxScale() == doctest::Approx(800));
  CHECK(f.ui.minScale() == doctest::Approx(std::min(1000.0 / 5, 800.0 / 4) * 0.95));
}

TEST_CASE("interaction: small move is a click and centers that photo") {
  Fixture f;
  const uint32_t target = 6;  // cell (1,1)
  f.click(f.screenOf(target));
  f.run(0.5);
  CHECK(f.cam.center.x == doctest::Approx(1.5));
  CHECK(f.cam.center.y == doctest::Approx(1.5));
  CHECK(f.ui.idle());
}

TEST_CASE("interaction: drag pans 1:1 and is not a click") {
  Fixture f;
  const pb::Vec2d c0 = f.cam.center;
  const double s = f.cam.scale;
  f.ui.onPointerDown({500, 400}, 0);
  for (int i = 1; i <= 10; ++i) f.ui.onPointerMove({500.0 + i * 10, 400}, 0.5 + i * 0.01);
  CHECK(f.cam.center.x == doctest::Approx(c0.x - 100 / s));
  f.ui.onPointerUp({600, 400}, 2.0);  // released long after stopping: no fling
  f.t = 2.0;
  f.run(0.5);
  CHECK(f.cam.center.x == doctest::Approx(c0.x - 100 / s));
  CHECK(f.cam.center.y == doctest::Approx(c0.y));
}

TEST_CASE("interaction: fling starts momentum, grab stops it") {
  Fixture f;
  f.ui.onPointerDown({500, 400}, 0);
  for (int i = 1; i <= 10; ++i) f.ui.onPointerMove({500.0 + i * 10, 400}, i * 0.01);
  const double afterDrag = f.cam.center.x;
  f.ui.onPointerUp({600, 400}, 0.1);
  f.t = 0.1;
  f.run(0.1);
  CHECK(f.cam.center.x < afterDrag);  // still gliding left in world space
  CHECK_FALSE(f.ui.idle());
  f.ui.onPointerDown({500, 400}, f.t);
  const double grabbed = f.cam.center.x;
  f.run(0.2);
  CHECK(f.cam.center.x == grabbed);
}

TEST_CASE("interaction: wheel zoom keeps point under cursor and clamps to limits") {
  Fixture f;
  const pb::Vec2d cursor{700, 300};
  const pb::Vec2d w = f.cam.screenToWorld(cursor);
  f.ui.onWheel(cursor, 1);
  f.run(1.0);
  CHECK(f.cam.scale == doctest::Approx(800.0 / 3 * 1.25));
  CHECK(f.cam.screenToWorld(cursor).x == doctest::Approx(w.x));
  CHECK(f.cam.screenToWorld(cursor).y == doctest::Approx(w.y));
  f.ui.onWheel(cursor, -40);
  f.run(1.0);
  CHECK(f.cam.scale == doctest::Approx(f.ui.minScale()));
}

TEST_CASE("interaction: double-click focuses, escape unfocuses") {
  Fixture f;
  const auto p = f.screenOf(6);
  f.click(p);
  f.click(p);
  REQUIRE(f.ui.focused());
  CHECK(*f.ui.focused() == 6);
  f.run(0.4);
  CHECK(f.ui.focusProgress() == doctest::Approx(1));
  CHECK(f.ui.onKey(pb::Key::Escape, f.t));
  f.run(0.4);
  CHECK(f.ui.focusProgress() == doctest::Approx(0));
  CHECK_FALSE(f.ui.focused());
}

TEST_CASE("interaction: two slow clicks do not focus") {
  Fixture f;
  const auto p = f.screenOf(6);
  f.click(p);
  f.run(0.5);
  f.click(f.screenOf(6));
  CHECK_FALSE(f.ui.focused());
}

TEST_CASE("interaction: wheel past max zoom over a photo focuses it") {
  Fixture f;
  f.ui.onWheel({500, 400}, 20);
  f.run(1.0);
  CHECK(f.cam.scale == doctest::Approx(800));
  CHECK_FALSE(f.ui.focused());
  f.ui.onWheel({500, 400}, 1);
  REQUIRE(f.ui.focused());
  CHECK(*f.ui.focused() == f.layout.centerPosition());
  f.ui.onWheel({500, 400}, -1);  // wheel out leaves
  f.run(0.4);
  CHECK_FALSE(f.ui.focused());
}

TEST_CASE("interaction: enter focuses centered photo") {
  Fixture f;
  CHECK(f.ui.onKey(pb::Key::Enter, 0));
  REQUIRE(f.ui.focused());
  CHECK(*f.ui.focused() == f.layout.centerPosition());
}

TEST_CASE("interaction: escape when not focused is unhandled") {
  Fixture f;
  CHECK_FALSE(f.ui.onKey(pb::Key::Escape, 0));
}

TEST_CASE("interaction: focus left/right steps and clamps") {
  Fixture f;
  f.click(f.screenOf(1));
  f.click(f.screenOf(1));
  REQUIRE(f.ui.focused());
  f.ui.onKey(pb::Key::Left, f.t);
  CHECK(*f.ui.focused() == 0);
  f.ui.onKey(pb::Key::Left, f.t);
  CHECK(*f.ui.focused() == 0);
  f.ui.onKey(pb::Key::Right, f.t);
  f.ui.onKey(pb::Key::Right, f.t);
  CHECK(*f.ui.focused() == 2);
  f.run(0.5);
  CHECK(f.cam.center == f.layout.place(2).cell.center());
}

TEST_CASE("interaction: drag leaves focus") {
  Fixture f;
  f.ui.onKey(pb::Key::Enter, 0);
  f.run(0.4);
  f.ui.onPointerDown({500, 400}, f.t);
  f.ui.onPointerMove({560, 400}, f.t + 0.01);
  f.run(0.4);
  CHECK_FALSE(f.ui.focused());
}

TEST_CASE("interaction: arrow keys move one cell") {
  Fixture f;
  const pb::Vec2d c0 = f.cam.center;
  f.ui.onKey(pb::Key::Right, 0);
  f.run(0.5);
  CHECK(f.cam.center.x == doctest::Approx(c0.x + 1));
  f.ui.onKey(pb::Key::Down, f.t);
  f.run(0.5);
  CHECK(f.cam.center.y == doctest::Approx(c0.y + 1));
}

TEST_CASE("interaction: home returns to start view") {
  Fixture f;
  const pb::Vec2d c0 = f.cam.center;
  f.ui.onKey(pb::Key::Right, 0);
  f.ui.onWheel({100, 100}, 2);
  f.run(1.0);
  f.ui.onKey(pb::Key::Home, f.t);
  f.run(1.0);
  CHECK(f.cam.center.x == doctest::Approx(c0.x));
  CHECK(f.cam.scale == doctest::Approx(800.0 / 3));
}

TEST_CASE("interaction: viewport change keeps center") {
  Fixture f;
  f.click(f.screenOf(6));
  f.run(0.1);  // mid-tween
  f.ui.setViewport({1600, 900});
  const pb::Vec2d mid = f.cam.screenToWorld({800, 450});
  CHECK(mid == f.cam.center);
  f.run(0.5);
  CHECK(f.cam.center.x == doctest::Approx(1.5));
  f.ui.setViewport({300, 200});  // much smaller: max scale 200
  f.run(1.0);
  CHECK(f.cam.scale <= f.ui.maxScale() + 1e-9);
  CHECK(f.ui.maxScale() == doctest::Approx(200));
}

TEST_CASE("interaction: empty library does not crash") {
  Fixture f(0);
  f.click({500, 400});
  f.ui.onWheel({500, 400}, 3);
  f.ui.onKey(pb::Key::Enter, 0);
  f.ui.onKey(pb::Key::Right, 0);
  f.run(0.5);
  CHECK_FALSE(f.ui.focused());
}

TEST_CASE("interaction: click inside the whole photo stays, outside leaves") {
  Fixture f;
  f.ui.onKey(pb::Key::Enter, 0);
  f.run(0.4);
  f.ui.setFocusRect({200, 20, 800, 780});  // where the renderer draws the whole photo
  f.click({750, 100});                     // inside the photo, outside its grid cell
  f.run(0.4);
  CHECK(f.ui.focused());
  f.click({100, 400});
  f.run(0.4);
  CHECK_FALSE(f.ui.focused());
}
