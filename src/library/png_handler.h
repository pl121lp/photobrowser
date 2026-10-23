#pragma once

#include "library/media_format.h"

namespace pb {

inline constexpr const char* kPngOrigin = "core png 1";

// Built-in handler for .png files. Stateless, thread-safe.
class PngHandler final : public MediaHandler {
 public:
  std::optional<MediaProbe> probe(const std::filesystem::path& abs) const override;
  // Upright (eXIf orientation applied), natural size; the indexer resizes.
  std::optional<Image> decodeStill(const std::filesystem::path& abs, uint32_t minLongEdge) const override;
};

}  // namespace pb
