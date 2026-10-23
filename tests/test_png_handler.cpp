#include <doctest/doctest.h>

#include <filesystem>

#include "imaging/png.h"
#include "library/png_handler.h"
#include "test_util.h"

namespace {

void put16be(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}
void put32be(std::vector<uint8_t>& v, uint32_t x) {
  put16be(v, uint16_t(x >> 16));
  put16be(v, uint16_t(x));
}

// Big-endian TIFF with only IFD0 {Orientation=6}.
std::vector<uint8_t> tiffOrientation6() {
  std::vector<uint8_t> t = {'M', 'M', 0, 42};
  put32be(t, 8);
  put16be(t, 1);
  put16be(t, 0x0112);
  put16be(t, 3);
  put32be(t, 1);
  put32be(t, 6u << 16);
  put32be(t, 0);
  return t;
}

std::vector<uint8_t> pixels(uint32_t w, uint32_t h) {
  std::vector<uint8_t> p(size_t(w) * h * 4);
  for (size_t i = 0; i < p.size(); ++i) p[i] = uint8_t(i * 7 + 1);
  return p;
}

}  // namespace

TEST_CASE("png decode: synthetic RGBA round trip") {
  const auto px = pixels(3, 2);
  const auto img = pb::decodePng(pbtest::makePng(3, 2, px));
  REQUIRE(img);
  CHECK(img->w == 3);
  CHECK(img->h == 2);
  CHECK(img->rgba == px);
}

TEST_CASE("png decode: 16-bit Display P3 screenshot looks normal") {
  const auto img = pb::decodePng(pbtest::readFile(pbtest::photo("IMG_3590.PNG")));
  REQUIRE(img);
  CHECK(img->w == 1170);
  CHECK(img->h == 2532);
  double sum = 0;
  bool distinct = false;
  const size_t n = size_t(img->w) * img->h;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* p = &img->rgba[i * 4];
    sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
    if (p[0] != img->rgba[0] || p[1] != img->rgba[1] || p[2] != img->rgba[2]) distinct = true;
  }
  const double mean = sum / double(n);
  CHECK(mean > 20);
  CHECK(mean < 235);
  CHECK(distinct);
}

TEST_CASE("png decode: truncated file") {
  CHECK_FALSE(pb::decodePng(pbtest::readFile(pbtest::photo("IMG_3310.PNG"), 4000)));
}

TEST_CASE("png handler: probe of a screenshot") {
  pb::PngHandler h;
  const auto p = h.probe(pbtest::photo("IMG_3310.PNG"));
  REQUIRE(p);
  CHECK(p->width == 1170);
  CHECK(p->height == 2532);
  CHECK(p->takenAt == *pb::parseExifDateTime("2026:06:16 21:56:20"));
  CHECK_FALSE(p->gps);
  const auto q = h.probe(pbtest::photo("SBWF6980.PNG"));
  REQUIRE(q);
  CHECK(q->takenAt == 0);
}

TEST_CASE("png handler: orientation 6 is applied once") {
  pbtest::TempDir dir;
  const auto px = pixels(3, 2);
  const auto png = pbtest::makePng(3, 2, px, tiffOrientation6());
  const auto path = dir.path / "o6.png";
  pbtest::writeFile(path, std::string(png.begin(), png.end()));
  pb::PngHandler h;
  const auto p = h.probe(path);
  REQUIRE(p);
  CHECK(p->width == 2);
  CHECK(p->height == 3);
  const auto img = h.decodeStill(path, 0);
  REQUIRE(img);
  CHECK(img->w == 2);
  CHECK(img->h == 3);
  // 90 degrees clockwise: the top-right pixel is the source's top-left pixel.
  const size_t tr = size_t(img->w - 1) * 4;
  for (int c = 0; c < 4; ++c) CHECK(img->rgba[tr + c] == px[c]);
}

TEST_CASE("png handler: missing file") {
  pb::PngHandler h;
  CHECK_FALSE(h.probe("/nonexistent/none.png"));
  CHECK_FALSE(h.decodeStill("/nonexistent/none.png", 0));
}

namespace {
// A tiny valid PNG whose IHDR claims w x h (CRCs are not checked by the parser).
std::vector<uint8_t> pngClaiming(uint32_t w, uint32_t h) {
  auto png = pbtest::makePng(2, 2, pixels(2, 2));
  std::vector<uint8_t> dims;
  put32be(dims, w);
  put32be(dims, h);
  std::copy(dims.begin(), dims.end(), png.begin() + 16);
  return png;
}
}  // namespace

TEST_CASE("png handler: oversized and zero-sized IHDR are rejected") {
  pbtest::TempDir dir;
  pb::PngHandler h;
  const auto write = [&](const char* name, const std::vector<uint8_t>& png) {
    const auto path = dir.path / name;
    pbtest::writeFile(path, std::string(png.begin(), png.end()));
    return path;
  };
  const auto big = write("big.png", pngClaiming(20000, 20000));
  CHECK_FALSE(h.probe(big));
  CHECK_FALSE(h.decodeStill(big, 0));
  CHECK_FALSE(pb::decodePng(pngClaiming(20000, 20000)));
  const auto zero = write("zero.png", pngClaiming(0, 5));
  CHECK_FALSE(h.probe(zero));
  CHECK_FALSE(h.decodeStill(zero, 0));
}
