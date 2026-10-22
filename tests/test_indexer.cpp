#include <doctest/doctest.h>

#include <algorithm>

#include "library/exif.h"
#include "library/indexer.h"
#include "library/scanner.h"
#include "platform/paths.h"
#include "test_util.h"

namespace {
pb::ScanEntry entry(const std::string& name) {
  auto all = pb::scanFolder(PB_TEST_PHOTOS_DIR);
  auto it = std::find_if(all.begin(), all.end(), [&](const pb::ScanEntry& e) { return e.relPath == name; });
  REQUIRE(it != all.end());
  return *it;
}
}  // namespace

TEST_CASE("indexer: pixel photo quick pass uses preview") {
  auto q = pb::quickIndex(PB_TEST_PHOTOS_DIR, entry("PXL_20260321_003010085.jpg"));
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
  auto q = pb::quickIndex(PB_TEST_PHOTOS_DIR, e);
  CHECK((q.rec.flags & pb::kNeedsDecodeL1) != 0);
  CHECK(q.l1.empty());
  pb::Image l0, l1;
  REQUIRE(pb::decodeL0L1FromOriginal(std::filesystem::path(PB_TEST_PHOTOS_DIR) / e.relPath, q.rec, l0, l1));
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
  auto q = pb::quickIndex(PB_TEST_PHOTOS_DIR, e);
  auto lv = pb::buildLazyLevels(std::filesystem::path(PB_TEST_PHOTOS_DIR) / e.relPath, q.rec, 4);
  CHECK(lv[4].w == 1024);
  CHECK(lv[4].h == 768);
  CHECK(lv[3].w == 512);
  CHECK(lv[2].w == 256);
  CHECK(lv[1].empty());
  CHECK(lv[0].empty());
}

TEST_CASE("indexer: full decode at screen size") {
  const auto e = entry("PXL_20260321_003010085.jpg");
  auto q = pb::quickIndex(PB_TEST_PHOTOS_DIR, e);
  auto f = pb::decodeFull(std::filesystem::path(PB_TEST_PHOTOS_DIR) / e.relPath, q.rec, 2000);
  REQUIRE(f);
  CHECK(f->h == 2040);  // 1/2 scale
}
