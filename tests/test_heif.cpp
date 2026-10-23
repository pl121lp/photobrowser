// The heif plugin: its pure helpers directly, the plugin through the PluginHost on
// iPhone fixtures under photos/.
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <thread>
#include <vector>

#include "../plugins/heif/heif_util.h"
#include "library/exif.h"
#include "library/media_format.h"
#include "plugin/plugin_host.h"
#include "test_util.h"

namespace fs = std::filesystem;

TEST_CASE("heif util: exif item offset") {
  const std::vector<uint8_t> withHeader = {0, 0, 0, 6, 'E', 'x', 'i', 'f', 0, 0, 'M', 'M', 0, 42, 0, 0, 0, 8};
  auto t = pbheif::exifTiffFromItem(withHeader);
  REQUIRE(t.size() == 8);
  CHECK(t.data() == withHeader.data() + 10);
  CHECK(t[0] == 'M');

  const std::vector<uint8_t> bare = {0, 0, 0, 0, 'M', 'M', 0, 42, 0, 0, 0, 8};
  t = pbheif::exifTiffFromItem(bare);
  REQUIRE(t.size() == 8);
  CHECK(t.data() == bare.data() + 4);

  const std::vector<uint8_t> beyond = {0, 0, 0, 40, 'M', 'M', 0, 42, 0, 0, 0, 8};
  CHECK(pbheif::exifTiffFromItem(beyond).empty());
  const std::vector<uint8_t> notTiff = {0, 0, 0, 0, 'X', 'Y', 0, 42, 0, 0, 0, 8};
  CHECK(pbheif::exifTiffFromItem(notTiff).empty());
  const std::vector<uint8_t> littleEndian = {0, 0, 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0};
  CHECK(pbheif::exifTiffFromItem(littleEndian).size() == 8);
  CHECK(pbheif::exifTiffFromItem({}).empty());

  const std::vector<uint8_t> zeroOffsetPrefixed = {0, 0, 0, 0, 'E', 'x', 'i', 'f', 0, 0, 'M', 'M', 0, 42, 0, 0, 0, 8};
  t = pbheif::exifTiffFromItem(zeroOffsetPrefixed);
  REQUIRE(t.size() == 8);
  CHECK(t.data() == zeroOffsetPrefixed.data() + 10);
}

TEST_CASE("heif util: thumbnail choice") {
  using Sizes = std::vector<std::pair<uint32_t, uint32_t>>;
  const Sizes one = {{320, 240}};
  CHECK(pbheif::chooseThumbnail(one, 128) == 0);
  CHECK(pbheif::chooseThumbnail(one, 256) == 0);
  CHECK(pbheif::chooseThumbnail(one, 512) == -1);
  CHECK(pbheif::chooseThumbnail(Sizes{}, 128) == -1);
  const Sizes two = {{160, 120}, {480, 360}};
  CHECK(pbheif::chooseThumbnail(two, 128) == 0);
  CHECK(pbheif::chooseThumbnail(two, 200) == 1);
}

