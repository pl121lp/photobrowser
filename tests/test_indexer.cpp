#include <doctest/doctest.h>

#include <algorithm>

#include "library/exif.h"
#include "library/indexer.h"
#include "library/png_handler.h"
#include "library/scanner.h"
#include "platform/paths.h"
#include "test_util.h"

namespace {
// Scan entry for fixture photo `name`, relative to pbtest::photoDir(name).
pb::ScanEntry entry(const std::string& name) {
  auto all = pb::scanFolder(pbtest::photoDir(name), pb::MediaFormats::none(), false);
  auto it = std::find_if(all.begin(), all.end(), [&](const pb::ScanEntry& e) { return e.relPath == name; });
  REQUIRE(it != all.end());
  return *it;
}
}  // namespace

TEST_CASE("indexer: pixel photo quick pass uses preview") {
  const std::string name = "PXL_20260321_003010085.jpg";
  auto q = pb::quickIndex(pbtest::photoDir(name), entry(name));
  CHECK(q.rec.relPath == "PXL_20260321_003010085.jpg");
  CHECK(q.rec.takenAt == *pb::parseExifDateTime("2026:03:20 20:30:10"));
  CHECK(q.rec.width == 3072);
  CHECK(q.rec.height == 4080);
  CHECK(q.rec.key != 0);
  CHECK((q.rec.flags & (pb::kNeedsDecodeL1 | pb::kBroken)) == 0);
  CHECK(q.l1.w == 96);
  CHECK(q.l1.h == 128);
  CHECK(q.l0.w == 24);
  CHECK(q.l0.h == 32);
}

TEST_CASE("indexer: file without preview falls back") {
  const auto e = entry("PXL_20260321_012354015~2.jpg");
  const auto dir = pbtest::photoDir(e.relPath);
  auto q = pb::quickIndex(dir, e);
  CHECK((q.rec.flags & pb::kNeedsDecodeL1) != 0);
  CHECK(q.l1.empty());
  pb::Image l0, l1;
  REQUIRE(pb::decodeL0L1FromOriginal(dir / e.relPath, q.rec, l0, l1));
  CHECK(std::max(l1.w, l1.h) == 128);
  CHECK(std::max(l0.w, l0.h) == 32);
  CHECK((q.rec.flags & pb::kNeedsDecodeL1) == 0);
}

TEST_CASE("indexer: non-jpeg is broken") {
  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "x.jpg", "this is not a jpeg");
  auto scan = pb::scanFolder(dir.path);
  REQUIRE(scan.size() == 1);
  auto q = pb::quickIndex(dir.path, scan[0]);
  CHECK((q.rec.flags & pb::kBroken) != 0);
  CHECK(q.rec.takenAt == scan[0].mtime);
}

TEST_CASE("indexer: truncated jpeg does not crash") {
  pbtest::TempDir dir;
  auto bytes = pbtest::readFile(pbtest::photo("PXL_20260321_003010085.jpg"), 40000);
  pbtest::writeFile(dir.path / "cut.jpg", std::string(bytes.begin(), bytes.end()));
  auto scan = pb::scanFolder(dir.path);
  auto q = pb::quickIndex(dir.path, scan[0]);
  CHECK((q.rec.flags & pb::kBroken) == 0);  // preview is intact in the first 40 KB
  CHECK(q.l1.h == 128);
  pb::Image l0, l1;
  CHECK_NOTHROW(pb::decodeL0L1FromOriginal(dir.path / "cut.jpg", q.rec, l0, l1));
}

TEST_CASE("indexer: lazy levels cascade") {
  const auto e = entry("PXL_20260321_010024264.jpg");  // landscape 3648x2736
  const auto dir = pbtest::photoDir(e.relPath);
  auto q = pb::quickIndex(dir, e);
  auto lv = pb::buildLazyLevels(dir / e.relPath, q.rec, 4);
  CHECK(lv[4].w == 1024);
  CHECK(lv[4].h == 768);
  CHECK(lv[3].w == 512);
  CHECK(lv[2].w == 256);
  CHECK(lv[1].empty());
  CHECK(lv[0].empty());
}

TEST_CASE("indexer: full decode at screen size") {
  const auto e = entry("PXL_20260321_003010085.jpg");
  const auto dir = pbtest::photoDir(e.relPath);
  auto q = pb::quickIndex(dir, e);
  auto f = pb::decodeFull(dir / e.relPath, q.rec, 2000);
  REQUIRE(f);
  CHECK(f->h == 2040);  // 1/2 scale
}

TEST_CASE("indexer: PNG through the built-in handler") {
  // Big-endian TIFF: IFD0 {ExifIFD ptr}, ExifIFD {DateTimeOriginal}.
  auto be16 = [](std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); };
  auto be32 = [&](std::vector<uint8_t>& v, uint32_t x) { be16(v, uint16_t(x >> 16)); be16(v, uint16_t(x)); };
  auto entry = [&](std::vector<uint8_t>& v, uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
    be16(v, tag); be16(v, type); be32(v, count); be32(v, value);
  };
  std::vector<uint8_t> tiff = {'M', 'M', 0, 42};
  be32(tiff, 8);
  be16(tiff, 1);
  entry(tiff, 0x8769, 4, 1, 26);  // 8 + 2 + 12 + 4
  be32(tiff, 0);
  be16(tiff, 1);
  entry(tiff, 0x9003, 2, 20, 26 + 2 + 12 + 4);
  be32(tiff, 0);
  for (char c : std::string("2026:06:16 21:56:20")) tiff.push_back(uint8_t(c));
  tiff.push_back(0);

  pbtest::TempDir dir;
  const auto png = pbtest::makePng(300, 200, std::vector<uint8_t>(300 * 200 * 4, 200), tiff);
  pbtest::writeFile(dir.path / "a.png", std::string(png.begin(), png.end()));
  static const pb::PngHandler handler;
  pb::MediaFormats formats;
  formats.add(".png", &handler, pb::kPngOrigin);
  const auto scan = pb::scanFolder(dir.path, formats);
  REQUIRE(scan.size() == 1);
  const auto q = pb::quickIndex(dir.path, scan[0], formats);
  CHECK((q.rec.flags & pb::kBroken) == 0);
  CHECK(q.rec.width == 300);
  CHECK(q.rec.height == 200);
  CHECK(q.rec.takenAt == *pb::parseExifDateTime("2026:06:16 21:56:20"));
  CHECK_FALSE(q.l0.rgba.empty());
  CHECK_FALSE(q.l1.rgba.empty());
}
