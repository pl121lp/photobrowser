#include <doctest/doctest.h>

#include <set>
#include <vector>

#include "layout/square_grid.h"
#include "view/camera.h"
#include "view/interaction.h"
#include "view/screensaver.h"

namespace {

struct Fixture {
  pb::SquareGridLayout layout;
  pb::Camera cam;
  pb::Interaction ui;
  pb::Screensaver saver;
  double t = 0;
  explicit Fixture(uint32_t n = 100, double hold = 5) : ui(setup(n), layout), saver(hold, 1234) {}
  pb::Camera& setup(uint32_t n) {
    layout.build(n);
    cam.viewport = {1000, 800};
    return cam;
  }
  void run(double seconds) {
    const double dt = 1.0 / 60;
    for (double e = 0; e < seconds; e += dt) {
      t += dt;
      ui.update(t, dt);
      saver.update(ui, layout.count(), t);
    }
  }
  // Runs until a photo is fully open; returns it.
  uint32_t untilOpen() {
    for (int i = 0; i < 60 * 20; ++i) {
      run(1.0 / 60);
      if (ui.focused() && ui.focusProgress() >= 1 && !ui.flying()) return *ui.focused();
    }
    FAIL("no photo opened");
    return 0;
  }
};

}  // namespace

TEST_CASE("screensaver: off by default; start flies to a photo and opens it") {
  Fixture f;
  CHECK_FALSE(f.saver.active());
  f.run(1);
  CHECK(f.ui.idle());  // inactive: does nothing
  f.saver.start();
  CHECK(f.saver.active());
  const uint32_t p = f.untilOpen();
  const pb::Vec2d c = f.layout.place(p).cell.center();
  CHECK(f.cam.center.x == doctest::Approx(c.x));
  CHECK(f.cam.center.y == doctest::Approx(c.y));
}

TEST_CASE("screensaver: holds for the configured time, then travels to another photo") {
  Fixture f(100, 5);
  f.saver.start();
  const uint32_t first = f.untilOpen();
  f.run(4.5);
  REQUIRE(f.ui.focused());
  CHECK(*f.ui.focused() == first);
  CHECK(f.ui.focusProgress() == doctest::Approx(1));
  f.run(1.0);
  CHECK(f.ui.flying());  // left after 5 s
  const uint32_t second = f.untilOpen();
  CHECK(second != first);
}

TEST_CASE("screensaver: keeps going and visits different photos") {
  Fixture f(100, 1);
  f.saver.start();
  std::set<uint32_t> seen;
  for (int i = 0; i < 6; ++i) {
    seen.insert(f.untilOpen());
    f.run(1.1);
  }
  CHECK(seen.size() >= 4);
}

TEST_CASE("screensaver: never leaves the view idle between steps") {
  Fixture f(100, 2);
  f.saver.start();
  f.run(1.0 / 60);
  for (int i = 0; i < 60 * 8; ++i) {
    f.run(1.0 / 60);
    if (f.ui.focused() && f.ui.focusProgress() >= 1) return;
    REQUIRE_FALSE(f.ui.idle());  // flying or opening the photo
  }
  FAIL("no photo opened");
}

TEST_CASE("screensaver: stop halts the cycle and leaves the view where it is") {
  Fixture f(100, 1);
  f.saver.start();
  f.untilOpen();
  f.saver.stop();
  CHECK_FALSE(f.saver.active());
  const auto focused = f.ui.focused();
  f.run(3);
  CHECK(f.ui.focused() == focused);
  CHECK_FALSE(f.ui.flying());
}

TEST_CASE("screensaver: empty and single-photo libraries") {
  Fixture empty(0);
  empty.saver.start();
  empty.run(2);
  CHECK_FALSE(empty.ui.focused());

  Fixture one(1, 1);
  one.saver.start();
  CHECK(one.untilOpen() == 0);
  one.run(3);  // nothing else to visit: stays on it
  REQUIRE(one.ui.focused());
  CHECK(*one.ui.focused() == 0);
}