TEST_CASE("heif util: utf-8 path") {
  pbtest::TempDir tmp;
  const fs::path dir = tmp.path / fs::path(u8"Tromsø");
  fs::create_directories(dir);
  const std::vector<uint8_t> bytes = {1, 2, 3, 0, 255, 'a'};
  {
    std::ofstream f(dir / "a.bin", std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  }
  const std::u8string p = (dir / "a.bin").u8string();
  auto read = pbheif::readFileUtf8(reinterpret_cast<const char*>(p.c_str()));
  REQUIRE(read);
  CHECK(*read == bytes);
  CHECK_FALSE(pbheif::readFileUtf8(reinterpret_cast<const char*>((dir / "missing.bin").u8string().c_str())));
}

#ifdef PB_TEST_HEIF_PLUGIN
namespace {

struct HeifHost {
  pb::PluginHost host;
  const pb::MediaHandler* h = nullptr;
  HeifHost() {
    REQUIRE_FALSE(host.load("heif", {fs::path(PB_TEST_PLUGIN_DIR)}));
    h = host.formats().find("x.heic");
  }
};

// Without an HEVC decoder the plugin loads with no extensions: skip the fixture tests.
#define REQUIRE_HEIF_DECODER(hh)                                                \
  if (!(hh).h) {                                                                \
    MESSAGE("heif plugin has no HEVC decoder; skipping");                       \
    return;                                                                     \
  }

}  // namespace

TEST_CASE("heif plugin: GPS and date of an iPhone photo") {
  HeifHost hh;
  REQUIRE_HEIF_DECODER(hh);
  auto p = hh.h->probe(pbtest::photo("IMG_2073.HEIC"));
  REQUIRE(p);
  CHECK(p->width == 4032);
  CHECK(p->height == 3024);
  CHECK(p->takenAt == *pb::parseExifDateTime("2025:11:27 19:32:57"));
  REQUIRE(p->gps);
  CHECK(p->gps->lat == doctest::Approx(34.2377917).epsilon(1e-6));
  CHECK(p->gps->lon == doctest::Approx(-116.8723528).epsilon(1e-6));
  REQUIRE(p->gps->altitude);
  CHECK(*p->gps->altitude == doctest::Approx(2112.55).epsilon(0.01));
}

TEST_CASE("heif plugin: portrait photo is upright once") {
  HeifHost hh;
  REQUIRE_HEIF_DECODER(hh);
  const std::string path = pbtest::photo("IMG_5693.HEIC");
  auto p = hh.h->probe(path);
  REQUIRE(p);
  CHECK(p->width == 3024);
  CHECK(p->height == 4032);
  CHECK_FALSE(p->gps);
  auto img = hh.h->decodeStill(path, 1024);
  REQUIRE(img);
  CHECK(img->h > img->w);
  CHECK(double(img->w) / img->h == doctest::Approx(3024.0 / 4032.0).epsilon(0.01));
}

TEST_CASE("heif plugin: indexing size uses a small image") {
  HeifHost hh;
  REQUIRE_HEIF_DECODER(hh);
  auto img = hh.h->decodeStill(pbtest::photo("IMG_2073.HEIC"), 128);
  REQUIRE(img);
  CHECK(std::max(img->w, img->h) >= 128);
  CHECK(std::max(img->w, img->h) < 4032);
}

TEST_CASE("heif plugin: truncated file is rejected") {
  HeifHost hh;
  REQUIRE_HEIF_DECODER(hh);
  pbtest::TempDir tmp;
  const fs::path cut = tmp.path / "cut.heic";
  const auto bytes = pbtest::readFile(pbtest::photo("IMG_2073.HEIC"), 100 * 1024);
  {
    std::ofstream f(cut, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  }
  // The 320x240 thumbnail lies within the first 100 KB (so a small still may still
  // decode); the primary image does not.
  auto p = hh.h->probe(cut);
  auto img = hh.h->decodeStill(cut, 1024);
  CHECK((!p || !img));
}

TEST_CASE("heif plugin: non-ASCII folder") {
  HeifHost hh;
  REQUIRE_HEIF_DECODER(hh);
  pbtest::TempDir tmp;
  const fs::path dir = tmp.path / fs::path(u8"Tromsø");
  fs::create_directories(dir);
  fs::copy_file(pbtest::photo("IMG_2073.HEIC"), dir / "IMG_2073.HEIC");
  auto p = hh.h->probe(dir / "IMG_2073.HEIC");
  REQUIRE(p);
  CHECK(p->gps);
}

TEST_CASE("heif plugin: concurrent decodes") {
  HeifHost hh;
  REQUIRE_HEIF_DECODER(hh);
  const std::string landscape = pbtest::photo("IMG_2073.HEIC");
  const std::string portrait = pbtest::photo("IMG_5693.HEIC");
  std::atomic<int> ok{0}, bad{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 4; ++i) {
        auto l = hh.h->decodeStill(landscape, 128);
        auto p = hh.h->decodeStill(portrait, 128);
        (l && l->w > l->h && p && p->h > p->w ? ok : bad)++;
      }
    });
  }
  for (auto& th : threads) th.join();
  CHECK(bad == 0);
  CHECK(ok == 32);
}
#endif
