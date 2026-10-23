#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "map/map_view.h"
#include "map/mercator.h"

namespace {

struct FakeSource final : pb::TileSource {
  pb::TileResult get(int, int, int) const override { return {}; }
  std::string attribution() const override { return "fake"; }
  int minZoom() const override { return 0; }
  int maxZoom() const override { return 18; }
};

struct Fixture {
  FakeSource source;
  pb::MapView map;
  pb::Vec2d window{1000, 800};
  double t = 0;
  pb::MapView::Subject subject;
  size_t pending = 0;
  void step(double dt = 1.0 / 120) {
    t += dt;
    map.update(t, dt, window, 1, subject, pending);
  }
  void run(double seconds) {
    for (double e = 0; e < seconds; e += 1.0 / 120) step();
  }
  void open() {
    map.toggle(t, &source);
    run(0.5);
  }
};

}  // namespace

TEST_CASE("map view: M without a tile source posts a notice and stays closed") {
  Fixture f;
  f.map.toggle(f.t, nullptr);
  f.run(0.5);
  CHECK_FALSE(f.map.isOpen());
  CHECK_FALSE(f.map.shown());
  const auto n = f.map.takeNotice();
  REQUIRE(n);
  CHECK(*n == pb::MapNotice::NeedsPlugin);
  CHECK(pb::mapNoticeText(*n) == "Map needs a map tile plugin (start with --plugin; see README)");
  CHECK_FALSE(f.map.takeNotice());
  CHECK(f.map.contentRect(f.window) == pb::Rect{0, 0, 1000, 800});
}

TEST_CASE("map view: opening and closing animate the content and panel rects") {
  Fixture f;
  f.map.toggle(f.t, &f.source);
  CHECK(f.map.isOpen());
  CHECK(f.map.shown());
  f.step(0);
  CHECK(f.map.contentRect(f.window).x1 == 1000);
  f.step(0.175);
  const double mid = f.map.contentRect(f.window).x1;
  CHECK(mid > 600);
  CHECK(mid < 1000);
  CHECK(f.map.panelRect(f.window).x0 == mid);
  CHECK(f.map.panelRect(f.window).width() == 400);
  f.step(0.2);
  CHECK(f.map.contentRect(f.window) == pb::Rect{0, 0, 600, 800});
  CHECK(f.map.panelRect(f.window) == pb::Rect{600, 0, 1000, 800});
  CHECK(f.map.camera().viewport == pb::Vec2d{400, 800});
  CHECK(f.map.camera().maxZoom == 18);
  f.map.toggle(f.t, &f.source);
  CHECK_FALSE(f.map.isOpen());
  CHECK(f.map.shown());
  f.run(0.4);
  CHECK_FALSE(f.map.shown());
  CHECK(f.map.contentRect(f.window).x1 == 1000);
}

TEST_CASE("map view: escape closes the map first") {
  Fixture f;
  CHECK_FALSE(f.map.escape(f.t));
  f.open();
  CHECK(f.map.escape(f.t));
  CHECK_FALSE(f.map.isOpen());
  CHECK_FALSE(f.map.escape(f.t));
}

TEST_CASE("map view: pointer over the panel goes to the map, elsewhere not") {
  Fixture f;
  CHECK_FALSE(f.map.pointerDown({900, 400}, f.t));  // closed
  f.open();
  CHECK_FALSE(f.map.pointerDown({300, 400}, f.t));
  CHECK_FALSE(f.map.pointerMove({310, 400}, f.t));
  CHECK_FALSE(f.map.pointerUp({310, 400}, f.t));
  CHECK_FALSE(f.map.wheel({300, 400}, 1));
  const pb::Vec2d before = f.map.camera().center;
  CHECK(f.map.pointerDown({900, 400}, f.t));
  CHECK(f.map.pointerMove({850, 400}, f.t + 0.3));
  CHECK(f.map.pointerMove({500, 400}, f.t + 0.6));  // left the panel: still the map's drag
  CHECK(f.map.pointerUp({500, 400}, f.t + 1.5));
  CHECK(f.map.camera().center.x != before.x);
  CHECK(f.map.wheel({800, 100}, 1));
}

TEST_CASE("map view: resize keeps proportions and map centre") {
  Fixture f;
  f.open();
  const pb::Vec2d c = f.map.camera().center;
  f.window = {2000, 1200};
  f.step();
  CHECK(f.map.panelRect(f.window) == pb::Rect{1200, 0, 2000, 1200});
  CHECK(f.map.camera().viewport == pb::Vec2d{800, 1200});
  CHECK(f.map.camera().center == c);
}

TEST_CASE("map view: first batch all offline closes with a message; a cached tile keeps it open") {
  {
    Fixture f;
    f.map.toggle(f.t, &f.source);
    f.pending = 2;
    f.run(0.1);
    const std::vector<pb::TileStatus> offline = {pb::TileStatus::Offline};
    f.map.tileResults(offline);
    f.pending = 1;
    f.step();
    CHECK(f.map.isOpen());  // still waiting for the other one
    f.map.tileResults(offline);
    f.pending = 0;
    f.step();
    CHECK_FALSE(f.map.isOpen());
    const auto n = f.map.takeNotice();
    REQUIRE(n);
    CHECK(pb::mapNoticeText(*n) == "Map unavailable: no internet connection");
  }
  {
    Fixture f;
    f.map.toggle(f.t, &f.source);
    f.run(0.1);
    const std::vector<pb::TileStatus> mixed = {pb::TileStatus::Ok, pb::TileStatus::Offline};
    f.map.tileResults(mixed);
    f.step();
    CHECK(f.map.isOpen());
    CHECK(f.map.takeNotice() == pb::MapNotice::NoInternet);  // connection lost: once per session
    const std::vector<pb::TileStatus> offline = {pb::TileStatus::Offline};
    f.map.tileResults(offline);
    f.step();
    CHECK(f.map.isOpen());
    CHECK_FALSE(f.map.takeNotice());
  }
}

TEST_CASE("map view: refused posts its message once") {
  Fixture f;
  f.open();
  const std::vector<pb::TileStatus> refused = {pb::TileStatus::Ok, pb::TileStatus::Refused, pb::TileStatus::Refused};
  f.map.tileResults(refused);
  const auto n = f.map.takeNotice();
  REQUIRE(n);
  CHECK(pb::mapNoticeText(*n) == "Map tile server refused requests");
  CHECK_FALSE(f.map.takeNotice());
  const std::vector<pb::TileStatus> quiet = {pb::TileStatus::Missing, pb::TileStatus::Error};
  f.map.tileResults(quiet);
  CHECK_FALSE(f.map.takeNotice());
}
