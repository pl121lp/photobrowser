#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "imaging/image.h"

namespace pb {

// Largest accepted image (width * height); bounds a decode to about 512 MB of RGBA.
constexpr uint64_t kMaxPngPixels = 128'000'000;

// 8-bit RGBA; 16-bit and palette/grey sources are converted. nullopt if undecodable.
std::optional<Image> decodePng(std::span<const uint8_t> data);

}  // namespace pb