TEST_CASE("screensaver: slow zoom grows steadily over the hold, then eases back on leaving") {
  Fixture f(100, 4);
  f.saver.setSlowZoom(1.2);
  f.saver.start();
  f.untilOpen();
  CHECK(f.ui.focusZoom() == doctest::Approx(1).epsilon(0.01));
  CHECK(f.saver.animating());
  f.ui.setFocusRect({200, 100, 800, 700});  // the app reports the fitted rect each frame
  f.run(2);
  CHECK(f.ui.focusZoom() == doctest::Approx(std::sqrt(1.2)).epsilon(0.01));  // half way, in log scale
  const pb::Rect r = f.ui.focusDisplayRect();
  CHECK(r.center().x == doctest::Approx(500));  // zooms about the photo's centre
  CHECK(r.center().y == doctest::Approx(400));
  f.run(1.9);
  CHECK(f.ui.focusZoom() == doctest::Approx(1.2).epsilon(0.01));
  f.run(0.15);  // hold over: flies on
  REQUIRE(f.ui.flying());
  CHECK(f.ui.focusZoom() > 1.01);  // no jump back to fit
  f.run(0.5);
  CHECK(f.ui.focusZoom() == doctest::Approx(1));
}

TEST_CASE("screensaver: without slow zoom the photo stays fitted and frames aren't needed") {
  Fixture f(100, 2);
  f.saver.start();
  f.untilOpen();
  f.run(1.5);
  CHECK(f.ui.focusZoom() == doctest::Approx(1));
  CHECK_FALSE(f.saver.animating());
}

TEST_CASE("screensaver: reports each arrival once (to start live content such as a video)") {
  Fixture f(100, 2);
  f.saver.start();
  CHECK_FALSE(f.saver.takeArrived());
  f.untilOpen();
  f.run(1.0 / 60);  // the step after the photo is fully open
  CHECK(f.saver.takeArrived());
  CHECK_FALSE(f.saver.takeArrived());
  f.run(2.1);       // hold over: flies on
  CHECK_FALSE(f.saver.takeArrived());
  f.untilOpen();
  f.run(1.0 / 60);
  CHECK(f.saver.takeArrived());
}

TEST_CASE("screensaver: stays while the item is still playing, then moves on") {
  Fixture f(100, 2);
  f.saver.start();
  const uint32_t p = f.untilOpen();
  auto runBusy = [&](double seconds, bool busy) {
    for (double e = 0; e < seconds; e += 1.0 / 60) {
      f.t += 1.0 / 60;
      f.ui.update(f.t, 1.0 / 60);
      f.saver.update(f.ui, f.layout.count(), f.t, busy);
    }
  };
  runBusy(5, true);  // well past the 2 s hold
  REQUIRE(f.ui.focused());
  CHECK(*f.ui.focused() == p);
  CHECK_FALSE(f.ui.flying());
  runBusy(0.1, false);  // finished playing
  CHECK(f.ui.flying());
}

TEST_CASE("screensaver: sequential order starts at the first photo and steps through the timeline") {
  Fixture f(5, 1);
  f.saver.setOrder(pb::ScreensaverOrder::Sequential);
  f.saver.start();
  CHECK(f.untilOpen() == 0);
  std::vector<uint32_t> seen;
  for (int i = 0; i < 5; ++i) {
    f.run(1.1);
    seen.push_back(f.untilOpen());
  }
  CHECK(seen == std::vector<uint32_t>{1, 2, 3, 4, 0});  // wraps around at the end
}

TEST_CASE("screensaver: sequential order starts after the open photo") {
  Fixture f(10, 1);
  f.ui.openPhoto(6, f.t);
  f.run(1);
  f.saver.setOrder(pb::ScreensaverOrder::Sequential);
  f.saver.start();
  CHECK(f.untilOpen() == 7);
}

TEST_CASE("screensaver: random order is the default") {
  Fixture f(100, 1);
  f.saver.start();
  std::vector<uint32_t> seen{f.untilOpen()};
  for (int i = 0; i < 4; ++i) {
    f.run(1.1);
    seen.push_back(f.untilOpen());
  }
  bool consecutive = true;
  for (size_t i = 1; i < seen.size(); ++i) consecutive = consecutive && seen[i] == seen[i - 1] + 1;
  CHECK_FALSE(consecutive);
}

TEST_CASE("screensaver: heading is the photo it flies to, until that photo opens") {
  Fixture f(100, 1);
  CHECK_FALSE(f.saver.heading());
  f.saver.start();
  f.run(1.0 / 60);
  REQUIRE(f.saver.heading());
  const uint32_t target = *f.saver.heading();
  CHECK(f.ui.flying());
  CHECK_FALSE(f.ui.opened());
  while (f.saver.heading()) {
    CHECK(f.saver.heading() == target);
    CHECK_FALSE(f.ui.opened());
    f.run(1.0 / 60);
    REQUIRE(f.t < 20);
  }
  CHECK(f.ui.opened() == target);  // cleared in the frame the photo starts opening
  f.saver.stop();
  CHECK_FALSE(f.saver.heading());
}
