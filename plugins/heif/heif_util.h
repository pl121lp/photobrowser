// Helpers of the heif plugin that need neither libheif nor the host (unit-tested
// directly, see tests/test_heif.cpp).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace pbheif {

// The TIFF block inside a HEIF "Exif" item: skips the 4-byte big-endian offset (ISO
// 23008-12) and checks for "II*\0"/"MM\0*". Empty span if invalid.
std::span<const uint8_t> exifTiffFromItem(std::span<const uint8_t> item);
// Index of the smallest thumbnail whose long edge >= minLongEdge; -1 = decode the primary.
int chooseThumbnail(std::span<const std::pair<uint32_t, uint32_t>> thumbSizes, uint32_t minLongEdge);
// Whole file via std::ifstream on fs::path(u8 string), so UTF-8 paths work on Windows.
std::optional<std::vector<uint8_t>> readFileUtf8(const char* pathUtf8);

}  // namespace pbheif
