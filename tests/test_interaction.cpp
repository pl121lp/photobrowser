#include <doctest/doctest.h>

#include <cmath>

#include "layout/content.h"
#include "layout/square_grid.h"
#include "view/camera.h"
#include "view/interaction.h"

namespace {

struct Fixture {
  pb::SquareGridLayout layout;
  pb::Camera cam;
  pb::Interaction ui;
  double t = 0;
  explicit Fixture(uint32_t n = 20, double maxPhotoZoom = 4.0) : ui(setup(n), layout, maxPhotoZoom) {}
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

TEST_CASE("interaction: focus up/down jumps one row, clamping at the edges") {
  Fixture f;  // 20 photos: 5 columns, 4 rows
  f.ui.openPhoto(7, f.t);  // row 1, column 2
  f.run(0.5);
  f.ui.onKey(pb::Key::Down, f.t);
  CHECK(*f.ui.focused() == 12);
  f.ui.onKey(pb::Key::Up, f.t);
  f.ui.onKey(pb::Key::Up, f.t);
  CHECK(*f.ui.focused() == 2);
  f.ui.onKey(pb::Key::Up, f.t);  // top row: stays
  CHECK(*f.ui.focused() == 2);
  f.run(0.5);
  CHECK(f.cam.center == f.layout.place(2).cell.center());
  CHECK(f.ui.focusProgress() == doctest::Approx(1));  // still in whole-photo view
}

TEST_CASE("interaction: focus down into a shorter last row picks the closest photo") {
  Fixture f(23);  // 5 columns; the last row holds 20, 21, 22
  f.ui.openPhoto(19, f.t);  // row 3, last column
  f.run(0.5);
  f.ui.onKey(pb::Key::Down, f.t);
  CHECK(*f.ui.focused() == 22);
  f.ui.onKey(pb::Key::Down, f.t);  // bottom row: stays
  CHECK(*f.ui.focused() == 22);
  f.ui.onKey(pb::Key::Up, f.t);
  CHECK(*f.ui.focused() == 17);
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

TEST_CASE("interaction: wheel zooms a focused photo up to its configured limit") {
  Fixture f(20, 3.0);
  f.ui.onKey(pb::Key::Enter, 0);
  f.run(0.4);
  f.ui.setFocusRect({200, 20, 800, 780});
  f.ui.onWheel({500, 400}, 20);
  f.run(1.0);
  CHECK(f.ui.focused());
  CHECK(f.ui.focusZoom() == doctest::Approx(3.0));
  CHECK(f.ui.focusDisplayRect().width() == doctest::Approx(1800.0));
  CHECK(f.ui.focusDisplayRect().height() == doctest::Approx(2280.0));
}

TEST_CASE("interaction: focused photo zoom preserves the point under the cursor") {
  Fixture f;
  f.ui.onKey(pb::Key::Enter, 0);
  f.run(0.4);
  f.ui.setFocusRect({200, 20, 800, 780});
  f.ui.onWheel({500, 400}, 4);
  f.run(1.0);
  const pb::Vec2d cursor{650, 500};
  const pb::Rect before = f.ui.focusDisplayRect();
  const pb::Vec2d imagePoint{(cursor.x - before.x0) / before.width(), (cursor.y - before.y0) / before.height()};
  f.ui.onWheel(cursor, 1);
  f.run(1.0);
  const pb::Rect after = f.ui.focusDisplayRect();
  CHECK((cursor.x - after.x0) / after.width() == doctest::Approx(imagePoint.x));
  CHECK((cursor.y - after.y0) / after.height() == doctest::Approx(imagePoint.y));
}

TEST_CASE("interaction: focused zoom waits for the photo rect before anchoring") {
  Fixture f;
  f.ui.onKey(pb::Key::Enter, 0);
  f.ui.onWheel({700, 500}, 2);  // same event batch, before the focused photo has rendered
  f.run(0.2);
  CHECK(f.ui.focusZoom() == doctest::Approx(1.0));
  f.ui.setFocusRect({200, 20, 800, 780});
  f.run(0.5);
  CHECK(f.ui.focusZoom() > 1.0);
}

TEST_CASE("interaction: left drag pans a zoomed focused photo") {
  Fixture f;
  f.ui.onKey(pb::Key::Enter, 0);
  f.run(0.4);
  f.ui.setFocusRect({200, 20, 800, 780});
  f.ui.onWheel({500, 400}, 20);
  f.run(1.0);
  const pb::Vec2d before = f.ui.focusDisplayRect().center();
  f.ui.onPointerDown({500, 400}, f.t);
  f.ui.onPointerMove({600, 350}, f.t + 0.1);
  f.ui.onPointerUp({600, 350}, f.t + 0.2);
  CHECK(f.ui.focused());
  CHECK(f.ui.focusDisplayRect().center() == before + pb::Vec2d{100, -50});
}

TEST_CASE("interaction: zoomed photo pans when drag has no intermediate motion event") {
  Fixture f;
  f.ui.onKey(pb::Key::Enter, 0);
  f.run(0.4);
  f.ui.setFocusRect({200, 20, 800, 780});
  f.ui.onWheel({500, 400}, 20);
  f.run(1.0);
  const pb::Vec2d photoBefore = f.ui.focusDisplayRect().center();
  const pb::Vec2d cameraBefore = f.cam.center;
  f.ui.onPointerDown({500, 400}, f.t);
  f.ui.onPointerUp({600, 350}, f.t + 0.1);
  CHECK(f.ui.focused());
  CHECK(f.ui.focusDisplayRect().center() == photoBefore + pb::Vec2d{100, -50});
  CHECK(f.cam.center == cameraBefore);
}

TEST_CASE("interaction: wheel out returns to fit before leaving focus") {
  Fixture f;
  f.ui.onKey(pb::Key::Enter, 0);
  f.run(0.4);
  f.ui.setFocusRect({200, 20, 800, 780});
  f.ui.onWheel({500, 400}, 20);
  f.run(1.0);
  f.ui.onWheel({500, 400}, -20);
  f.run(1.0);
  CHECK(f.ui.focused());
  CHECK(f.ui.focusZoom() == doctest::Approx(1.0));
  CHECK(f.ui.focusDisplayRect() == pb::Rect{200, 20, 800, 780});
  f.ui.onWheel({500, 400}, -1);
  f.run(0.4);
  CHECK_FALSE(f.ui.focused());
}

TEST_CASE("interaction: secondary click resets focused photo zoom") {
  Fixture f;
  f.ui.onKey(pb::Key::Enter, 0);
  f.run(0.4);
  f.ui.setFocusRect({200, 20, 800, 780});
  f.ui.onWheel({500, 400}, 20);
  f.run(1.0);
  f.ui.onSecondaryClick();
  f.run(1.0);
  CHECK(f.ui.focused());
  CHECK(f.ui.focusZoom() == doctest::Approx(1.0));
  CHECK(f.ui.focusDisplayRect() == pb::Rect{200, 20, 800, 780});
}

TEST_CASE("interaction: camera stays within the library bounds") {
  Fixture f;
  // Hard fling to the left: content moves left, camera center moves right.
  f.ui.onPointerDown({900, 400}, 0);
  for (int i = 1; i <= 6; ++i) f.ui.onPointerMove({900.0 - i * 150, 400}, i * 0.016);
  f.ui.onPointerUp({0, 400}, 0.1);
  f.t = 0.1;
  f.run(3.0);
  CHECK(f.cam.center.x <= f.layout.bounds().x1 + 1e-9);
  // Dragging far past the edge is also held at the edge.
  f.ui.onPointerDown({500, 400}, f.t);
  f.ui.onPointerMove({500, 5000}, f.t + 0.5);
  CHECK(f.cam.center.y >= f.layout.bounds().y0 - 1e-9);
}

TEST_CASE("interaction: flyTo zooms out mid-flight and lands on the target at max grid zoom") {
  Fixture f(400);  // 20x20 grid
  const uint32_t target = 399;
  const pb::Vec2d dest = f.layout.place(target).cell.center();
  f.ui.flyTo(target, f.t);
  CHECK(f.ui.flying());
  const double startScale = f.cam.scale;
  double lowest = f.cam.scale;
  while (f.ui.flying()) {
    f.run(1.0 / 60);
    lowest = std::min(lowest, f.cam.scale);
    REQUIRE(f.t < 20);
  }
  CHECK(lowest < startScale * 0.5);  // pulled back for the journey
  CHECK(lowest >= f.ui.minScale() * 0.999);
  CHECK(f.cam.center.x == doctest::Approx(dest.x));
  CHECK(f.cam.center.y == doctest::Approx(dest.y));
  CHECK(f.cam.scale == doctest::Approx(f.ui.maxScale()));
  CHECK(f.ui.idle());
}

TEST_CASE("interaction: flyTo duration grows with distance") {
  Fixture near(400), far(400);
  const uint32_t from = near.layout.centerPosition();
  near.ui.flyTo(from + 1, near.t);
  far.ui.flyTo(0, far.t);
  while (near.ui.flying()) near.run(0.01);
  while (far.ui.flying()) far.run(0.01);
  CHECK(near.t >= 2);
  CHECK(far.t > near.t + 1);
  CHECK(far.t <= 6.1);
}

TEST_CASE("interaction: openPhoto and closePhoto drive the whole-photo view") {
  Fixture f;
  f.ui.openPhoto(7, f.t);
  f.run(0.5);
  REQUIRE(f.ui.focused());
  CHECK(*f.ui.focused() == 7);
  CHECK(f.ui.focusProgress() == doctest::Approx(1));
  f.ui.closePhoto();
  f.run(0.5);
  CHECK_FALSE(f.ui.focused());
}

TEST_CASE("interaction: click on the open photo activates it once; outside leaves") {
  Fixture f;
  const auto p = f.screenOf(6);
  f.click(p);
  f.click(p);
  f.run(0.4);
  CHECK_FALSE(f.ui.takeActivation());
  f.ui.setFocusRect({100, 100, 900, 700});
  f.run(0.5);  // past the double-click window
  f.click({500, 400});
  CHECK(f.ui.takeActivation());
  CHECK_FALSE(f.ui.takeActivation());
  REQUIRE(f.ui.opened());
  CHECK(*f.ui.opened() == 6);
  f.click({20, 20});
  CHECK_FALSE(f.ui.takeActivation());
  CHECK_FALSE(f.ui.opened());  // closing
  CHECK(f.ui.focused());
  f.run(0.4);
  CHECK_FALSE(f.ui.focused());
}

TEST_CASE("interaction: start position per start_at mode") {
  // 20 photos, centre position 10 (as the grid reports it).
  using pb::StartAt;
  CHECK(pb::startPosition(StartAt::Center, 20, 10, false, 0) == 10);
  CHECK(pb::startPosition(StartAt::Oldest, 20, 10, false, 0) == 0);
  CHECK(pb::startPosition(StartAt::Newest, 20, 10, false, 0) == 19);
  CHECK(pb::startPosition(StartAt::Oldest, 20, 10, true, 0) == 19);  // newest first: oldest is last
  CHECK(pb::startPosition(StartAt::Newest, 20, 10, true, 0) == 0);
  CHECK(pb::startPosition(StartAt::Random, 20, 10, false, 47) == 47 % 20);
  CHECK(pb::startPosition(StartAt::Random, 0, 0, false, 47) == 0);
  CHECK(pb::startPosition(StartAt::Newest, 0, 0, false, 0) == 0);
}

TEST_CASE("interaction: start view and Home go to the start position") {
  Fixture f;
  f.ui.setStartPosition(17);
  f.ui.resetView();
  CHECK(f.cam.center == f.layout.place(17).cell.center());
  f.click(f.screenOf(6));
  f.run(0.6);
  CHECK(f.ui.onKey(pb::Key::Home, f.t));
  f.run(1.0);
  CHECK(f.cam.center.x == doctest::Approx(f.layout.place(17).cell.center().x));
  CHECK(f.cam.center.y == doctest::Approx(f.layout.place(17).cell.center().y));
  f.ui.setStartPosition(99);  // beyond the library: falls back to the centre photo
  f.ui.resetView();
  CHECK(f.cam.center == f.layout.place(f.layout.centerPosition()).cell.center());
}

TEST_CASE("interaction: start zoom modes") {
  SUBCASE("focus: about three photos across the short side (default)") {
    Fixture f;
    CHECK(f.cam.scale == doctest::Approx(800.0 / 3));
  }
  SUBCASE("photo: the start photo opens in whole-photo view at once") {
    Fixture f;
    f.ui.setStartPosition(7);
    f.ui.setStartZoom(pb::StartZoom::Photo);
    f.ui.resetView();
    REQUIRE(f.ui.focused());
    CHECK(*f.ui.focused() == 7);
    CHECK(f.ui.focusProgress() == doctest::Approx(1));
    CHECK(f.cam.scale == doctest::Approx(800.0 / 3));  // Esc leaves to the usual grid view
    CHECK(f.cam.center == f.layout.place(7).cell.center());
    // Arrows step through photos; Home returns to the start photo, still whole.
    f.ui.onKey(pb::Key::Right, f.t);
    f.run(0.6);
    CHECK(*f.ui.focused() == 8);
    f.ui.onKey(pb::Key::Home, f.t);
    f.run(0.6);
    REQUIRE(f.ui.focused());
    CHECK(*f.ui.focused() == 7);
    CHECK(f.ui.focusProgress() == doctest::Approx(1));
    // From the grid, Home opens it again.
    f.ui.onKey(pb::Key::Escape, f.t);
    f.run(0.6);
    CHECK_FALSE(f.ui.focused());
    f.ui.onKey(pb::Key::Home, f.t);
    f.run(0.6);
    REQUIRE(f.ui.focused());
    CHECK(*f.ui.focused() == 7);
  }
  SUBCASE("full: the whole library, centred") {
    Fixture f;
    f.ui.setStartPosition(7);
    f.ui.setStartZoom(pb::StartZoom::Full);
    f.ui.resetView();
    CHECK(f.cam.scale == doctest::Approx(f.ui.minScale()));
    CHECK(f.cam.center == f.layout.bounds().center());
    // Home comes back to the same view.
    f.click(f.screenOf(0));
    f.ui.onWheel({500, 400}, 3);
    f.run(1.0);
    f.ui.onKey(pb::Key::Home, f.t);
    f.run(1.0);
    CHECK(f.cam.scale == doctest::Approx(f.ui.minScale()));
    CHECK(f.cam.center.x == doctest::Approx(f.layout.bounds().center().x));
    CHECK(f.cam.center.y == doctest::Approx(f.layout.bounds().center().y));
  }
}

TEST_CASE("interaction: full start view stays centred when the window grows (going fullscreen)") {
  Fixture f;
  f.ui.setStartZoom(pb::StartZoom::Full);
  f.ui.resetView();
  f.ui.setViewport({3840, 2160});  // the minimum zoom rises above the start zoom
  f.run(1.0);
  CHECK(f.cam.scale == doctest::Approx(f.ui.minScale()));
  CHECK(f.cam.center.x == doctest::Approx(f.layout.bounds().center().x));
  CHECK(f.cam.center.y == doctest::Approx(f.layout.bounds().center().y));
}

TEST_CASE("interaction: shift+arrow hops five photos, stopping at the edges") {
  SUBCASE("grid") {
    Fixture f;  // 20 photos: 5 columns x 4 rows
    f.cam.center = f.layout.place(0).cell.center();
    f.ui.onKey(pb::Key::Right, f.t, 5);
    f.run(1.0);
    CHECK(f.cam.center.x == doctest::Approx(f.layout.place(4).cell.center().x));  // only 4 more columns
    f.ui.onKey(pb::Key::Down, f.t, 5);
    f.run(1.0);
    CHECK(f.cam.center.y == doctest::Approx(f.layout.place(19).cell.center().y));  // bottom row
    f.ui.onKey(pb::Key::Left, f.t, 2);
    f.run(1.0);
    CHECK(f.cam.center.x == doctest::Approx(f.layout.place(2).cell.center().x));
  }
  SUBCASE("whole-photo view") {
    Fixture f;
    const auto p = f.screenOf(6);
    f.click(p);
    f.click(p);
    f.run(0.5);
    REQUIRE(f.ui.focused());
    f.ui.onKey(pb::Key::Right, f.t, 5);
    CHECK(*f.ui.focused() == 11);
    f.ui.onKey(pb::Key::Left, f.t, 5);
    f.ui.onKey(pb::Key::Left, f.t, 5);
    CHECK(*f.ui.focused() == 1);
    f.ui.onKey(pb::Key::Left, f.t, 5);
    CHECK(*f.ui.focused() == 0);
    f.ui.onKey(pb::Key::Down, f.t, 5);
    CHECK(*f.ui.focused() == 15);  // last row (row 3), same column
    f.ui.onKey(pb::Key::Up, f.t, 2);
    CHECK(*f.ui.focused() == 5);
  }
}

TEST_CASE("interaction: photo effect applies only in whole-photo view and resets on leaving it") {
  Fixture f;
  CHECK(f.ui.photoEffect() == 0);
  CHECK_FALSE(f.ui.setPhotoEffect(2));  // grid view: ignored
  CHECK(f.ui.photoEffect() == 0);
  f.ui.onKey(pb::Key::Enter, f.t);
  f.run(0.5);
  CHECK(f.ui.setPhotoEffect(2));
  CHECK(f.ui.photoEffect() == 2);
  f.ui.onKey(pb::Key::Right, f.t);  // stepping to the next photo keeps it
  CHECK(f.ui.photoEffect() == 2);
  CHECK(f.ui.setPhotoEffect(0));
  CHECK(f.ui.photoEffect() == 0);
  f.ui.setPhotoEffect(3);
  f.ui.onKey(pb::Key::Escape, f.t);
  CHECK(f.ui.photoEffect() == 0);
  f.run(0.5);
  f.ui.onKey(pb::Key::Enter, f.t);  // the next photo opens without an effect
  CHECK(f.ui.photoEffect() == 0);
}

TEST_CASE("interaction: narrowing viewport keeps the centre photo centred and the open photo fitted") {
  Fixture f;
  const uint32_t centre = f.layout.centerPosition();
  for (int i = 0; i <= 20; ++i) {
    f.ui.setViewport({1000 - 20.0 * i, 800});
    f.run(1.0 / 120);
    CHECK(f.screenOf(centre).x == doctest::Approx(f.cam.viewport.x / 2).epsilon(0.0005));
  }
  f.ui.onKey(pb::Key::Enter, f.t);
  f.run(0.5);
  for (int i = 0; i <= 20; ++i) {
    const pb::Vec2d vp{600 + 20.0 * i, 800};
    f.ui.setViewport(vp);
    f.ui.setFocusRect(pb::fitRect({0, 0, vp.x, vp.y}, 1.5));
    f.run(1.0 / 120);
    const pb::Rect r = f.ui.focusDisplayRect();
    CHECK(r.x0 >= -1e-9);
    CHECK(r.x1 <= vp.x + 1e-9);
    CHECK(r.y0 >= -1e-9);
    CHECK(r.y1 <= vp.y + 1e-9);
  }
}

TEST_CASE("interaction: flightEnd tells when a flyTo lands") {
  Fixture f(400);
  CHECK_FALSE(f.ui.flightEnd());
  f.ui.flyTo(0, f.t);
  REQUIRE(f.ui.flightEnd());
  const double end = *f.ui.flightEnd();
  CHECK(end > f.t + 2 - 1e-9);
  while (f.ui.flying()) f.run(0.01);
  CHECK(f.t >= end);
  CHECK(f.t < end + 0.02);
  CHECK_FALSE(f.ui.flightEnd());
}
