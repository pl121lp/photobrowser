#include <doctest/doctest.h>

#include "library/exif.h"
#include "test_util.h"

namespace {

std::optional<pb::JpegHeaderInfo> parse(const std::string& name, size_t bytes = 262144) {
  auto head = pbtest::readFile(pbtest::photo(name), bytes);
  return pb::parseJpegHeader(head);
}

void put16be(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}
void put32be(std::vector<uint8_t>& v, uint32_t x) {
  put16be(v, uint16_t(x >> 16));
  put16be(v, uint16_t(x));
}

// Minimal big-endian JPEG: SOI, APP1 Exif with IFD0 {Orientation=6}, SOF0 640x480, SOS.
std::vector<uint8_t> syntheticBigEndianJpeg() {
  std::vector<uint8_t> tiff = {'M', 'M', 0, 42};
  put32be(tiff, 8);       // IFD0 offset
  put16be(tiff, 1);       // one entry
  put16be(tiff, 0x0112);  // Orientation
  put16be(tiff, 3);       // SHORT
  put32be(tiff, 1);
  put16be(tiff, 6);
  put16be(tiff, 0);
  put32be(tiff, 0);  // no next IFD
  std::vector<uint8_t> j = {0xFF, 0xD8, 0xFF, 0xE1};
  put16be(j, uint16_t(2 + 6 + tiff.size()));
  for (char c : std::string("Exif\0\0", 6)) j.push_back(uint8_t(c));
  j.insert(j.end(), tiff.begin(), tiff.end());
  j.insert(j.end(), {0xFF, 0xC0});
  put16be(j, 17);
  j.push_back(8);
  put16be(j, 480);
  put16be(j, 640);
  j.insert(j.end(), 12, 0);
  j.insert(j.end(), {0xFF, 0xDA, 0x00, 0x02});
  return j;
}

void putRational(std::vector<uint8_t>& v, uint32_t num, uint32_t den) {
  put32be(v, num);
  put32be(v, den);
}

void putEntry(std::vector<uint8_t>& v, uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
  put16be(v, tag);
  put16be(v, type);
  put32be(v, count);
  put32be(v, value);
}

// Big-endian JPEG whose IFD0 points to a GPS IFD. `coords`: with latitude/longitude
// (33 deg 52' 30" S, 151 deg 12' 36" W, 122.5 m below sea level); else only the version tag.
std::vector<uint8_t> syntheticGpsJpeg(bool coords) {
  std::vector<uint8_t> tiff = {'M', 'M', 0, 42};
  put32be(tiff, 8);  // IFD0 at 8
  put16be(tiff, 1);
  putEntry(tiff, 0x8825, 4, 1, 26);  // GPS IFD at 26
  put32be(tiff, 0);
  if (coords) {
    const uint32_t data = 26 + 2 + 6 * 12 + 4;  // after the GPS IFD
    put16be(tiff, 6);
    putEntry(tiff, 1, 2, 2, uint32_t('S') << 24);  // LatitudeRef, inline ASCII
    putEntry(tiff, 2, 5, 3, data);                 // Latitude
    putEntry(tiff, 3, 2, 2, uint32_t('W') << 24);  // LongitudeRef
    putEntry(tiff, 4, 5, 3, data + 24);            // Longitude
    putEntry(tiff, 5, 1, 1, 1u << 24);             // AltitudeRef: below sea level
    putEntry(tiff, 6, 5, 1, data + 48);            // Altitude
    put32be(tiff, 0);
    putRational(tiff, 33, 1);
    putRational(tiff, 52, 1);
    putRational(tiff, 3000, 100);
    putRational(tiff, 151, 1);
    putRational(tiff, 12, 1);
    putRational(tiff, 36, 1);
    putRational(tiff, 1225, 10);
  } else {
    put16be(tiff, 1);
    putEntry(tiff, 0, 1, 4, 0x02020000);  // GPSVersionID only
    put32be(tiff, 0);
  }
  std::vector<uint8_t> j = {0xFF, 0xD8, 0xFF, 0xE1};
  put16be(j, uint16_t(2 + 6 + tiff.size()));
  for (char c : std::string("Exif\0\0", 6)) j.push_back(uint8_t(c));
  j.insert(j.end(), tiff.begin(), tiff.end());
  j.insert(j.end(), {0xFF, 0xDA, 0x00, 0x02});
  return j;
}

}  // namespace

