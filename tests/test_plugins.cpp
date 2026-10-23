#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "app/library_index.h"
#include "library/catalog.h"
#include "library/indexer.h"
#include "library/media_format.h"
#include "library/scanner.h"
#include "platform/dynlib.h"
#include "plugin/plugin_host.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

// A format whose files are 30x10 stills (or broken if the file starts with 'x').
struct FakeHandler final : pb::MediaHandler {
  std::optional<pb::MediaProbe> probe(const fs::path& abs) const override {
    if (pbtest::readFile(abs.string(), 1) == std::vector<uint8_t>{'x'}) return std::nullopt;
    return pb::MediaProbe{30, 10, 777};
  }
  std::optional<pb::Image> decodeStill(const fs::path&, uint32_t) const override {
    pb::Image img{30, 10, {}};
    img.rgba.assign(30 * 10 * 4, 128);
    return img;
  }
};

}  // namespace

TEST_CASE("media formats: extension lookup is case-insensitive, JPEG always accepted") {
  FakeHandler h;
  pb::MediaFormats f;
  CHECK(f.signature().empty());
  CHECK_FALSE(f.accepts("a/b.mp4"));
  f.add(".MP4", &h);
  f.add(".mov", &h);
  f.add(".jpg", &h);  // JPEG stays built in
  CHECK(f.find("dir.x/clip.Mp4") == &h);
  CHECK(f.find("a.jpg") == nullptr);
  CHECK(f.find("noext") == nullptr);
  CHECK(f.accepts("A.JPEG"));
  CHECK(f.accepts("c.mov"));
  CHECK_FALSE(f.accepts("c.png"));
  CHECK(f.signature() == ".mov .mp4");
  CHECK(pb::lowerExtension("a.b/c") == "");
  CHECK(pb::lowerExtension("x/Y.JPG") == ".jpg");
}

TEST_CASE("media formats: scanner includes registered extensions") {
  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "a.jpg", "1");
  pbtest::writeFile(dir.path / "b.mp4", "2");
  pbtest::writeFile(dir.path / "c.png", "3");
  CHECK(pb::scanFolder(dir.path).size() == 1);
  FakeHandler h;
  pb::MediaFormats f;
  f.add(".mp4", &h);
  auto e = pb::scanFolder(dir.path, f);
  REQUIRE(e.size() == 2);
  CHECK(e[1].relPath == "b.mp4");
}

TEST_CASE("media formats: indexer uses the handler for size, date and thumbnails") {
  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "v.mp4", "ok");
  pbtest::writeFile(dir.path / "bad.mp4", "x");
  FakeHandler h;
  pb::MediaFormats f;
  f.add(".mp4", &h);
  const auto scan = pb::scanFolder(dir.path, f);
  REQUIRE(scan.size() == 2);
  auto bad = pb::quickIndex(dir.path, scan[0], f);
  CHECK((bad.rec.flags & pb::kBroken) != 0);
  auto q = pb::quickIndex(dir.path, scan[1], f);
  CHECK(q.rec.flags == 0);
  CHECK(q.rec.width == 30);
  CHECK(q.rec.height == 10);
  CHECK(q.rec.takenAt == 777);
  CHECK(q.l1.w == 30);  // never upscaled
  CHECK(q.l0.w == 30);
  auto levels = pb::buildLazyLevels(dir.path / "v.mp4", q.rec, 3, &h);
  CHECK(levels[3].w == 30);
  CHECK(levels[2].w == 30);
  auto full = pb::decodeFull(dir.path / "v.mp4", q.rec, 2000, &h);
  REQUIRE(full);
  CHECK(full->w == 30);
}

