#include "imaging/resize.h"

#include <algorithm>
#include <cmath>

namespace pb {

Image downscaleBox(const Image& src, uint32_t w, uint32_t h) {
  w = std::clamp(w, 1u, src.w);
  h = std::clamp(h, 1u, src.h);
  Image dst{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  const double sx = double(src.w) / w, sy = double(src.h) / h;
  for (uint32_t y = 0; y < h; ++y) {
    const double fy0 = y * sy, fy1 = (y + 1) * sy;
    const uint32_t y0 = uint32_t(fy0), y1 = std::min(src.h, uint32_t(std::ceil(fy1)));
    for (uint32_t x = 0; x < w; ++x) {
      const double fx0 = x * sx, fx1 = (x + 1) * sx;
      const uint32_t x0 = uint32_t(fx0), x1 = std::min(src.w, uint32_t(std::ceil(fx1)));
      double acc[4] = {0, 0, 0, 0}, total = 0;
      for (uint32_t yy = y0; yy < y1; ++yy) {
        const double wy = std::min(fy1, yy + 1.0) - std::max(fy0, double(yy));
        const uint8_t* row = &src.rgba[(size_t(yy) * src.w) * 4];
        for (uint32_t xx = x0; xx < x1; ++xx) {
          const double wgt = wy * (std::min(fx1, xx + 1.0) - std::max(fx0, double(xx)));
          const uint8_t* p = row + size_t(xx) * 4;
          for (int c = 0; c < 4; ++c) acc[c] += p[c] * wgt;
          total += wgt;
        }
      }
      uint8_t* d = &dst.rgba[(size_t(y) * w + x) * 4];
      for (int c = 0; c < 4; ++c) d[c] = uint8_t(std::lround(acc[c] / total));
    }
  }
  return dst;
}

Image fitToLongEdge(const Image& src, uint32_t longEdge) {
  const uint32_t lng = std::max(src.w, src.h);
  if (lng <= longEdge || src.empty()) return src;
  const double s = double(longEdge) / lng;
  return downscaleBox(src, std::max(1u, uint32_t(std::lround(src.w * s))),
                      std::max(1u, uint32_t(std::lround(src.h * s))));
}

Image applyOrientation(const Image& src, int o) {
  if (o < 2 || o > 8 || src.empty()) return src;
  const bool swap = o >= 5;
  Image dst;
  dst.w = swap ? src.h : src.w;
  dst.h = swap ? src.w : src.h;
  dst.rgba.resize(src.rgba.size());
  const auto* s = reinterpret_cast<const uint32_t*>(src.rgba.data());
  auto* d = reinterpret_cast<uint32_t*>(dst.rgba.data());
  const uint32_t W = src.w, H = src.h;
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      uint32_t dx = 0, dy = 0;
      switch (o) {
        case 2: dx = W - 1 - x; dy = y; break;          // mirror horizontal
        case 3: dx = W - 1 - x; dy = H - 1 - y; break;  // rotate 180
        case 4: dx = x; dy = H - 1 - y; break;          // mirror vertical
        case 5: dx = y; dy = x; break;                  // transpose
        case 6: dx = H - 1 - y; dy = x; break;          // rotate 90 cw
        case 7: dx = H - 1 - y; dy = W - 1 - x; break;  // transverse
        case 8: dx = y; dy = W - 1 - x; break;          // rotate 90 ccw
      }
      d[size_t(dy) * dst.w + dx] = s[size_t(y) * W + x];
    }
  }
  return dst;
}

std::vector<Image> buildMips(const Image& src) {
  std::vector<Image> mips{src};
  while (mips.back().w > 1 || mips.back().h > 1) {
    const Image& p = mips.back();
    mips.push_back(downscaleBox(p, std::max(1u, (p.w + 1) / 2), std::max(1u, (p.h + 1) / 2)));
  }
  return mips;
}

}  // namespace pb
