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

namespace {
const pb::GeoPos kOslo{59.9139, 10.7522, {}};
const pb::GeoPos kBergen{60.3913, 5.3221, {}};
const pb::GeoPos kRome{41.9028, 12.4964, {}};

void checkCentre(const pb::MapView& m, const pb::GeoPos& p) {
  const pb::GeoPos c = pb::toLatLon(m.camera().center);
  CHECK(c.lat == doctest::Approx(p.lat).epsilon(1e-9));
  CHECK(c.lon == doctest::Approx(p.lon).epsilon(1e-9));
}
}  // namespace

TEST_CASE("map view: opening on a photo starts at zoom 4 and flies to photo zoom") {
  Fixture f;
  f.subject.photo = 7;
  f.subject.photoGps = kOslo;
  f.map.toggle(f.t, &f.source);
  f.step();
  CHECK(f.map.singleMode());
  CHECK(f.map.camera().zoom == doctest::Approx(4).epsilon(0.01));
  checkCentre(f.map, kOslo);
  CHECK(f.map.dots().size() == 1);
  CHECK(f.map.status().empty());
  f.run(3.5);
  CHECK(f.map.camera().zoom == doctest::Approx(15));
  checkCentre(f.map, kOslo);
  CHECK(f.map.pulse(f.t));
  CHECK(f.map.animating(f.t));
  f.run(2.1);
  CHECK_FALSE(f.map.pulse(f.t));
  CHECK_FALSE(f.map.animating(f.t));
}

TEST_CASE("map view: changing photo flies to the next location, even after a manual pan") {
  Fixture f;
  f.subject.photo = 1;
  f.subject.photoGps = kOslo;
  f.open();
  f.run(3.5);
  CHECK(f.map.pointerDown({900, 400}, f.t));
  f.map.pointerMove({800, 300}, f.t + 0.5);
  f.map.pointerUp({800, 300}, f.t + 1);
  f.run(1);
  f.subject.photo = 2;
  f.subject.photoGps = kBergen;
  f.step();
  CHECK(f.map.camera().flying());
  f.run(3.1);
  checkCentre(f.map, kBergen);
  CHECK(f.map.camera().zoom == doctest::Approx(15));
}

TEST_CASE("map view: photo without GPS keeps the view, dimmed with a message") {
  Fixture f;
  f.subject.photo = 1;
  f.subject.photoGps = kOslo;
  f.open();
  f.run(3.5);
  const pb::Vec2d c = f.map.camera().center;
  f.subject.photo = 2;
  f.subject.photoGps.reset();
  f.run(1);
  CHECK(f.map.camera().center == c);
  CHECK(f.map.status() == "No location for this item");
  CHECK(f.map.dimmed());
  CHECK(f.map.dots().empty());
  f.subject.photo = 3;
  f.subject.photoGps = kRome;
  f.run(3.2);
  checkCentre(f.map, kRome);
  CHECK_FALSE(f.map.dimmed());
}

TEST_CASE("map view: grid dots are the visible photos, deduplicated") {
  Fixture f;
  const std::vector<pb::GeoPos> gps = {kOslo, kBergen, kOslo};
  f.subject.visibleGps = gps;
  f.open();
  CHECK_FALSE(f.map.singleMode());
  CHECK(f.map.dots().size() == 2);
  // Opened onto the fit at once: both inside the panel.
  for (const pb::Vec2d& d : f.map.dots()) {
    const pb::Vec2d s = f.map.camera().worldToScreen(d);
    CHECK(s.x >= 0);
    CHECK(s.x <= 400);
  }
}

