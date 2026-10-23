#pragma once

#include <array>
#include <filesystem>
#include <optional>

#include "core/levels.h"
#include "imaging/image.h"
#include "library/catalog.h"
#include "library/media_format.h"
#include "library/scanner.h"

namespace pb {

struct QuickResult {
  PhotoRecord rec;
  Image l0, l1;  // empty when kNeedsDecodeL1 or kBroken is set
};

// JPEG: reads only the start of the file: EXIF date/orientation/size and the embedded
// preview, which yields L0 (32 px) and L1 (128 px). Other formats in `formats` are
// probed and their still image decoded at L1 size by their handler. Never throws.
QuickResult quickIndex(const std::filesystem::path& root, const ScanEntry& e,
                       const MediaFormats& formats = MediaFormats::none());

// In the functions below, `handler` is the file's MediaHandler (nullptr = JPEG).

// Fallback for photos without a usable preview: 1/8-scale decode of the original.
// Fills missing width/height and clears kNeedsDecodeL1. False if undecodable.
bool decodeL0L1FromOriginal(const std::filesystem::path& abs, PhotoRecord& rec, Image& l0, Image& l1,
                            const MediaHandler* handler = nullptr);

// One decode of the original produces levels 2..highestLevel (others left empty).
std::array<Image, kLevelCount> buildLazyLevels(const std::filesystem::path& abs, const PhotoRecord& rec,
                                               int highestLevel, const MediaHandler* handler = nullptr);

// Upright image whose long edge is at least minLongEdge (or full size).
std::optional<Image> decodeFull(const std::filesystem::path& abs, const PhotoRecord& rec, uint32_t minLongEdge,
                                const MediaHandler* handler = nullptr);

}  // namespace pb
