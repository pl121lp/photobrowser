#include <doctest/doctest.h>

#include "view/camera.h"

TEST_CASE("camera: round trip and pivot zoom invariance") {
  pb::Camera c;
  c.center = {10, 20};
  c.scale = 50;
  c.viewport = {800, 600};
  CHECK(c.worldToScreen({10, 20}) == pb::Vec2d{400, 300});
  auto w = c.screenToWorld({123, 456});
  CHECK(c.worldToScreen(w).x == doctest::Approx(123));
  CHECK(c.worldToScreen(w).y == doctest::Approx(456));
  c.zoomAbout({123, 456}, 200);
  CHECK(c.scale == 200);
  CHECK(c.screenToWorld({123, 456}).x == doctest::Approx(w.x));
  CHECK(c.screenToWorld({123, 456}).y == doctest::Approx(w.y));
}

TEST_CASE("camera: visible world rect") {
  pb::Camera c;
  c.center = {5, 5};
  c.scale = 100;
  c.viewport = {800, 600};
  auto r = c.visibleWorld();
  CHECK(r.x0 == doctest::Approx(1));
  CHECK(r.x1 == doctest::Approx(9));
  CHECK(r.y0 == doctest::Approx(2));
  CHECK(r.y1 == doctest::Approx(8));
}
