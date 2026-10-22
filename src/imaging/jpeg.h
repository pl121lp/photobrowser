#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "imaging/image.h"

namespace pb {

// Decodes to RGBA using the smallest DCT scale (1/8, 1/4, 1/2, 1) whose long edge
// is >= minLongEdge (0 = full size). Thread-safe.
std::optional<Image> decodeJpeg(std::span<const uint8_t> data, uint32_t minLongEdge = 0);

std::optional<std::vector<uint8_t>> encodeJpeg(const Image& img, int quality = 88);

}  // namespace pb
