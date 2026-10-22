#pragma once

#include <cstdint>
#include <vector>

namespace pb {

// Tightly packed 8-bit RGBA pixels, top row first.
struct Image {
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
  bool empty() const { return w == 0 || h == 0; }
};

}  // namespace pb
