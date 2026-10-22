#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include "imaging/image.h"

namespace pb {

using L0Entries = std::vector<std::pair<uint64_t, Image>>;  // (cache key, image <= 32x32)

// One file holding every photo's 32 px thumbnail: magic, count, then per entry
// key u64, w u16, h u16 and a 32x32 RGBA block (image in the top-left), then END!.
bool writeL0Pack(const std::filesystem::path& path, const L0Entries& entries);
std::optional<L0Entries> readL0Pack(const std::filesystem::path& path);

}  // namespace pb
