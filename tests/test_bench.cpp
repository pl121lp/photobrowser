#include <doctest/doctest.h>

#include "core/frame_stats.h"
#include "layout/square_grid.h"
#include "view/bench_script.h"
#include "view/camera.h"
#include "view/interaction.h"

TEST_CASE("frame stats: percentiles and missed frames") {
  pb::FrameStats s;
  for (int i = 1; i <= 100; ++i) s.add(double(i));
  CHECK(s.count() == 100);
  CHECK(s.percentile(50) == doctest::Approx(50).epsilon(0.02));
  CHECK(s.percentile(99) == doctest::Approx(99).epsilon(0.02));
  CHECK(s.max() == 100);
  CHECK(s.countAbove(90) == 10);
  s.reset();
  CHECK(s.count() == 0);
  CHECK(s.percentile(50) == 0);
}

TEST_CASE("bench script: drives a full tour and ends unfocused") {
  pb::SquareGridLayout layout;
  layout.build(10000);
  pb::Camera cam;
  cam.viewport = {1920, 1080};
  pb::Interaction ui(cam, layout);
  pb::BenchScript script;
  double minScale = cam.scale, maxScale = cam.scale;
  bool sawFocus = false;
  double t = 0;
  while (!script.done(t)) {
    script.drive(ui, cam.viewport, t);
    ui.update(t, 1.0 / 60);
    minScale = std::min(minScale, cam.scale);
    maxScale = std::max(maxScale, cam.scale);
    sawFocus |= ui.focused().has_value();
    t += 1.0 / 60;
  }
  CHECK(minScale == doctest::Approx(ui.minScale()).epsilon(0.05));  // zoomed out to the whole grid
  CHECK(maxScale > 1080.0 / 3 * 1.5);                                // zoomed into detail
  CHECK(sawFocus);
  CHECK(t < 15);
  for (int i = 0; i < 60; ++i, t += 1.0 / 60) ui.update(t, 1.0 / 60);
  CHECK_FALSE(ui.focused());
}
