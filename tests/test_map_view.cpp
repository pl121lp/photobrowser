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
  explicit Fixture(pb::MapSettings s = {}) : map(s) {}
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

TEST_CASE("map view: a frozen subject keeps the dots and the view until the next photo opens") {
  Fixture f;
  f.subject.photo = 1;
  f.subject.photoGps = kOslo;
  f.open();
  f.run(5.5);  // arrived, pulse over
  const pb::Vec2d c = f.map.camera().center;
  // The screensaver closes the photo and flies through the grid: the map holds still.
  const std::vector<pb::GeoPos> passing = {kRome, kBergen};
  f.subject = {};
  f.subject.visibleGps = passing;
  f.subject.gridMoving = true;
  f.subject.frozen = true;
  f.run(1);
  f.subject.gridMoving = false;
  f.run(1);
  CHECK(f.map.singleMode());
  CHECK(f.map.dots().size() == 1);
  CHECK(f.map.camera().center == c);
  CHECK_FALSE(f.map.camera().flying());
  CHECK_FALSE(f.map.animating(f.t));
  // Arrived at the next photo: the map flies there as usual.
  f.subject = {};
  f.subject.photo = 2;
  f.subject.photoGps = kRome;
  f.step();
  CHECK(f.map.camera().flying());
  f.run(3.1);
  checkCentre(f.map, kRome);
}

namespace {
// Grid fixture with photos at layout positions 10 (Oslo), 11 (Bergen), 12 (Oslo again).
struct GridFixture : Fixture {
  std::vector<pb::GeoPos> gps = {kOslo, kBergen, kOslo};
  std::vector<uint32_t> pos = {10, 11, 12};
  GridFixture() {
    subject.visibleGps = gps;
    subject.visiblePos = pos;
    open();
  }
  size_t dotOf(const pb::GeoPos& g) const {
    const pb::Vec2d w = *pb::toWorld(g);
    for (size_t i = 0; i < map.dots().size(); ++i) {
      if (length(map.dots()[i] - w) < 1e-12) return i;
    }
    FAIL("no dot at that position");
    return 0;
  }
};
}  // namespace

TEST_CASE("map view: dots remember their photos") {
  GridFixture f;
  REQUIRE(f.map.dots().size() == 2);
  CHECK(f.map.dotPhotos(f.dotOf(kOslo)) == std::vector<uint32_t>{10, 12});
  CHECK(f.map.dotPhotos(f.dotOf(kBergen)) == std::vector<uint32_t>{11});
}

TEST_CASE("map view: hovering a dot highlights its photos") {
  GridFixture f;
  const pb::Vec2d at = f.map.dotScreen(f.dotOf(kOslo));
  CHECK(f.map.panelRect(f.window).contains(at));
  CHECK_FALSE(f.map.pointerMove(at + pb::Vec2d{3, 2}, f.t));  // hover only: the content still gets it
  f.step();
  CHECK(f.map.highlightedDot() == f.dotOf(kOslo));
  CHECK(f.map.hoveredPhotos() == std::vector<uint32_t>{10, 12});
  f.map.pointerMove(at + pb::Vec2d{60, 60}, f.t);
  f.step();
  CHECK(f.map.hoveredPhotos().empty());
  CHECK_FALSE(f.map.highlightedDot());
  f.map.pointerMove(at, f.t);
  f.step();
  f.map.pointerLeave();
  f.step();
  CHECK(f.map.hoveredPhotos().empty());
}

TEST_CASE("map view: clicking a dot asks for its photos; dragging does not") {
  GridFixture f;
  const pb::Vec2d at = f.map.dotScreen(f.dotOf(kBergen));
  CHECK(f.map.pointerDown(at, f.t));
  CHECK(f.map.pointerUp(at + pb::Vec2d{1, 0}, f.t + 0.1));
  CHECK(f.map.takeClickedPhotos() == std::vector<uint32_t>{11});
  CHECK(f.map.takeClickedPhotos().empty());
  f.t += 1;
  CHECK(f.map.pointerDown(at, f.t));
  f.map.pointerMove(at + pb::Vec2d{40, 0}, f.t + 0.2);
  f.map.pointerUp(at + pb::Vec2d{40, 0}, f.t + 0.4);
  CHECK(f.map.takeClickedPhotos().empty());
}

