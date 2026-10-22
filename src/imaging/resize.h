#pragma once

#include <cstdint>
#include <vector>

#include "imaging/image.h"

namespace pb {

// Area-average downscale to w x h (each must be <= the source dimension).
Image downscaleBox(const Image& src, uint32_t w, uint32_t h);
// Downscales so the long edge is `longEdge`, keeping aspect; never upscales.
Image fitToLongEdge(const Image& src, uint32_t longEdge);
// Applies EXIF orientation (1..8) so the result is upright.
Image applyOrientation(const Image& src, int exifOrientation);
// Mip chain: [0] = src, each next level halved (rounding up) down to 1x1.
std::vector<Image> buildMips(const Image& src);

}  // namespace pb