TEST_CASE("media formats: catalog remembers its formats; warm load requires the same set") {
  pbtest::TempDir photos, cache;
  fs::copy_file(pbtest::photo("PXL_20260321_003010085.jpg"), photos.path / "p.jpg");
  pbtest::writeFile(photos.path / "v.mp4", "ok");
  FakeHandler h;
  pb::MediaFormats f;
  f.add(".mp4", &h);
  const fs::path libDir = cache.path / "lib";
  pb::ThumbCache thumbs(libDir);
  {
    pb::JobSystem jobs(2);
    pb::Indexer ix(photos.path, libDir, jobs, thumbs, f);
    ix.start();
    for (int i = 0; i < 2000 && !ix.finished(); ++i) std::this_thread::sleep_for(5ms);
    REQUIRE(ix.finished());
    auto st = ix.take();
    CHECK(st.catalog.order.size() == 2);
    CHECK(st.catalog.formats == ".mp4");
  }
  CHECK_FALSE(pb::loadWarm(libDir));  // plugin not loaded this time: re-index
  auto warm = pb::loadWarm(libDir, f);
  REQUIRE(warm);
  CHECK(warm->catalog.formats == ".mp4");
  CHECK(warm->catalog.order.size() == 2);
}

TEST_CASE("plugins: candidates for a name or a path") {
  const std::vector<fs::path> dirs = {"/opt/pb/plugins", "/opt/pb"};
  auto byName = pb::pluginCandidates("video", dirs);
  REQUIRE(byName.size() == 2);
  CHECK(byName[0] == fs::path("/opt/pb/plugins") / pb::sharedLibraryName("video"));
  CHECK(pb::pluginCandidates("./x/libpb_video.so", dirs) == std::vector<fs::path>{"./x/libpb_video.so"});
  CHECK(pb::pluginCandidates("libpb_video.so", dirs) == std::vector<fs::path>{"libpb_video.so"});
}

TEST_CASE("plugins: missing plugin is a readable error") {
  pb::PluginHost host;
  auto err = host.load("nosuchplugin", {fs::path(PB_TEST_PLUGIN_DIR)});
  REQUIRE(err);
  CHECK(err->find("nosuchplugin") != std::string::npos);
  CHECK(host.plugins().empty());
  CHECK(host.formats().signature().empty());
}

TEST_CASE("plugins: load the test plugin and use its formats and players") {
  pb::PluginHost host;
  REQUIRE_FALSE(host.load("testfake", {"/nonexistent", fs::path(PB_TEST_PLUGIN_DIR)}));
  CHECK_FALSE(host.load("testfake", {fs::path(PB_TEST_PLUGIN_DIR)}));  // twice is fine
  REQUIRE(host.plugins().size() == 1);
  CHECK(host.plugins()[0].name == "testfake");
  CHECK(host.plugins()[0].extensions == std::vector<std::string>{".fake"});
  CHECK(host.formats().signature() == ".fake");

  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "clip.fake", "A");
  const pb::MediaHandler* h = host.formats().find("clip.FAKE");
  REQUIRE(h);
  auto probe = h->probe(dir.path / "clip.fake");
  REQUIRE(probe);
  CHECK(probe->width == 40);
  CHECK(probe->takenAt == 1234);
  CHECK_FALSE(h->probe(dir.path / "missing.fake"));
  auto still = h->decodeStill(dir.path / "clip.fake", 128);
  REQUIRE(still);
  CHECK(still->w == 40);
  CHECK(still->h == 20);
  CHECK(still->rgba[0] == 'A');

  CHECK_FALSE(host.openPlayer(dir.path / "photo.jpg"));
  auto player = host.openPlayer(dir.path / "clip.fake");
  REQUIRE(player);
  CHECK_FALSE(player->frame());
  CHECK_FALSE(player->active());
  player->activate();
  CHECK(player->active());
  auto frame = player->frame();
  REQUIRE(frame);
  CHECK(frame->w == 4);
  CHECK(frame->rgba[0] == 200);
  CHECK(frame->serial == 1);
  player->activate();
  CHECK_FALSE(player->active());
}