TEST_CASE("map view: hovering a photo highlights its dot") {
  GridFixture f;
  f.subject.hoveredPhoto = 12;
  f.step();
  CHECK(f.map.highlightedDot() == f.dotOf(kOslo));
  CHECK(f.map.hoveredPhotos().empty());  // only hovering a dot highlights photos
  f.subject.hoveredPhoto = 99;  // no location
  f.step();
  CHECK_FALSE(f.map.highlightedDot());
}

TEST_CASE("map view: no dot hover or click while a photo is open") {
  GridFixture f;
  const pb::Vec2d at = f.map.dotScreen(f.dotOf(kOslo));
  f.subject.photo = 10;
  f.subject.photoGps = kOslo;
  f.run(4);
  f.map.pointerMove(f.map.dotScreen(0), f.t);
  f.step();
  CHECK(f.map.hoveredPhotos().empty());
  CHECK_FALSE(f.map.highlightedDot());
  f.map.pointerDown(f.map.dotScreen(0), f.t);
  f.map.pointerUp(f.map.dotScreen(0), f.t + 0.1);
  CHECK(f.map.takeClickedPhotos().empty());
  (void)at;
}

TEST_CASE("map view: trip path is off by default") {
  Fixture f;
  const std::vector<pb::GeoPos> gps = {kOslo, kBergen};
  const std::vector<int64_t> taken = {0, 100};
  f.subject.visibleGps = gps;
  f.subject.visibleTakenAt = taken;
  f.open();
  CHECK(f.map.tripSegments().empty());
}

TEST_CASE("map view: trip path joins the visible photos in time order, grid only") {
  pb::MapSettings s;
  s.tripPath = true;
  Fixture f(s);
  const std::vector<pb::GeoPos> gps = {kBergen, kOslo, kRome};
  const std::vector<int64_t> taken = {100, 0, 200};
  f.subject.visibleGps = gps;
  f.subject.visibleTakenAt = taken;
  f.open();
  const auto segs = f.map.tripSegments();
  REQUIRE(segs.size() == 2);
  // In window px, from Oslo (earliest) via Bergen to Rome.
  auto screen = [&](const pb::GeoPos& g) {
    return f.map.camera().worldToScreen(*pb::toWorld(g)) + pb::Vec2d{f.map.panelRect(f.window).x0, 0};
  };
  CHECK(length(segs[0].a - screen(kOslo)) < 1e-6);
  CHECK(length(segs[0].b - screen(kBergen)) < 1e-6);
  CHECK(length(segs[1].b - screen(kRome)) < 1e-6);
  CHECK(segs[0].dateA == 0);
  CHECK(segs[1].dateB == 1);

  f.subject.photo = 0;
  f.subject.photoGps = kOslo;
  f.step();
  CHECK(f.map.tripSegments().empty());
}

TEST_CASE("map view: trip path breaks after the configured gap") {
  pb::MapSettings s;
  s.tripPath = true;
  s.tripGapS = 50;
  Fixture f(s);
  const std::vector<pb::GeoPos> gps = {kOslo, kBergen, kRome};
  const std::vector<int64_t> taken = {0, 40, 200};
  f.subject.visibleGps = gps;
  f.subject.visibleTakenAt = taken;
  f.open();
  CHECK(f.map.tripSegments().size() == 1);
}

TEST_CASE("map view: trip path needs the photos' times") {
  pb::MapSettings s;
  s.tripPath = true;
  Fixture f(s);
  const std::vector<pb::GeoPos> gps = {kOslo, kBergen};
  f.subject.visibleGps = gps;
  f.open();
  CHECK(f.map.tripSegments().empty());
}