TEST_CASE("map view: auto-fit waits for 300 ms of grid rest") {
  Fixture f;
  const std::vector<pb::GeoPos> first = {kOslo, kBergen}, second = {kRome};
  f.subject.visibleGps = first;
  f.open();
  const pb::Vec2d c = f.map.camera().center;
  f.subject.visibleGps = second;
  f.subject.gridMoving = true;
  f.run(1);
  CHECK(f.map.dots().size() == 1);  // dots follow every frame
  CHECK(f.map.camera().center == c);
  f.subject.gridMoving = false;
  f.run(0.2);
  CHECK(f.map.camera().center == c);
  CHECK(f.map.animating(f.t));  // waiting to refit
  f.run(0.15);
  CHECK(f.map.camera().flying());
  f.run(3.1);
  checkCentre(f.map, kRome);
  CHECK(f.map.camera().zoom == doctest::Approx(15));  // one dot: photo zoom
}

TEST_CASE("map view: manual map move pauses auto-fit until the visible set changes") {
  Fixture f;
  const std::vector<pb::GeoPos> first = {kOslo, kBergen}, second = {kRome, kOslo};
  f.subject.visibleGps = first;
  f.open();
  f.map.wheel({800, 400}, 2);
  f.run(1);
  const pb::Vec2d moved = f.map.camera().center;
  f.subject.gridMoving = true;  // the grid moves but shows the same located photos
  f.run(0.2);
  f.subject.gridMoving = false;
  f.run(1);
  CHECK(f.map.camera().center == moved);
  f.subject.visibleGps = second;
  f.subject.gridMoving = true;
  f.run(0.2);
  f.subject.gridMoving = false;
  f.run(0.4);
  CHECK(f.map.camera().flying());
}

TEST_CASE("map view: no visible locations shows a message") {
  Fixture f;
  f.open();
  CHECK(f.map.status() == "No locations among the visible photos");
  CHECK(f.map.dimmed());
  CHECK(f.map.camera().zoom == 2);
  CHECK_FALSE(f.map.animating(f.t));
}

TEST_CASE("map view: switching photo to grid and back keeps the map open") {
  Fixture f;
  const std::vector<pb::GeoPos> grid = {kOslo, kBergen, kRome};
  f.subject.visibleGps = grid;
  f.open();
  CHECK(f.map.dots().size() == 3);
  f.subject.photo = 4;
  f.subject.photoGps = kBergen;
  f.step();
  CHECK(f.map.singleMode());
  CHECK(f.map.dots().size() == 1);
  CHECK(f.map.camera().flying());
  f.run(3.2);
  f.subject.photo.reset();
  f.subject.photoGps.reset();
  f.run(0.5);
  CHECK(f.map.isOpen());
  CHECK_FALSE(f.map.singleMode());
  CHECK(f.map.dots().size() == 3);
  CHECK(f.map.camera().flying());  // refits to the grid's photos
}

TEST_CASE("map view: non-finite photo positions are ignored") {
  Fixture f;
  f.subject.photo = 1;
  f.subject.photoGps = pb::GeoPos{NAN, 10, {}};
  f.open();
  CHECK(f.map.status() == "No location for this item");
  CHECK(std::isfinite(f.map.camera().center.x));
  CHECK(std::isfinite(f.map.camera().center.y));
  f.subject.photo.reset();
  f.subject.photoGps.reset();
  const std::vector<pb::GeoPos> gps = {kOslo, pb::GeoPos{INFINITY, 1, {}}};
  f.subject.visibleGps = gps;
  f.run(0.5);
  CHECK(f.map.dots().size() == 1);
  CHECK(std::isfinite(f.map.camera().center.x));
  CHECK(std::isfinite(f.map.camera().zoom));
}

TEST_CASE("map view: two offline results with nothing good close the map without waiting for the rest") {
  Fixture f;
  f.map.toggle(f.t, &f.source);
  f.pending = 20;  // a dead network that times out: the rest would take long
  f.run(0.1);
  const std::vector<pb::TileStatus> offline = {pb::TileStatus::Offline, pb::TileStatus::Offline};
  f.map.tileResults(offline);
  f.pending = 18;
  f.step();
  CHECK_FALSE(f.map.isOpen());
  CHECK(f.map.takeNotice() == pb::MapNotice::NoInternet);
}
