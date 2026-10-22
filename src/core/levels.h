#pragma once

#include <cstdint>

namespace pb {

// Image pyramid: L0..L4 are fixed square slots (image fitted, aspect preserved);
// kFullLevel is decoded from the original at screen resolution.
constexpr int kLevelCount = 5;
constexpr uint32_t kSlotSize[kLevelCount] = {32, 128, 256, 512, 1024};
constexpr int kFullLevel = 5;
constexpr uint32_t kPageSize = 4096;

}  // namespace pb
