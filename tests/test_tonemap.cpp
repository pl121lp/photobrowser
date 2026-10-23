#include <doctest/doctest.h>

#include <array>
#include <vector>
#include <cstdint>

#include "../plugins/video/tonemap.h"

using namespace pbvideo;

namespace {

// 10-bit code value as the 16-bit sample libswscale produces (RGB48).
uint16_t code16(double signal) { return uint16_t(signal * 1023 + 0.5) << 6; }

std::array<uint8_t, 4> mapOne(const HdrToSdr& t, double r, double g, double b) {
  const uint16_t in[3] = {code16(r), code16(g), code16(b)};
  std::array<uint8_t, 4> out{};
  t.mapRow(in, out.data(), 1);
  return out;
}

}  // namespace

TEST_CASE("tonemap: transfer functions") {
  CHECK(hlgToScene(0) == 0);
  CHECK(hlgToScene(0.5) == doctest::Approx(1.0 / 12));
  CHECK(hlgToScene(1) == doctest::Approx(1.0).epsilon(1e-6));
  // BT.2408: HLG reference white (75%) is 203 nits on a 1000-nit display.
  CHECK(1000 * std::pow(hlgToScene(0.75), 1.2) == doctest::Approx(203).epsilon(0.01));
  CHECK(pqToNits(0) == 0);
  CHECK(pqToNits(1) == doctest::Approx(10000));
  for (double n : {0.1, 1.0, 100.0, 203.0, 1000.0, 4000.0}) CHECK(pqToNits(nitsToPq(n)) == doctest::Approx(n));
}

TEST_CASE("tonemap: spline keeps black, lands the peak on SDR white, rises smoothly") {
  const double inMax = nitsToPq(1000), outMax = nitsToPq(203);
  CHECK(splinePq(0, inMax, outMax) == doctest::Approx(0).epsilon(1e-9));
  CHECK(splinePq(inMax, inMax, outMax) == doctest::Approx(outMax));
  CHECK(splinePq(inMax + 0.1, inMax, outMax) == doctest::Approx(outMax));  // above the peak: clipped
  double prev = -1;
  for (int i = 0; i <= 1000; ++i) {
    const double y = splinePq(inMax * i / 1000, inMax, outMax);
    CHECK(y >= prev);
    CHECK(y <= outMax + 1e-9);
    prev = y;
  }
  // Mid-tones come down (the point of the curve), but not crushed.
  const double mid = splinePq(nitsToPq(50), inMax, outMax);
  CHECK(pqToNits(mid) < 50);
  CHECK(pqToNits(mid) > 25);
}

TEST_CASE("tonemap: HLG greys stay neutral and brighten monotonically") {
  const HdrToSdr t(HdrTransfer::Hlg, true);
  CHECK(mapOne(t, 0, 0, 0) == std::array<uint8_t, 4>{0, 0, 0, 255});
  int prev = -1;
  for (int i = 0; i <= 100; ++i) {
    const auto px = mapOne(t, i / 100.0, i / 100.0, i / 100.0);
    CHECK(px[0] == px[1]);
    CHECK(px[1] == px[2]);
    CHECK(int(px[0]) >= prev);
    prev = px[0];
  }
  CHECK(prev >= 254);  // 100% HLG is white
  // Reference white (75%) is light grey, below white: headroom for highlights.
  const auto ref = mapOne(t, 0.75, 0.75, 0.75);
  CHECK(ref[0] > 150);
  CHECK(ref[0] < 190);
}

TEST_CASE("tonemap: bright saturated colours keep their hue instead of turning white") {
  const HdrToSdr t(HdrTransfer::Hlg, true);
  const auto sky = mapOne(t, 0.55, 0.75, 0.95);  // bright blue sky
  CHECK(sky[2] > sky[1]);
  CHECK(sky[1] > sky[0]);
  CHECK(int(sky[2]) - int(sky[0]) > 40);
  const auto red = mapOne(t, 1.0, 0.2, 0.2);  // out of the sRGB gamut and above white
  CHECK(red[0] == 255);
  CHECK(red[1] < 200);
}

TEST_CASE("tonemap: PQ") {
  const HdrToSdr t(HdrTransfer::Pq, true);
  CHECK(mapOne(t, 0, 0, 0) == std::array<uint8_t, 4>{0, 0, 0, 255});
  const auto white = mapOne(t, nitsToPq(1000), nitsToPq(1000), nitsToPq(1000));
  CHECK(white[0] >= 254);
  const auto ref = mapOne(t, nitsToPq(203), nitsToPq(203), nitsToPq(203));
  CHECK(ref[0] > 150);
  CHECK(ref[0] < 190);
  CHECK(ref[0] == ref[2]);
}

TEST_CASE("tonemap: a row can be written with a step (rotation), even backwards") {
  const HdrToSdr t(HdrTransfer::Hlg, true);
  std::vector<uint16_t> in;
  for (int i = 0; i < 300; ++i) {  // more than one internal chunk
    for (int c = 0; c < 3; ++c) in.push_back(code16(i / 299.0));
  }
  std::vector<uint8_t> row(300 * 4), every3(300 * 3 * 4, 7), back(300 * 4);
  t.mapRow(in.data(), row.data(), 300);
  t.mapRow(in.data(), every3.data(), 300, 3);
  t.mapRow(in.data(), back.data() + 299 * 4, 300, -1);
  for (int i = 0; i < 300; ++i) {
    CAPTURE(i);
    CHECK(every3[size_t(i) * 12] == row[size_t(i) * 4]);
    CHECK(every3[size_t(i) * 12 + 4] == 7);  // untouched between
    CHECK(back[size_t(299 - i) * 4] == row[size_t(i) * 4]);
  }
}
