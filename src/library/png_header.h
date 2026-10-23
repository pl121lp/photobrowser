#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "library/exif.h"

namespace pb {

struct PngHeaderInfo {
  uint32_t width = 0, height = 0;  // from IHDR, before orientation
  std::optional<ExifInfo> exif;    // eXIf chunk before the first IDAT
};

// Walks chunks from the signature up to the first IDAT. nullopt if the signature or
// IHDR is missing; truncation after IHDR yields what was found. CRCs are not checked.
std::optional<PngHeaderInfo> parsePngHeader(std::span<const uint8_t> head);

}  // namespace pb
