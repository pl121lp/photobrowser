#include <doctest/doctest.h>

#include "library/scanner.h"
#include "test_util.h"

TEST_CASE("scanner: recursive, extension filter, unicode") {
  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "a.JPG", "1");
  pbtest::writeFile(dir.path / "sub" / "b.jpeg", "22");
  pbtest::writeFile(dir.path / "sub" / std::filesystem::path(u8"ø.jpg"), "333");
  pbtest::writeFile(dir.path / "c.png", "x");
  pbtest::writeFile(dir.path / "d.txt", "x");
  auto e = pb::scanFolder(dir.path);
  REQUIRE(e.size() == 3);
  CHECK(e[0].relPath == "a.JPG");
  CHECK(e[1].relPath == "sub/b.jpeg");
  CHECK(e[2].relPath == "sub/ø.jpg");
  CHECK(e[1].size == 2);
  CHECK(e[2].mtime > 0);
}

TEST_CASE("scanner: missing folder gives empty result") {
  CHECK(pb::scanFolder("/definitely/not/here").empty());
}

TEST_CASE("scanner: diff detects added, removed, changed") {
  pb::Catalog c;
  pb::PhotoRecord a, b, d;
  a.relPath = "a.jpg"; a.fileSize = 1; a.mtime = 1;
  b.relPath = "b.jpg"; b.fileSize = 2; b.mtime = 2;
  d.relPath = "d.jpg"; d.fileSize = 4; d.mtime = 4;
  c.photos = {a, b, d};
  std::vector<pb::ScanEntry> now = {{"a.jpg", 1, 1}, {"b.jpg", 2, 99}, {"e.jpg", 5, 5}};
  auto diff = pb::diffCatalog(c, now);
  REQUIRE(diff.added.size() == 1);
  CHECK(diff.added[0].relPath == "e.jpg");
  CHECK(diff.removed == std::vector<pb::PhotoId>{2});
  CHECK(diff.changed == std::vector<pb::PhotoId>{1});
  CHECK_FALSE(diff.empty());
  CHECK(pb::diffCatalog(c, {{"a.jpg", 1, 1}, {"b.jpg", 2, 2}, {"d.jpg", 4, 4}}).empty());
}
