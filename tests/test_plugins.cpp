#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "app/library_index.h"
#include "library/catalog.h"
#include "library/indexer.h"
#include "library/exif.h"
#include "library/media_format.h"
#include "library/scanner.h"
#include "platform/dynlib.h"
#include "plugin/photobrowse_plugin.h"
#include "plugin/plugin_host.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

// A format whose files are 30x10 stills (or broken if the file starts with 'x').
struct FakeHandler final : pb::MediaHandler {
  std::optional<pb::MediaProbe> probe(const fs::path& abs) const override {
    if (pbtest::readFile(abs.string(), 1) == std::vector<uint8_t>{'x'}) return std::nullopt;
    return pb::MediaProbe{30, 10, 777, pb::GeoPos{60.39, 5.32, 12.5}};
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
  pb::MediaFormats tagged;
  tagged.add(".mp4", &h, "video 1.1");
  tagged.add(".avi", &h, "video 1.1");
  CHECK(tagged.signature() == ".avi .mp4 (video 1.1)");  // a plugin upgrade re-indexes
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
  REQUIRE(q.rec.gps);
  CHECK(q.rec.gps->lat == 60.39);
  CHECK(q.rec.gps->lon == 5.32);
  CHECK(q.rec.gps->altitude == 12.5);
  CHECK(q.l1.w == 30);  // never upscaled
  CHECK(q.l0.w == 30);
  auto levels = pb::buildLazyLevels(dir.path / "v.mp4", q.rec, 3, &h);
  CHECK(levels[3].w == 30);
  CHECK(levels[2].w == 30);
  auto full = pb::decodeFull(dir.path / "v.mp4", q.rec, 2000, &h);
  REQUIRE(full);
  CHECK(full->w == 30);
}

TEST_CASE("media formats: a plugin upgrade gives its files new thumbnail cache keys") {
  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "v.mp4", "ok");
  FakeHandler h;
  pb::MediaFormats v1, v1again, v2;
  v1.add(".mp4", &h, "video 1.1");
  v1again.add(".mp4", &h, "video 1.1");
  v2.add(".mp4", &h, "video 1.2");
  const auto scan = pb::scanFolder(dir.path, v1);
  REQUIRE(scan.size() == 1);
  const uint64_t k1 = pb::quickIndex(dir.path, scan[0], v1).rec.key;
  CHECK(pb::quickIndex(dir.path, scan[0], v1again).rec.key == k1);
  CHECK(pb::quickIndex(dir.path, scan[0], v2).rec.key != k1);  // stale stills are not reused
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

TEST_CASE("plugins: built-in formats come first and join the signature") {
  FakeHandler builtin;
  pb::PluginHost host;
  host.addBuiltinFormat(".png", &builtin, "core png 1");
  REQUIRE_FALSE(host.load("testfake", {fs::path(PB_TEST_PLUGIN_DIR)}));
  CHECK(host.formats().signature() == ".fake .png (core png 1, testfake 1.0)");
  CHECK(host.formats().find("a.PNG") == &builtin);
}

TEST_CASE("plugins: parse_exif gives plugins date and GPS") {
  pb::PluginHost host;
  REQUIRE_FALSE(host.load("testfake", {fs::path(PB_TEST_PLUGIN_DIR)}));
  const pb::MediaHandler* h = host.formats().find("x.fake");
  REQUIRE(h);
  pbtest::TempDir dir;
  const auto tiff = pbtest::makeDateGpsTiff();
  pbtest::writeFile(dir.path / "e.fake", std::string(tiff.begin(), tiff.end()));
  auto probe = h->probe(dir.path / "e.fake");
  REQUIRE(probe);
  CHECK(probe->takenAt == *pb::parseExifDateTime("2026:06:16 21:56:20"));
  REQUIRE(probe->gps);
  CHECK(probe->gps->lat == doctest::Approx(-33.875));
  CHECK(probe->gps->lon == doctest::Approx(-151.21));
  REQUIRE(probe->gps->altitude);
  CHECK(*probe->gps->altitude == doctest::Approx(-122.5));
}

TEST_CASE("plugins: load the test plugin and use its formats and players") {
  pb::PluginHost host;
  REQUIRE_FALSE(host.load("testfake", {"/nonexistent", fs::path(PB_TEST_PLUGIN_DIR)}));
  CHECK_FALSE(host.load("testfake", {fs::path(PB_TEST_PLUGIN_DIR)}));  // twice is fine
  REQUIRE(host.plugins().size() == 1);
  CHECK(host.plugins()[0].name == "testfake");
  CHECK(host.plugins()[0].extensions == std::vector<std::string>{".fake"});
  CHECK(host.formats().signature() == ".fake (testfake 1.0)");

  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "clip.fake", "A");
  const pb::MediaHandler* h = host.formats().find("clip.FAKE");
  REQUIRE(h);
  auto probe = h->probe(dir.path / "clip.fake");
  REQUIRE(probe);
  CHECK(probe->width == 40);
  CHECK(probe->takenAt == 1234);
  REQUIRE(probe->gps);
  CHECK(probe->gps->lat == -33.5);
  CHECK(probe->gps->lon == 151.25);
  CHECK_FALSE(probe->gps->altitude);
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
  CHECK_FALSE(player->progress());  // not started
  player->activate();
  CHECK(player->active());
  auto progress = player->progress();
  REQUIRE(progress);
  CHECK(progress->position == 2.5);
  CHECK(progress->duration == 10);
  player->setPlayOnce(true);  // the fake reports it as position 5
  CHECK(player->progress()->position == 5);
  player->setPlayOnce(false);
  CHECK(player->progress()->position == 2.5);
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
  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "clip.fake", "A");
  auto player = host.openPlayer(dir.path / "clip.fake");
  REQUIRE(player);
  player->activate();
  CHECK(player->active());
  CHECK_FALSE(player->progress());  // no player_progress in a v1-sized table
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
  CHECK(probe->gps);  // every sample clip has a location
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
  auto progress = player->progress();
  REQUIRE(progress);
  CHECK(progress->duration > 0.5);
  CHECK(progress->position > 0.1);
  CHECK(progress->position <= progress->duration);
  player->activate();  // pause
  CHECK_FALSE(player->active());
  const uint64_t pausedAt = player->frame()->serial;
  std::this_thread::sleep_for(300ms);
  CHECK(player->frame()->serial == pausedAt);
  player.reset();  // closes without waiting for the decode thread
}