TEST_CASE("plugins: tile source from a plugin") {
  pbtest::TempDir dir;
  pb::PluginHost host(nullptr, {}, dir.path / "pbcache");
  CHECK_FALSE(host.tileSource());
  REQUIRE_FALSE(host.load("testfake", {fs::path(PB_TEST_PLUGIN_DIR)}));
  CHECK(fs::is_directory(dir.path / "pbcache"));
  const pb::TileSource* src = host.tileSource();
  REQUIRE(src);
  CHECK(src->attribution() == "test tiles");
  CHECK(src->minZoom() == 0);
  CHECK(src->maxZoom() == 5);
  const pb::TileResult r = src->get(2, 1, 3);
  REQUIRE(r.status == pb::TileStatus::Ok);
  REQUIRE(r.image.w == 256);
  REQUIRE(r.image.h == 256);
  CHECK(r.image.rgba[0] == 80);
  CHECK(r.image.rgba[1] == 1);
  CHECK(r.image.rgba[2] == 3);
  CHECK(r.image.rgba[3] == 255);
}

TEST_CASE("plugins: tile source results are validated") {
  pb::PluginHost host;
  REQUIRE_FALSE(host.load("testfake", {fs::path(PB_TEST_PLUGIN_DIR)}));
  const pb::TileSource* src = host.tileSource();
  REQUIRE(src);
  CHECK(src->get(5, 0, 0).status == pb::TileStatus::Missing);
  const pb::TileResult wrongSize = src->get(4, 0, 0);
  CHECK(wrongSize.status == pb::TileStatus::Error);
  CHECK(wrongSize.image.empty());
}

TEST_CASE("plugins: settings reach the plugin") {
  pb::PluginHost host(nullptr, {{"testfake", {{"mode", "offline"}}}});
  REQUIRE_FALSE(host.load("testfake", {fs::path(PB_TEST_PLUGIN_DIR)}));
  REQUIRE(host.tileSource());
  CHECK(host.tileSource()->get(1, 0, 0).status == pb::TileStatus::Offline);
}

TEST_CASE("plugins: a v1-sized plugin table has no tile source") {
  pb::PluginHost host;
  REQUIRE_FALSE(host.load("testfake_v1", {fs::path(PB_TEST_PLUGIN_DIR)}));
  REQUIRE(host.plugins().size() == 1);
  CHECK(host.plugins()[0].name == "testfake_v1");
  CHECK_FALSE(host.tileSource());
}

#ifdef PB_TEST_VIDEO_PLUGIN
// Uses the real video plugin on a sample clip anywhere under photos/ (if any).
TEST_CASE("video plugin: still, metadata and playback of a sample clip") {
  fs::path clip;
  for (const auto& e : fs::recursive_directory_iterator(PB_TEST_PHOTOS_DIR)) {
    if (pb::lowerExtension(e.path().filename().string()) == ".mp4") clip = e.path();
  }
  if (clip.empty()) return;
  pb::PluginHost host;
  REQUIRE_FALSE(host.load("video", {fs::path(PB_TEST_PLUGIN_DIR)}));
  const pb::MediaHandler* h = host.formats().find(clip.filename().string());
  REQUIRE(h);
  auto probe = h->probe(clip);
  REQUIRE(probe);
  CHECK(probe->width > 0);
  CHECK(probe->height > 0);
  CHECK(probe->takenAt > 1'600'000'000);
  auto still = h->decodeStill(clip, 256);
  REQUIRE(still);
  CHECK(std::max(still->w, still->h) == 256);
  CHECK(double(still->w) / still->h == doctest::Approx(double(probe->width) / probe->height).epsilon(0.01));

  auto player = host.openPlayer(clip);
  REQUIRE(player);
  CHECK_FALSE(player->frame());  // nothing until clicked
  player->activate();
  std::optional<pb::LiveFrame> first;
  for (int i = 0; i < 300 && !first; ++i) {
    std::this_thread::sleep_for(10ms);
    first = player->frame();
  }
  REQUIRE(first);
  CHECK(first->w == probe->width);
  CHECK(first->h == probe->height);
  const uint64_t serial = first->serial;
  std::this_thread::sleep_for(300ms);
  auto later = player->frame();
  REQUIRE(later);
  CHECK(later->serial > serial);  // playing
  CHECK(player->active());
  player->activate();  // pause
  CHECK_FALSE(player->active());
  const uint64_t pausedAt = player->frame()->serial;
  std::this_thread::sleep_for(300ms);
  CHECK(player->frame()->serial == pausedAt);
  player.reset();  // closes without waiting for the decode thread
}
#endif
