#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "app/library_index.h"
#include "app/streamer.h"
#include "layout/square_grid.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

struct FakeSink : pb::TextureSink {
  std::vector<std::pair<int, uint16_t>> pages;
  std::vector<int> uploads;  // level per upload
  std::chrono::milliseconds uploadCost{0};
  int fullUploads = 0;   // completed full images (last row of mip 0 seen)
  size_t fullBytes = 0;
  void addPage(int level, uint16_t page) override { pages.emplace_back(level, page); }
  void releasePage(int, uint16_t) override {}
  void upload(int level, uint16_t, pb::Vec2d, const std::vector<pb::Image>& mips) override {
    CHECK_FALSE(mips.empty());
    uploads.push_back(level);
    std::this_thread::sleep_for(uploadCost);
  }
  pb::Vec2d capacity{4096, 4096};
  uint32_t maxFullW = 0, maxFullH = 0;
  pb::Vec2d fullCapacity() const override { return capacity; }
  pb::UvRect beginFull(int, uint32_t w, uint32_t h) override {
    maxFullW = std::max(maxFullW, w);
    maxFullH = std::max(maxFullH, h);
    return {0, 0, float(w / capacity.x), float(h / capacity.y)};
  }
  void uploadFullRows(int, int mip, uint32_t y0, uint32_t rows, const pb::Image& m) override {
    fullBytes += size_t(rows) * m.w * 4;
    if (mip == 0 && y0 + rows == m.h) ++fullUploads;
  }
};

struct World {
  pbtest::TempDir photos, cache;
  pb::JobSystem jobs{4};
  pb::LibraryState lib;
  std::unique_ptr<pb::ThumbCache> thumbs;
  pb::SquareGridLayout layout;
  pb::Camera cam;
  FakeSink sink;
  std::unique_ptr<pb::Streamer> streamer;
  double t = 0;

  World() {
    for (auto n : {"PXL_20260321_003010085.jpg", "PXL_20260321_010024264.jpg"}) {
      fs::copy_file(pbtest::photo(n), photos.path / n);
    }
    thumbs = std::make_unique<pb::ThumbCache>(cache.path);
    pb::Indexer ix(photos.path, cache.path, jobs, *thumbs);
    ix.start();
    for (int i = 0; i < 2000 && !ix.finished(); ++i) std::this_thread::sleep_for(5ms);
    lib = ix.take();
    layout.build(uint32_t(lib.catalog.order.size()));  // 2 cols x 1 row
    cam.viewport = {1000, 800};
    cam.center = {1, 0.5};
    cam.scale = 200;  // tiles ~188 px -> L2
    streamer = std::make_unique<pb::Streamer>(lib.catalog, photos.path, *thumbs, jobs, sink, 512u << 20);
  }
  std::vector<pb::TileDraw> frame(std::optional<uint32_t> focus = std::nullopt) {
    std::vector<pb::TileDraw> out;
    t += 1.0 / 60;
    streamer->frame(t, cam, layout, focus, out);
    return out;
  }
  // Runs frames (letting workers progress) until pred(draws) or timeout.
  std::vector<pb::TileDraw> until(const std::function<bool(const std::vector<pb::TileDraw>&)>& pred,
                                  std::optional<uint32_t> focus = std::nullopt) {
    std::vector<pb::TileDraw> d;
    for (int i = 0; i < 600; ++i) {
      d = frame(focus);
      if (pred(d)) return d;
      std::this_thread::sleep_for(5ms);
    }
    FAIL("condition not reached");
    return d;
  }
};

}  // namespace

TEST_CASE("streamer: missing L0 draws placeholder") {
  World w;
  auto d = w.frame();
  REQUIRE(d.size() == 2);
  CHECK(d[0].cur.level == -1);
}

TEST_CASE("streamer: L0 drawn immediately after setL0") {
  World w;
  w.streamer->setL0(w.lib.l0);
  CHECK(w.sink.pages == std::vector<std::pair<int, uint16_t>>{{0, 0}});
  CHECK(std::count(w.sink.uploads.begin(), w.sink.uploads.end(), 0) == 2);
  auto d = w.frame();
  REQUIRE(d.size() == 2);
  CHECK(d[0].cur.level == 0);
  CHECK(d[0].pos == 0);
  CHECK(d[1].pos == 1);
  CHECK(d[0].cur.uv.u1 < 32.0f / 4096 + 1e-6f);
}

