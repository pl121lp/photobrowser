#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <thread>

#include "library/catalog.h"
#include "platform/paths.h"
#include "test_util.h"

namespace {
pb::PhotoRecord rec(std::string path, int64_t taken, uint8_t flags = 0) {
  pb::PhotoRecord r;
  r.relPath = std::move(path);
  r.takenAt = taken;
  r.flags = flags;
  return r;
}
}  // namespace

TEST_CASE("catalog: sort by time then name, broken excluded") {
  pb::Catalog c;
  c.photos = {rec("b.jpg", 100), rec("a.jpg", 100), rec("c.jpg", 50), rec("x.jpg", 10, pb::kBroken)};
  c.sortOrder();
  CHECK(c.order == std::vector<pb::PhotoId>{2, 1, 0});
}

TEST_CASE("catalog: aspect is post-orientation and defaults to 1") {
  pb::PhotoRecord r;
  CHECK(r.aspect() == 1.0);
  r.width = 300;
  r.height = 400;
  CHECK(r.aspect() == doctest::Approx(0.75));
}

TEST_CASE("catalog: save/load round trip") {
  pbtest::TempDir dir;
  pb::Catalog c;
  pb::PhotoRecord r = rec("Résumé/日本.jpg", 1774038610);
  r.fileSize = 123456789;
  r.mtime = -5;
  r.width = 3072;
  r.height = 4080;
  r.orientation = 6;
  r.flags = pb::kNeedsDecodeL1;
  r.key = 0xDEADBEEFCAFEF00DULL;
  r.thumbOffset = 1310;
  r.thumbLength = 23268;
  c.photos = {r, rec("b.jpg", 1)};
  c.sortOrder();
  REQUIRE(pb::saveCatalog(c, dir.path / "catalog.bin"));
  auto l = pb::loadCatalog(dir.path / "catalog.bin");
  REQUIRE(l);
  REQUIRE(l->photos.size() == 2);
  const auto& a = l->photos[0];
  CHECK(a.relPath == r.relPath);
  CHECK(a.fileSize == r.fileSize);
  CHECK(a.mtime == r.mtime);
  CHECK(a.takenAt == r.takenAt);
  CHECK(a.width == r.width);
  CHECK(a.height == r.height);
  CHECK(a.orientation == r.orientation);
  CHECK(a.flags == r.flags);
  CHECK(a.key == r.key);
  CHECK(a.thumbOffset == r.thumbOffset);
  CHECK(a.thumbLength == r.thumbLength);
  CHECK(l->order == c.order);
}

TEST_CASE("catalog: bad magic / truncated") {
  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "bad.bin", "XXXX");
  CHECK_FALSE(pb::loadCatalog(dir.path / "bad.bin"));
  CHECK_FALSE(pb::loadCatalog(dir.path / "missing.bin"));
  pb::Catalog c;
  c.photos = {rec("a.jpg", 1), rec("b.jpg", 2)};
  c.sortOrder();
  REQUIRE(pb::saveCatalog(c, dir.path / "ok.bin"));
  auto bytes = pbtest::readFile((dir.path / "ok.bin").string());
  for (size_t n : {bytes.size() / 2, bytes.size() - 1}) {
    pbtest::writeFile(dir.path / "cut.bin", std::string(bytes.begin(), bytes.begin() + n));
    CHECK_FALSE(pb::loadCatalog(dir.path / "cut.bin"));
  }
}

TEST_CASE("catalog: cache key changes with mtime and size") {
  const auto k = pb::cacheKey("/p/a.jpg", 10, 100);
  CHECK(k == pb::cacheKey("/p/a.jpg", 10, 100));
  CHECK(k != pb::cacheKey("/p/a.jpg", 11, 100));
  CHECK(k != pb::cacheKey("/p/a.jpg", 10, 101));
  CHECK(k != pb::cacheKey("/p/b.jpg", 10, 100));
}

TEST_CASE("paths: atomic write replaces and leaves no tmp") {
  pbtest::TempDir dir;
  const auto p = dir.path / "sub" / "f.bin";
  std::vector<uint8_t> a{1, 2, 3}, b{4, 5};
  REQUIRE(pb::atomicWriteFile(p, a));
  REQUIRE(pb::atomicWriteFile(p, b));
  CHECK(pbtest::readFile(p.string()) == b);
  size_t files = 0;
  for (auto& e : std::filesystem::directory_iterator(p.parent_path())) {
    (void)e;
    ++files;
  }
  CHECK(files == 1);
}

TEST_CASE("paths: read file range") {
  pbtest::TempDir dir;
  pbtest::writeFile(dir.path / "f", "abcdefgh");
  auto r = pb::readFileRange(dir.path / "f", 2, 3);
  REQUIRE(r);
  CHECK(std::string(r->begin(), r->end()) == "cde");
  auto tail = pb::readFileRange(dir.path / "f", 6, 100);
  REQUIRE(tail);
  CHECK(tail->size() == 2);
  CHECK_FALSE(pb::readFileRange(dir.path / "nope", 0, 1));
}

TEST_CASE("paths: library cache dir is stable and distinct per library") {
  const auto a = pb::libraryCacheDir("/cache", "/home/me/photos");
  CHECK(a == pb::libraryCacheDir("/cache", "/home/me/photos"));
  CHECK(a != pb::libraryCacheDir("/cache", "/home/me/other"));
  CHECK(a.parent_path() == std::filesystem::path("/cache/photobrowse"));
  CHECK(a.filename().string().size() == 16);
  CHECK_FALSE(pb::defaultCacheRoot().empty());
}

TEST_CASE("paths: concurrent atomic writes to one file never mix contents") {
  pbtest::TempDir dir;
  const auto p = dir.path / "same.bin";
  std::atomic<int> bad{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&, t] {
      const std::vector<uint8_t> data(size_t(t + 1) * 64 * 1024, uint8_t(t + 1));
      for (int i = 0; i < 200; ++i) {
        pb::atomicWriteFile(p, data);
        // Whatever is at `p` must always be one complete write.
        const auto got = pbtest::readFile(p.string());
        if (got.empty()) continue;
        const bool whole = got.size() == size_t(got[0]) * 64 * 1024 &&
                           std::all_of(got.begin(), got.end(), [&](uint8_t b) { return b == got[0]; });
        if (!whole) ++bad;
      }
    });
  }
  for (auto& th : threads) th.join();
  CHECK(bad == 0);
}

TEST_CASE("catalog: newest first is the exact reverse") {
  pb::Catalog c;
  c.photos = {rec("b.jpg", 100), rec("a.jpg", 100), rec("c.jpg", 50), rec("x.jpg", 10, pb::kBroken)};
  c.sortOrder(true);
  CHECK(c.order == std::vector<pb::PhotoId>{0, 1, 2});
  c.sortOrder(false);
  CHECK(c.order == std::vector<pb::PhotoId>{2, 1, 0});
}
