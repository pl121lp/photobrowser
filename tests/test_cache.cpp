#include <doctest/doctest.h>

#include <chrono>

#include "cache/l0_pack.h"
#include "cache/thumb_cache.h"
#include "test_util.h"

namespace {
pb::Image img(uint32_t w, uint32_t h, uint8_t v) { return {w, h, std::vector<uint8_t>(size_t(w) * h * 4, v)}; }
}  // namespace

TEST_CASE("l0pack: round trip") {
  pbtest::TempDir dir;
  std::vector<std::pair<uint64_t, pb::Image>> in = {{42, img(24, 32, 7)}, {7, img(32, 18, 9)}};
  REQUIRE(pb::writeL0Pack(dir.path / "l0.pack", in));
  auto out = pb::readL0Pack(dir.path / "l0.pack");
  REQUIRE(out);
  REQUIRE(out->size() == 2);
  CHECK((*out)[0].first == 42);
  CHECK((*out)[0].second.w == 24);
  CHECK((*out)[0].second.h == 32);
  CHECK((*out)[0].second.rgba == in[0].second.rgba);
  CHECK((*out)[1].second.rgba == in[1].second.rgba);
}

TEST_CASE("l0pack: truncated") {
  pbtest::TempDir dir;
  REQUIRE(pb::writeL0Pack(dir.path / "l0.pack", {{1, img(32, 32, 1)}}));
  auto bytes = pbtest::readFile((dir.path / "l0.pack").string());
  pbtest::writeFile(dir.path / "cut.pack", std::string(bytes.begin(), bytes.end() - 1));
  CHECK_FALSE(pb::readL0Pack(dir.path / "cut.pack"));
  CHECK_FALSE(pb::readL0Pack(dir.path / "missing.pack"));
}

TEST_CASE("thumbcache: write/read and sweep removes oldest lazy files only") {
  pbtest::TempDir dir;
  pb::ThumbCache cache(dir.path);
  CHECK(cache.levelPath(2, 0xABCDEF0123456789ULL) == dir.path / "l2" / "ab" / "abcdef0123456789.jpg");
  REQUIRE(cache.write(1, 1, img(96, 128, 50)));
  for (uint64_t k : {10, 11, 12}) REQUIRE(cache.write(2, k, img(192, 256, 80)));
  auto l = cache.read(2, 10);
  REQUIRE(l);
  CHECK(l->w == 192);
  CHECK_FALSE(cache.read(3, 10));

  // Age the files: 10 oldest, then 11, then 12; the L1 file is oldest of all.
  using namespace std::chrono_literals;
  const auto now = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(cache.levelPath(1, 1), now - 10h);
  std::filesystem::last_write_time(cache.levelPath(2, 10), now - 3h);
  std::filesystem::last_write_time(cache.levelPath(2, 11), now - 2h);
  std::filesystem::last_write_time(cache.levelPath(2, 12), now - 1h);
  const auto one = std::filesystem::file_size(cache.levelPath(2, 12));
  const auto freed = cache.sweep(one * 2 + 1);
  CHECK(freed > 0);
  CHECK_FALSE(std::filesystem::exists(cache.levelPath(2, 10)));
  CHECK(std::filesystem::exists(cache.levelPath(2, 11)));
  CHECK(std::filesystem::exists(cache.levelPath(2, 12)));
  CHECK(std::filesystem::exists(cache.levelPath(1, 1)));
}