TEST_CASE("streamer: loads desired level and cross-fades") {
  World w;
  w.streamer->setL0(w.lib.l0);
  auto first = w.until([](auto& d) { return d[0].cur.level == 2; });
  CHECK(first[0].fade < 1.0f);
  CHECK(first[0].prev.level < 2);
  auto later = first;
  for (int i = 0; i < 12; ++i) later = w.frame();
  CHECK(later[0].cur.level == 2);
  CHECK(later[0].fade == 1.0f);
  CHECK(fs::exists(w.thumbs->levelPath(2, w.lib.catalog.photos[later[0].id].key)));
}

TEST_CASE("streamer: upload budget limits per-frame uploads") {
  World w;
  w.streamer->setL0(w.lib.l0);
  w.streamer->setUploadBudget(1);
  size_t before = w.sink.uploads.size();
  for (int i = 0; i < 200; ++i) {
    w.frame();
    CHECK(w.sink.uploads.size() - before <= 1);
    before = w.sink.uploads.size();
    std::this_thread::sleep_for(2ms);
  }
}

TEST_CASE("streamer: focused photo gets full resolution") {
  World w;
  w.streamer->setL0(w.lib.l0);
  auto d = w.until([](auto& dr) { return dr[1].cur.level == pb::kFullLevel; }, 1u);
  CHECK(w.sink.fullUploads >= 1);
  CHECK(d[1].cur.uv.u0 == 0);
  CHECK(d[1].cur.uv.u1 < 1);  // sub-rect of the pre-allocated full texture
}

TEST_CASE("streamer: full images upload a few rows per frame within the budget") {
  World w;
  w.streamer->setL0(w.lib.l0);
  w.streamer->setUploadBudget(1u << 20);
  size_t maxFrame = 0;
  w.until([&](auto&) {
    maxFrame = std::max(maxFrame, w.streamer->stats().uploadBytesThisFrame);
    return w.sink.fullUploads >= 1;
  }, 1u);
  CHECK(maxFrame <= (1u << 20) + 1024 * 4 * 4);  // budget plus at most one row of a large mip
}

TEST_CASE("streamer: whole-photo view prefetches neighbours at full resolution") {
  World w;
  w.streamer->setL0(w.lib.l0);
  w.until([&](auto&) { return w.sink.fullUploads >= 2; }, 0u);
  CHECK(w.sink.fullUploads >= 2);
}

TEST_CASE("streamer: full images fit the full-texture capacity") {
  World w;
  w.streamer->setL0(w.lib.l0);
  w.sink.capacity = {2000, 1600};
  w.cam.viewport = {2000, 1600};
  w.cam.scale = 1600;  // max grid zoom: the portrait crop wants ~2000 px of height
  w.cam.center = {0.5, 0.5};
  w.until([&](auto& d) { return !d.empty() && d[0].cur.level == pb::kFullLevel; });
  CHECK(w.sink.maxFullW <= 2000);
  CHECK(w.sink.maxFullH <= 1600);
  CHECK(w.sink.maxFullH >= 1500);
}

TEST_CASE("streamer: uploads stop when the frame's time budget is spent") {
  World w;
  w.streamer->setL0(w.lib.l0);
  w.sink.uploadCost = 5ms;
  w.streamer->setUploadTimeBudget(4.0);
  w.cam.scale = 60;  // ~56 px tiles: both photos want L1, which arrive together
  std::this_thread::sleep_for(1ms);
  w.frame();                            // requests both
  std::this_thread::sleep_for(300ms);   // both results are waiting now
  const size_t before = w.sink.uploads.size();
  w.frame();
  CHECK(w.sink.uploads.size() - before == 1);  // one upload used up the 4 ms budget
  w.frame();
  CHECK(w.sink.uploads.size() - before == 2);
}
