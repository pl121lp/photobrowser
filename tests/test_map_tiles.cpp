#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "app/map_tiles.h"
#include "map/mercator.h"

using namespace std::chrono_literals;

namespace {

// Tiles of one colour; `missingLevel` tiles are missing; while the gate is closed every
// fetch blocks.
struct FakeSource final : pb::TileSource {
  int missingLevel = -1;
  mutable std::atomic<int> calls{0};
  mutable std::mutex m;
  mutable std::condition_variable cv;
  bool gateOpen = true;

  pb::TileResult get(int z, int, int) const override {
    {
      std::unique_lock lock(m);
      cv.wait(lock, [&] { return gateOpen; });
    }
    ++calls;
    if (z == missingLevel) return {pb::TileStatus::Missing, {}};
    pb::Image img{256, 256, {}};
    img.rgba.assign(256 * 256 * 4, 100);
    return {pb::TileStatus::Ok, std::move(img)};
  }
  void setGate(bool open) {
    {
      std::lock_guard lock(m);
      gateOpen = open;
    }
    cv.notify_all();
  }
  std::string attribution() const override { return "fake"; }
  int minZoom() const override { return 0; }
  int maxZoom() const override { return 19; }
};

struct FakeSink final : pb::MapTileSink {
  bool ready = true;
  int uploads = 0;
  bool mapPageReady() const override { return ready; }
  void uploadMapTile(pb::Vec2d origin, const std::vector<pb::Image>& mips) override {
    CHECK(origin.x >= 0);
    CHECK(origin.x < 4096);
    REQUIRE_FALSE(mips.empty());
    CHECK(mips[0].w == 256);
    ++uploads;
  }
};

pb::MapCamera oslo() {
  pb::MapCamera c;
  c.viewport = {800, 600};
  c.jumpTo(pb::toWorld(59.9139, 10.7522), 10);
  return c;
}

// Frames until nothing is pending or fading (or `limit` seconds pass).
void settle(pb::MapTiles& tiles, const pb::MapCamera& cam, double& now, std::vector<pb::MapTileDraw>& draws,
            double limit = 5) {
  const double end = now + limit;
  do {
    now += 1.0 / 60;
    draws.clear();
    tiles.frame(now, cam, draws);
    std::this_thread::sleep_for(2ms);
  } while ((tiles.pending() > 0 || tiles.fading(now)) && now < end);
}

}  // namespace

TEST_CASE("map tiles: requested tiles are fetched, uploaded and drawn") {
  FakeSource src;
  FakeSink sink;
  pb::MapTiles tiles(src, sink);
  const auto cam = oslo();
  std::vector<pb::MapTileDraw> draws;
  double now = 0;
  tiles.frame(now, cam, draws);
  CHECK(tiles.pending() > 0);
  settle(tiles, cam, now, draws);
  CHECK(tiles.pending() == 0);
  CHECK(sink.uploads > 0);
  CHECK(draws.size() == pb::tilesCovering(cam.visibleWorld(), 10).size());
  for (const auto& d : draws) CHECK(d.fade == 1);
  const auto results = tiles.takeResults();
  CHECK(int(results.size()) == sink.uploads);
  CHECK(std::all_of(results.begin(), results.end(), [](auto s) { return s == pb::TileStatus::Ok; }));
  CHECK(tiles.takeResults().empty());
}

TEST_CASE("map tiles: at most four uploads per frame") {
  FakeSource src;
  FakeSink sink;
  pb::MapTiles tiles(src, sink);
  const auto cam = oslo();
  std::vector<pb::MapTileDraw> draws;
  tiles.frame(0, cam, draws);
  for (int i = 0; i < 200 && src.calls < 10; ++i) std::this_thread::sleep_for(5ms);
  REQUIRE(src.calls >= 10);
  std::this_thread::sleep_for(20ms);
  tiles.frame(0.016, cam, draws);
  CHECK(sink.uploads == int(pb::MapTiles::kUploadsPerFrame));
}

TEST_CASE("map tiles: results wait until the page is ready") {
  FakeSource src;
  FakeSink sink;
  sink.ready = false;
  pb::MapTiles tiles(src, sink);
  const auto cam = oslo();
  std::vector<pb::MapTileDraw> draws;
  double now = 0;
  for (int i = 0; i < 30; ++i) {
    tiles.frame(now += 0.016, cam, draws);
    std::this_thread::sleep_for(5ms);
  }
  CHECK(sink.uploads == 0);
  CHECK(tiles.pending() > 0);
  sink.ready = true;
  settle(tiles, cam, now, draws);
  CHECK(sink.uploads > 0);
  CHECK(tiles.pending() == 0);
}

TEST_CASE("map tiles: failures are reported and drawn from the ancestor") {
  FakeSource src;
  src.missingLevel = 10;
  FakeSink sink;
  pb::MapTiles tiles(src, sink);
  const auto cam = oslo();
  std::vector<pb::MapTileDraw> draws;
  double now = 0;
  settle(tiles, cam, now, draws);
  const auto results = tiles.takeResults();
  CHECK(std::count(results.begin(), results.end(), pb::TileStatus::Missing) ==
        long(pb::tilesCovering(cam.visibleWorld(), 10).size()));
  CHECK(draws.size() == pb::tilesCovering(cam.visibleWorld(), 10).size());  // from levels 7 and 2
  const int calls = src.calls;
  settle(tiles, cam, now, draws, 1);
  CHECK(src.calls == calls);  // missing tiles are not asked for again
}

TEST_CASE("map tiles: cancelAll and destruction with fetches in flight") {
  FakeSource src;
  FakeSink sink;
  const auto cam = oslo();
  std::vector<pb::MapTileDraw> draws;
  {
    pb::MapTiles tiles(src, sink);
    src.setGate(false);
    tiles.frame(0, cam, draws);
    std::this_thread::sleep_for(30ms);  // both fetch threads are now blocked
    tiles.cancelAll();
    CHECK(tiles.pending() == 0);
    src.setGate(true);
    std::this_thread::sleep_for(50ms);  // the two blocked fetches finish
    CHECK(src.calls == 2);              // queued fetches were dropped, not run
    tiles.frame(0.1, cam, draws);
    CHECK(sink.uploads == 0);  // the two results were dropped
    CHECK(tiles.takeResults().empty());
  }
  {
    auto tiles = std::make_unique<pb::MapTiles>(src, sink);
    src.setGate(false);
    tiles->frame(0, cam, draws);
    std::this_thread::sleep_for(20ms);
    std::thread opener([&] {
      std::this_thread::sleep_for(50ms);
      src.setGate(true);
    });
    const auto t0 = std::chrono::steady_clock::now();
    tiles.reset();  // waits for the two running fetches only
    CHECK(std::chrono::steady_clock::now() - t0 >= 30ms);
    opener.join();
  }
}
