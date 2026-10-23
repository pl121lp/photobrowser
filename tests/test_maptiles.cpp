// The maptiles plugin: its helpers directly, the plugin through the PluginHost. All
// offline: file:// URLs and a closed local port.
#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "../plugins/maptiles/maptiles_util.h"
#include "plugin/plugin_host.h"
#include "test_util.h"

namespace fs = std::filesystem;

TEST_CASE("maptiles: url template") {
  CHECK(pbmaps::expandUrl("https://t/{z}/{x}/{y}.png?k={key}", 3, 4, 5, "abc") == "https://t/3/4/5.png?k=abc");
  CHECK(pbmaps::expandUrl("https://t/{z}/{x}/{y}.png", 12, 1205, 1540, "") == "https://t/12/1205/1540.png");
}

TEST_CASE("maptiles: cache path") {
  const fs::path root = "/cache";
  const fs::path p = pbmaps::tilePath(root, "https://t/{z}/{x}/{y}.png", 3, 4, 5);
  const std::string s = p.generic_string();
  REQUIRE(s.starts_with("/cache/maptiles/"));
  const std::string hashDir = p.parent_path().parent_path().parent_path().filename().string();
  CHECK(hashDir.size() == 16);
  CHECK(s.ends_with("/3/4/5.png"));
  CHECK(pbmaps::tilePath(root, "https://u/{z}/{x}/{y}.png", 3, 4, 5).parent_path().parent_path().parent_path() !=
        p.parent_path().parent_path().parent_path());
  CHECK(pbmaps::fileExtension("https://t/{z}/{x}/{y}.jpg?key={key}") == "jpg");
  CHECK(pbmaps::fileExtension("https://t/{z}/{x}/{y}") == "img");
}

TEST_CASE("maptiles: cache headers") {
  CHECK(pbmaps::maxAgeSeconds("HTTP/1.1 200 OK\r\nCache-Control: public, max-age=604800\r\n") == 604800);
  CHECK(pbmaps::maxAgeSeconds("cache-control: max-age=86400, stale-while-revalidate=1\r\n") == 86400);
  CHECK_FALSE(pbmaps::maxAgeSeconds("cache-control: no-cache\r\n"));
  CHECK_FALSE(pbmaps::maxAgeSeconds(""));
  CHECK(pbmaps::expiryFor(std::nullopt, 0) == 604800);
  CHECK(pbmaps::expiryFor(100, 1000) == 1100);
}

TEST_CASE("maptiles: transfer results") {
  CHECK(pbmaps::mapTransfer(CURLE_OK, 200) == PB_TILE_OK);
  CHECK(pbmaps::mapTransfer(CURLE_OK, 0) == PB_TILE_OK);  // file://
  CHECK(pbmaps::mapTransfer(CURLE_OK, 404) == PB_TILE_MISSING);
  CHECK(pbmaps::mapTransfer(CURLE_FILE_COULDNT_READ_FILE, 0) == PB_TILE_MISSING);
  CHECK(pbmaps::mapTransfer(CURLE_OK, 403) == PB_TILE_REFUSED);
  CHECK(pbmaps::mapTransfer(CURLE_OK, 429) == PB_TILE_REFUSED);
  CHECK(pbmaps::mapTransfer(CURLE_OK, 500) == PB_TILE_ERROR);
  for (CURLcode c : {CURLE_COULDNT_RESOLVE_HOST, CURLE_COULDNT_RESOLVE_PROXY, CURLE_COULDNT_CONNECT,
                     CURLE_OPERATION_TIMEDOUT, CURLE_SEND_ERROR, CURLE_RECV_ERROR, CURLE_GOT_NOTHING}) {
    CHECK(pbmaps::mapTransfer(c, 0) == PB_TILE_OFFLINE);
  }
  CHECK(pbmaps::mapTransfer(CURLE_SSL_CONNECT_ERROR, 0) == PB_TILE_ERROR);
}

TEST_CASE("maptiles: trim deletes the oldest files first") {
  pbtest::TempDir dir;
  const auto now = fs::file_time_type::clock::now();
  int i = 0;
  for (const char* name : {"a/old.png", "b/mid.png", "c/new.png"}) {
    pbtest::writeFile(dir.path / name, std::string(1000, 'x'));
    fs::last_write_time(dir.path / name, now - std::chrono::hours(10 - i++));
  }
  pbmaps::trimCache(dir.path, 2500);
  CHECK_FALSE(fs::exists(dir.path / "a/old.png"));
  CHECK(fs::exists(dir.path / "b/mid.png"));
  CHECK(fs::exists(dir.path / "c/new.png"));
}

TEST_CASE("maptiles plugin: tiles from a file URL, cached, missing, offline") {
  pbtest::TempDir dir;
  fs::create_directories(dir.path / "src/1/0");
  fs::copy_file(fs::path(PB_TEST_DATA_DIR) / "tile.png", dir.path / "src/1/0/1.png");
  const std::string tmpl = "file://" + (dir.path / "src").generic_string() + "/{z}/{x}/{y}.png";
  {
    pb::PluginHost host(nullptr, {{"maptiles", {{"url", tmpl}, {"attribution", "test map"}}}}, dir.path / "cache");
    REQUIRE_FALSE(host.load("maptiles", {fs::path(PB_TEST_PLUGIN_DIR)}));
    const pb::TileSource* src = host.tileSource();
    REQUIRE(src);
    CHECK(src->attribution() == "test map");
    CHECK(src->maxZoom() == 19);
    pb::TileResult r = src->get(1, 0, 1);
    REQUIRE(r.status == pb::TileStatus::Ok);
    CHECK(r.image.rgba[0] == 10);
    CHECK(r.image.rgba[1] == 200);
    CHECK(r.image.rgba[2] == 30);
    CHECK(r.image.rgba[3] == 255);
    const fs::path cached = pbmaps::tilePath(dir.path / "cache", tmpl, 1, 0, 1);
    CHECK(fs::exists(cached));
    CHECK(fs::exists(cached.string() + ".exp"));
    fs::remove(dir.path / "src/1/0/1.png");
    CHECK(src->get(1, 0, 1).status == pb::TileStatus::Ok);  // from the disk cache
    CHECK(src->get(1, 1, 1).status == pb::TileStatus::Missing);
  }
  {
    pb::PluginHost host(nullptr, {{"maptiles", {{"url", "http://127.0.0.1:9/{z}/{x}/{y}.png"}}}}, dir.path / "cache2");
    REQUIRE_FALSE(host.load("maptiles", {fs::path(PB_TEST_PLUGIN_DIR)}));
    REQUIRE(host.tileSource());
    CHECK(host.tileSource()->get(0, 0, 0).status == pb::TileStatus::Offline);
    CHECK(host.tileSource()->attribution() == "© OpenStreetMap contributors");
  }
}