TEST_CASE("exif: pixel photo") {
  auto i = parse("PXL_20260321_003010085.jpg");
  REQUIRE(i);
  REQUIRE(i->dateTimeOriginal);
  CHECK(*i->dateTimeOriginal == "2026:03:20 20:30:10");
  CHECK(i->orientation == 1);
  CHECK(i->sawSof);
  CHECK(i->width == 3072);
  CHECK(i->height == 4080);
  CHECK(i->thumbOffset == 1310);
  CHECK(i->thumbLength == 23268);
  REQUIRE(i->gps);
  CHECK(i->gps->lat == doctest::Approx(40.752153).epsilon(1e-6));
  CHECK(i->gps->lon == doctest::Approx(-73.98535).epsilon(1e-6));
  REQUIRE(i->gps->altitude);
  CHECK(*i->gps->altitude == doctest::Approx(64.0));
}

TEST_CASE("exif: SOF can lie beyond the first 64 KB") {
  // Pixel photos carry ~40 KB of XMP before the frame header.
  auto i = parse("PXL_20260321_003010085.jpg", 65536);
  REQUIRE(i);
  CHECK(i->dateTimeOriginal);
  CHECK(i->thumbLength == 23268);
  CHECK_FALSE(i->sawSof);
}

TEST_CASE("exif: edited copy has no embedded preview") {
  auto i = parse("PXL_20260321_012354015~2.jpg");
  REQUIRE(i);
  CHECK(i->thumbLength == 0);
  CHECK(i->width == 3048);
  CHECK(i->height == 3418);
}

TEST_CASE("exif: datetime parse") {
  CHECK(*pb::parseExifDateTime("1970:01:02 00:00:01") == 86401);
  CHECK(*pb::parseExifDateTime("2026:03:20 20:30:10") == 1774038610);
  CHECK_FALSE(pb::parseExifDateTime("0000:00:00 00:00:00"));
  CHECK_FALSE(pb::parseExifDateTime("garbage"));
  CHECK_FALSE(pb::parseExifDateTime("2026:13:20 20:30:10"));
}

TEST_CASE("exif: truncated and garbage input") {
  auto full = pbtest::readFile(pbtest::photo("PXL_20260321_003010085.jpg"), 65536);
  for (size_t n : {0, 1, 2, 3, 10, 100, 500, 1400, 30000}) {
    CHECK_NOTHROW(pb::parseJpegHeader({full.data(), n}));
  }
  auto cut = pb::parseJpegHeader({full.data(), 1400});
  REQUIRE(cut);
  CHECK_FALSE(cut->sawSof);
  std::vector<uint8_t> junk(4096, 0xFF);
  CHECK_NOTHROW(pb::parseJpegHeader(junk));
  std::vector<uint8_t> notJpeg{'P', 'N', 'G'};
  CHECK_FALSE(pb::parseJpegHeader(notJpeg));
  // Corrupt every byte of the EXIF block one at a time: must never crash.
  for (size_t k = 4; k < 1400; k += 7) {
    auto bad = full;
    bad[k] ^= 0xA5;
    CHECK_NOTHROW(pb::parseJpegHeader(bad));
  }
}

TEST_CASE("exif: big-endian and orientation 6") {
  auto j = syntheticBigEndianJpeg();
  auto i = pb::parseJpegHeader(j);
  REQUIRE(i);
  CHECK(i->orientation == 6);
  CHECK(i->width == 640);
  CHECK(i->height == 480);
  CHECK_FALSE(i->dateTimeOriginal);
}

TEST_CASE("exif: GPS position, hemispheres and altitude below sea level") {
  auto j = syntheticGpsJpeg(true);
  auto i = pb::parseJpegHeader(j);
  REQUIRE(i);
  REQUIRE(i->gps);
  CHECK(i->gps->lat == doctest::Approx(-33.875));
  CHECK(i->gps->lon == doctest::Approx(-151.21));
  REQUIRE(i->gps->altitude);
  CHECK(*i->gps->altitude == doctest::Approx(-122.5));
}

TEST_CASE("exif: GPS block without coordinates is no position") {
  auto j = syntheticGpsJpeg(false);
  auto i = pb::parseJpegHeader(j);
  REQUIRE(i);
  CHECK_FALSE(i->gps);
  CHECK_FALSE(pb::parseJpegHeader(syntheticBigEndianJpeg())->gps);
}
