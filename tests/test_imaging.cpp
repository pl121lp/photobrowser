#include <doctest/doctest.h>

#include "imaging/jpeg.h"
#include "imaging/resize.h"
#include "test_util.h"

namespace {
pb::Image solid(uint32_t w, uint32_t h, uint8_t v) {
  pb::Image i{w, h, std::vector<uint8_t>(size_t(w) * h * 4, v)};
  return i;
}
uint8_t r(const pb::Image& i, uint32_t x, uint32_t y) { return i.rgba[(size_t(y) * i.w + x) * 4]; }
}  // namespace

TEST_CASE("jpeg: scaled decode picks smallest sufficient scale") {
  auto d = pbtest::readFile(pbtest::photo("PXL_20260321_003010085.jpg"));
  auto i = pb::decodeJpeg(d, 500);
  REQUIRE(i);
  CHECK(i->w == 384);
  CHECK(i->h == 510);  // 1/8
  CHECK(i->rgba.size() == size_t(384) * 510 * 4);
  auto j = pb::decodeJpeg(d, 1000);
  REQUIRE(j);
  CHECK(j->h == 1020);  // 1/4
  CHECK_FALSE(pb::decodeJpeg(std::vector<uint8_t>{1, 2, 3}));
}

TEST_CASE("jpeg: embedded preview decodes") {
  auto d = pbtest::readFile(pbtest::photo("PXL_20260321_003010085.jpg"), 30000);
  auto i = pb::decodeJpeg(std::span(d).subspan(1310, 23268));
  REQUIRE(i);
  CHECK(i->w == 382);
  CHECK(i->h == 507);
}

TEST_CASE("jpeg: encode round trip") {
  auto src = solid(64, 48, 200);
  auto bytes = pb::encodeJpeg(src, 88);
  REQUIRE(bytes);
  auto back = pb::decodeJpeg(*bytes);
  REQUIRE(back);
  CHECK(back->w == 64);
  CHECK(back->h == 48);
  CHECK(int(r(*back, 10, 10)) == doctest::Approx(200).epsilon(0.02));
}

TEST_CASE("resize: box average of 2x2 block") {
  pb::Image i = solid(2, 2, 0);
  for (int p : {1, 2}) i.rgba[size_t(p) * 4] = 255;
  auto o = pb::downscaleBox(i, 1, 1);
  CHECK(o.w == 1);
  CHECK(int(r(o, 0, 0)) == doctest::Approx(127.5).epsilon(0.01));
}

TEST_CASE("resize: fit to long edge keeps aspect") {
  auto o = pb::fitToLongEdge(solid(384, 510, 9), 128);
  CHECK(o.w == 96);
  CHECK(o.h == 128);
  auto same = pb::fitToLongEdge(solid(20, 10, 9), 128);
  CHECK(same.w == 20);
}

TEST_CASE("resize: orientation 6 rotates 90 cw") {
  pb::Image i = solid(2, 1, 0);
  i.rgba[4] = 255;  // right pixel
  auto o = pb::applyOrientation(i, 6);
  CHECK(o.w == 1);
  CHECK(o.h == 2);
  CHECK(r(o, 0, 1) == 255);  // right of a landscape row ends up at the bottom
  auto e = pb::applyOrientation(i, 8);
  CHECK(r(e, 0, 0) == 255);
  auto t = pb::applyOrientation(i, 3);
  CHECK(r(t, 0, 0) == 255);
  CHECK(pb::applyOrientation(i, 1).rgba == i.rgba);
}

TEST_CASE("resize: mips go to 1x1") {
  auto m = pb::buildMips(solid(100, 40, 1));
  REQUIRE(m.size() == 8);  // 100,50,25,13,7,4,2,1
  CHECK(m[1].w == 50);
  CHECK(m[1].h == 20);
  CHECK(m.back().w == 1);
  CHECK(m.back().h == 1);
}