// Regression: libswscale's unscaled YUV->RGBA path writes whole 16-pixel blocks, so a
// width that isn't a multiple of 16 used to overrun the tightly packed buffer by up to
// 32 bytes (heap corruption). tests/data/odd200x88.mp4: 200x88, yuv420p.
TEST_CASE("video plugin: stills never write past the image buffer") {
  constexpr size_t kGuard = 256;
  struct Buf {
    std::vector<uint8_t> bytes;
    size_t size = 0;
  };
  auto alloc = [](void* ctx, uint32_t w, uint32_t h) -> uint8_t* {
    auto* b = static_cast<Buf*>(ctx);
    b->size = size_t(w) * h * 4;
    b->bytes.assign(b->size + kGuard, 0xA5);
    return b->bytes.data();
  };
  std::string error;
  void* lib = pb::openLibrary(fs::path(PB_TEST_PLUGIN_DIR) / pb::sharedLibraryName("video"), error);
  REQUIRE(lib);
  auto entry = reinterpret_cast<PbPluginEntry>(pb::librarySymbol(lib, PB_PLUGIN_ENTRY_NAME));
  REQUIRE(entry);
  PbHost host{};
  host.abi_version = PB_PLUGIN_ABI_VERSION;
  host.struct_size = sizeof host;
  host.log = [](void*, int, const char*) {};
  host.setting = [](void*, const char*) -> const char* { return nullptr; };
  const PbPlugin* p = entry(&host);
  REQUIRE(p);
  const std::string clip = (fs::path(PB_TEST_DATA_DIR) / "odd200x88.mp4").string();
  for (uint32_t edge : {100u, 200u, 1000u}) {
    CAPTURE(edge);
    Buf b;
    PbImageOut out{&b, alloc};
    REQUIRE(p->decode_still(p->self, clip.c_str(), edge, &out));
    REQUIRE(b.size > 0);
    size_t overrun = 0;
    for (size_t i = b.size; i < b.bytes.size(); ++i) {
      if (b.bytes[i] != 0xA5) overrun = i - b.size + 1;
    }
    CHECK(overrun == 0);
  }
  if (p->destroy) p->destroy(p->self);
  pb::closeLibrary(lib);
}

// tests/data/odd200x88.mp4 is 0.5 s long.
TEST_CASE("video plugin: [plugin.video] loop = on restarts at the end, except when told to play once") {
  const fs::path clip = fs::path(PB_TEST_DATA_DIR) / "odd200x88.mp4";
  auto playsThrough = [](pb::Player& player) {  // true if it ends within 2 s
    for (int i = 0; i < 200; ++i) {
      std::this_thread::sleep_for(10ms);
      player.frame();  // ends are noticed when frames are taken
      if (!player.active()) return true;
    }
    return false;
  };
  {
    pb::PluginHost host;
    REQUIRE_FALSE(host.load("video", {fs::path(PB_TEST_PLUGIN_DIR)}));
    auto player = host.openPlayer(clip);
    REQUIRE(player);
    player->activate();
    CHECK(playsThrough(*player));  // off by default
  }
  pb::PluginHost host(nullptr, {{"video", {{"loop", "on"}}}});
  REQUIRE_FALSE(host.load("video", {fs::path(PB_TEST_PLUGIN_DIR)}));
  auto player = host.openPlayer(clip);
  REQUIRE(player);
  player->activate();
  double last = 0;
  bool wrapped = false;
  for (int i = 0; i < 150 && !wrapped; ++i) {  // 1.5 s: wraps around at least once
    std::this_thread::sleep_for(10ms);
    player->frame();
    CHECK(player->active());
    if (auto p = player->progress()) {
      wrapped = p->position < last - 0.2;
      last = p->position;
    }
  }
  CHECK(wrapped);
  player->setPlayOnce(true);  // e.g. the screensaver waits for the end
  CHECK(playsThrough(*player));
}
#endif
