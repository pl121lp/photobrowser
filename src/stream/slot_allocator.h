#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/geom.h"
#include "core/levels.h"

namespace pb {

struct SlotRef {
  uint16_t page = 0;
  uint16_t slot = 0;
  friend bool operator==(SlotRef, SlotRef) = default;
};

// GPU bytes of one 4096x4096 RGBA8 page including its mip chain.
constexpr size_t kPageBytes = size_t(kPageSize) * kPageSize * 4 * 4 / 3;

// Hands out fixed-size square slots within 4096x4096 pages. Page indices are stable;
// released page indices are reused by later addPage() calls.
class SlotAllocator {
 public:
  explicit SlotAllocator(uint32_t slotSize);

  uint32_t slotSize() const { return slotSize_; }
  uint32_t slotsPerPage() const { return perRow_ * perRow_; }
  uint32_t pageCount() const;  // live pages
  std::optional<SlotRef> allocate();
  uint16_t addPage();
  void releasePage(uint16_t page);  // page must be empty
  void release(SlotRef s);
  bool pageEmpty(uint16_t page) const;
  bool pageAlive(uint16_t page) const { return page < pages_.size() && pages_[page].alive; }
  size_t pageSlots() const { return pages_.size(); }  // highest page index + 1
  Vec2d origin(SlotRef s) const;                     // texel position of the slot
  UvRect uv(SlotRef s, uint32_t w, uint32_t h) const;  // content rect, half-texel inset

 private:
  struct Page {
    bool alive = false;
    std::vector<uint16_t> free;
  };
  uint32_t slotSize_, perRow_;
  std::vector<Page> pages_;
};

}  // namespace pb