namespace {
struct TrailFixture : Fixture {
  TrailFixture() : Fixture([] {
    pb::MapSettings s;
    s.tripPath = true;
    return s;
  }()) {}
  void show(uint32_t photo, std::optional<pb::GeoPos> gps) {
    subject.photo = photo;
    subject.photoGps = gps;
    step();
  }
  pb::Vec2d screen(const pb::GeoPos& g) const {
    pb::Vec2d w = *pb::toWorld(g);
    w.x -= std::round(w.x - map.camera().center.x);
    return map.camera().worldToScreen(w) + pb::Vec2d{map.panelRect(window).x0, 0};
  }
};
}  // namespace

TEST_CASE("map view: stepping through photos leaves a gray trail, with trip path on") {
  TrailFixture f;
  f.subject.photo = 1;
  f.subject.photoGps = kOslo;
  f.open();
  CHECK(f.map.trailSegments().empty());
  CHECK(f.map.trailDots().empty());
  f.show(2, kBergen);
  f.show(3, kRome);
  CHECK(f.map.dots().size() == 1);  // the current photo, as before
  const auto segs = f.map.trailSegments();
  REQUIRE(segs.size() == 2);
  CHECK(length(segs[0].a - f.screen(kOslo)) < 1e-6);
  CHECK(length(segs[0].b - f.screen(kBergen)) < 1e-6);
  CHECK(length(segs[1].b - f.screen(kRome)) < 1e-6);
  const auto dots = f.map.trailDots();  // the photos before, not the current one
  REQUIRE(dots.size() == 2);
  CHECK(length(dots[0] - f.screen(kOslo)) < 1e-6);
  CHECK(length(dots[1] - f.screen(kBergen)) < 1e-6);
  CHECK(f.map.tripSegments().empty());  // the grid's trip path is not shown
}

TEST_CASE("map view: no trail with trip path off") {
  Fixture f;
  f.subject.photo = 1;
  f.subject.photoGps = kOslo;
  f.open();
  f.subject.photo = 2;
  f.subject.photoGps = kBergen;
  f.step();
  CHECK(f.map.trailSegments().empty());
  CHECK(f.map.trailDots().empty());
}

TEST_CASE("map view: the trail skips photos without GPS and repeats of one spot") {
  TrailFixture f;
  f.subject.photo = 1;
  f.subject.photoGps = kOslo;
  f.open();
  f.show(2, std::nullopt);
  CHECK(f.map.trailDots().size() == 1);  // Oslo is no longer the current photo
  CHECK(f.map.trailSegments().empty());
  f.show(3, kBergen);
  f.show(4, kBergen);
  REQUIRE(f.map.trailSegments().size() == 1);
  CHECK(length(f.map.trailSegments()[0].a - f.screen(kOslo)) < 1e-6);
  CHECK(f.map.trailDots().size() == 1);
}

TEST_CASE("map view: back in the grid the trail is cleared; the next photo starts a new one") {
  TrailFixture f;
  f.subject.photo = 1;
  f.subject.photoGps = kOslo;
  f.open();
  f.show(2, kBergen);
  REQUIRE(f.map.trailSegments().size() == 1);
  f.show(0, std::nullopt);
  f.subject.photo.reset();
  f.step();
  CHECK(f.map.trailSegments().empty());
  CHECK(f.map.trailDots().empty());
  f.show(5, kRome);
  CHECK(f.map.trailSegments().empty());
  f.show(6, kOslo);
  CHECK(f.map.trailSegments().size() == 1);
}

TEST_CASE("map view: the trail keeps the last 1000 photos") {
  TrailFixture f;
  f.subject.photo = 0;
  f.subject.photoGps = pb::GeoPos{0, 0, {}};
  f.open();
  for (uint32_t i = 1; i < 1200; ++i) f.show(i, pb::GeoPos{0, i * 0.001, {}});
  CHECK(f.map.trailSegments().size() == pb::MapView::kTrailMax - 1);
  CHECK(f.map.trailDots().size() == pb::MapView::kTrailMax - 1);
}
