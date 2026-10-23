// HDR video (HLG or PQ) to SDR sRGB. The curve is libplacebo's default "spline" (what
// mpv shows), with a fixed knee instead of per-scene peak detection: phone HDR video is
// exposed with much of the picture above reference white, so mapping that white
// straight to SDR white (BT.2390 style) looks too bright and turns skies white.
// Tone-mapped on luminance, with RGB scaled together, so bright colours keep their hue.
// Header-only so the tests can include it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pbvideo {

constexpr double kSdrWhiteNits = 203;  // BT.2408 reference white: SDR white on output
constexpr double kHdrPeakNits = 1000;  // HLG's nominal display; assumed for PQ too

// HLG inverse OETF: signal [0, 1] -> scene light [0, 1].
inline double hlgToScene(double e) {
  constexpr double a = 0.17883277, b = 0.28466892, c = 0.55991073;
  return e <= 0.5 ? e * e / 3.0 : (std::exp((e - c) / a) + b) / 12.0;
}

namespace detail {
constexpr double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
}  // namespace detail

// PQ (SMPTE ST 2084) EOTF and its inverse: signal [0, 1] <-> nits [0, 10000].
inline double pqToNits(double e) {
  using namespace detail;
  const double p = std::pow(std::clamp(e, 0.0, 1.0), 1.0 / m2);
  return 10000.0 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}
inline double nitsToPq(double nits) {
  using namespace detail;
  const double y = std::pow(std::clamp(nits / 10000.0, 0.0, 1.0), m1);
  return std::pow((c1 + c2 * y) / (1 + c3 * y), m2);
}

// libplacebo's spline tone curve in the PQ domain, black at 0: maps [0, inMax] onto
// [0, outMax]. Shadows and mid-tones are pivoted down around a knee at 40% of the input
// range (a quadratic below, a cubic above that lands on outMax with zero curvature).
inline double splinePq(double x, double inMax, double outMax) {
  constexpr double kKnee = 0.4, kAdaptation = 0.4, kSlopeTuning = 1.5, kSlopeOffset = 0.2, kContrast = 0.5;
  const double srcKnee = kKnee * inMax;
  const double dstKnee = std::min(srcKnee, (1 - kAdaptation) * srcKnee + kAdaptation * kKnee * outMax);
  const double ratio = std::clamp(kSlopeTuning * (inMax / outMax - 1), kSlopeOffset, 1 + kSlopeOffset);
  const double slope = std::pow(dstKnee / srcKnee, (1 - kContrast) * ratio);
  const double inLo = -srcKnee, inHi = inMax - srcKnee, outLo = -dstKnee, outHi = outMax - dstKnee;
  const double pa = (outLo - slope * inLo) / (inLo * inLo);
  const double t = 2 * inHi * inHi;
  const double qa = (slope * inHi - outHi) / (inHi * t), qb = -3 * (slope * inHi - outHi) / t;
  const double d = std::clamp(x, 0.0, inMax) - srcKnee;
  return (d > 0 ? ((qa * d + qb) * d + slope) * d : (pa * d + slope) * d) + dstKnee;
}

enum class HdrTransfer { Hlg, Pq };

// Lookup tables for one transfer function and set of primaries; map() is per pixel.
class HdrToSdr {
 public:
  // `bt2020`: BT.2020 primaries (converted to BT.709 / sRGB), else already BT.709.
  HdrToSdr(HdrTransfer trc, bool bt2020) : bt2020_(bt2020) {
    toLinear_.resize(kInLut);
    for (size_t i = 0; i < kInLut; ++i) {
      const double e = double(i) / double(kInLut - 1);
      // HLG: scene light; PQ: display light relative to the assumed peak.
      toLinear_[i] = float(trc == HdrTransfer::Hlg ? hlgToScene(e) : pqToNits(e) / kHdrPeakNits);
    }
    // Gain on RGB for a pixel of luminance y, from the tone-mapped display luminance.
    const double inMax = nitsToPq(kHdrPeakNits), outMax = nitsToPq(kSdrWhiteNits);
    gain_.resize(kGainLut);
    for (size_t i = 1; i < kGainLut; ++i) {
      const double y = double(i) / double(kGainLut - 1);
      // HLG's OOTF (system gamma 1.2 at 1000 nits) on luminance, as BT.2100 specifies.
      const double nits = trc == HdrTransfer::Hlg ? kHdrPeakNits * std::pow(y, 1.2) : y * kHdrPeakNits;
      const double mapped = pqToNits(splinePq(nitsToPq(nits), inMax, outMax));
      gain_[i] = float(mapped / kSdrWhiteNits / y);
    }
    gain_[0] = gain_[1];
    toSrgb_.resize(kOutLut);
    for (size_t i = 0; i < kOutLut; ++i) {
      const double v = double(i) / double(kOutLut - 1);
      const double s = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
      toSrgb_[i] = uint8_t(std::lround(std::clamp(s, 0.0, 1.0) * 255));
    }
  }

