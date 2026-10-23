#include <doctest/doctest.h>

#include "library/png_header.h"
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
void putEntry(std::vector<uint8_t>& v, uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
  put16be(v, tag);
  put16be(v, type);
  put32be(v, count);
  put32be(v, value);
}

// Big-endian TIFF: IFD0 {Orientation=6, ExifIFD ptr}, ExifIFD {DateTimeOriginal}.
std::vector<uint8_t> tiffOrientation6AndDate() {
  const std::string date = "2026:06:16 21:56:20";  // 19 chars + NUL
  std::vector<uint8_t> t = {'M', 'M', 0, 42};
  put32be(t, 8);
  put16be(t, 2);
  putEntry(t, 0x0112, 3, 1, 6u << 16);
  putEntry(t, 0x8769, 4, 1, 38);  // 8 + 2 + 2*12 + 4
  put32be(t, 0);
  put16be(t, 1);
  putEntry(t, 0x9003, 2, 20, 38 + 2 + 12 + 4);
  put32be(t, 0);
  for (char c : date) t.push_back(uint8_t(c));
  t.push_back(0);
  return t;
}

std::vector<uint8_t> pixels(uint32_t w, uint32_t h) {
  std::vector<uint8_t> p(size_t(w) * h * 4);
  for (size_t i = 0; i < p.size(); ++i) p[i] = uint8_t(i * 7);
  return p;
}

std::optional<pb::PngHeaderInfo> fixture(const char* name) {
  return pb::parsePngHeader(pbtest::readFile(pbtest::photo(name), 65536));
}

}  // namespace

TEST_CASE("png header: size and eXIf from a synthetic file") {
  auto png = pbtest::makePng(3, 2, pixels(3, 2), tiffOrientation6AndDate());
  auto info = pb::parsePngHeader(png);
  REQUIRE(info);
  CHECK(info->width == 3);
  CHECK(info->height == 2);
  REQUIRE(info->exif);
  CHECK(info->exif->orientation == 6);
  CHECK(info->exif->dateTimeOriginal == "2026:06:16 21:56:20");
}

TEST_CASE("png header: no eXIf") {
  auto info = pb::parsePngHeader(pbtest::makePng(3, 2, pixels(3, 2)));
  REQUIRE(info);
  CHECK_FALSE(info->exif);
}

TEST_CASE("png header: eXIf after IDAT is ignored") {
  auto png = pbtest::makePng(3, 2, pixels(3, 2));
  // Splice an eXIf chunk (CRC is not checked) before the 12-byte IEND chunk.
  auto tiff = tiffOrientation6AndDate();
  std::vector<uint8_t> chunk;
  put32be(chunk, uint32_t(tiff.size()));
  chunk.insert(chunk.end(), {'e', 'X', 'I', 'f'});
  chunk.insert(chunk.end(), tiff.begin(), tiff.end());
  put32be(chunk, 0);
  png.insert(png.end() - 12, chunk.begin(), chunk.end());
  auto info = pb::parsePngHeader(png);
  REQUIRE(info);
  CHECK(info->width == 3);
  CHECK_FALSE(info->exif);
}

TEST_CASE("png header: bad signature or missing IHDR") {
  CHECK_FALSE(pb::parsePngHeader(std::vector<uint8_t>{0xFF, 0xD8, 0xFF, 0xE0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  CHECK_FALSE(pb::parsePngHeader(std::vector<uint8_t>{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}));
}

TEST_CASE("png header: truncated after IHDR") {
  auto png = pbtest::makePng(3, 2, pixels(3, 2), tiffOrientation6AndDate());
  png.resize(33);
  auto info = pb::parsePngHeader(png);
  REQUIRE(info);
  CHECK(info->width == 3);
  CHECK(info->height == 2);
  CHECK_FALSE(info->exif);
}

TEST_CASE("png header: iPhone screenshots") {
  auto a = fixture("IMG_3310.PNG");
  REQUIRE(a);
  CHECK(a->width == 1170);
  CHECK(a->height == 2532);
  REQUIRE(a->exif);
  CHECK(a->exif->dateTimeOriginal == "2026:06:16 21:56:20");
  auto b = fixture("IMG_3590.PNG");
  REQUIRE(b);
  REQUIRE(b->exif);
  CHECK(b->exif->dateTimeOriginal == "2026:06:20 10:54:09");
  auto c = fixture("SBWF6980.PNG");
  REQUIRE(c);
  CHECK(c->width == 1024);
  CHECK(c->height == 1536);
  CHECK_FALSE(c->exif);
}