  // n pixels of 16-bit RGB (libswscale's RGB48, full range) -> RGBA8 sRGB, written
  // `step` pixels apart (1 = a row; a rotated picture uses another step, even negative).
  // Runs for every pixel of every frame, so it works in chunks: the table lookups are
  // scalar, the arithmetic in between is in plain loops the compiler vectorizes.
  void mapRow(const uint16_t* in, uint8_t* out, size_t n, ptrdiff_t step = 1) const {
    constexpr size_t kChunk = 256;
    alignas(64) float c0[kChunk], c1[kChunk], c2[kChunk], y[kChunk], k[kChunk];
    alignas(64) int32_t idx[kChunk];
    const float* lin = toLinear_.data();
    const float* gain = gain_.data();
    const uint8_t* srgb = toSrgb_.data();
    for (size_t done = 0; done < n; done += kChunk, in += 3 * kChunk, out += 4 * step * ptrdiff_t(kChunk)) {
      const size_t m = std::min(kChunk, n - done);
      for (size_t i = 0; i < m; ++i) {
        c0[i] = lin[in[3 * i] >> 6];
        c1[i] = lin[in[3 * i + 1] >> 6];
        c2[i] = lin[in[3 * i + 2] >> 6];
      }
      if (bt2020_) {
        for (size_t i = 0; i < m; ++i) {
          const float r = c0[i], g = c1[i], b = c2[i];
          y[i] = 0.2627f * r + 0.6780f * g + 0.0593f * b;
          // BT.2087 BT.2020 -> BT.709, full precision so greys stay grey (rows sum to 1).
          c0[i] = 1.660491f * r - 0.587641f * g - 0.072850f * b;
          c1[i] = -0.124550f * r + 1.132900f * g - 0.008349f * b;
          c2[i] = -0.018151f * r - 0.100579f * g + 1.118730f * b;
        }
      } else {
        for (size_t i = 0; i < m; ++i) y[i] = 0.2126f * c0[i] + 0.7152f * c1[i] + 0.0722f * c2[i];
      }
      for (size_t i = 0; i < m; ++i) idx[i] = int32_t(std::min(y[i], 1.0f) * float(kGainLut - 1) + 0.5f);
      for (size_t i = 0; i < m; ++i) k[i] = gain[idx[i]];
      for (size_t i = 0; i < m; ++i) {
        const float g = k[i], yy = std::min(y[i] * g, 1.0f);
        const float r = std::max(c0[i] * g, 0.0f), gg = std::max(c1[i] * g, 0.0f), b = std::max(c2[i] * g, 0.0f);
        // Brighter than white or outside sRGB: pull towards grey until the top channel
        // fits, which keeps the hue (clipping each channel would shift it).
        const float mx = std::max(std::max(r, gg), b);
        const float s = mx > 1 ? (1 - yy) / std::max(mx - yy, 1e-6f) : 1.0f;
        constexpr float kScale = float(kOutLut - 1);
        c0[i] = std::min(std::max(yy + (r - yy) * s, 0.0f), 1.0f) * kScale + 0.5f;
        c1[i] = std::min(std::max(yy + (gg - yy) * s, 0.0f), 1.0f) * kScale + 0.5f;
        c2[i] = std::min(std::max(yy + (b - yy) * s, 0.0f), 1.0f) * kScale + 0.5f;
      }
      uint8_t* o = out;
      for (size_t i = 0; i < m; ++i, o += 4 * step) {
        o[0] = srgb[int32_t(c0[i])];
        o[1] = srgb[int32_t(c1[i])];
        o[2] = srgb[int32_t(c2[i])];
        o[3] = 255;
      }
    }
  }

 private:
  static constexpr size_t kInLut = 1024;    // 10-bit signal
  static constexpr size_t kGainLut = 65536;
  static constexpr size_t kOutLut = 65536;

  bool bt2020_;
  std::vector<float> toLinear_, gain_;
  std::vector<uint8_t> toSrgb_;
};

}  // namespace pbvideo
