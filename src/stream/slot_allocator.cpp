#include "stream/slot_allocator.h"

namespace pb {

SlotAllocator::SlotAllocator(uint32_t slotSize) : slotSize_(slotSize), perRow_(kPageSize / slotSize) {}

uint32_t SlotAllocator::pageCount() const {
  uint32_t n = 0;
  for (const auto& p : pages_) n += p.alive;
  return n;
}

std::optional<SlotRef> SlotAllocator::allocate() {
  for (size_t i = 0; i < pages_.size(); ++i) {
    auto& p = pages_[i];
    if (p.alive && !p.free.empty()) {
      const uint16_t s = p.free.back();
      p.free.pop_back();
      return SlotRef{uint16_t(i), s};
    }
  }
  return std::nullopt;
}

uint16_t SlotAllocator::addPage() {
  size_t idx = 0;
  while (idx < pages_.size() && pages_[idx].alive) ++idx;
  if (idx == pages_.size()) pages_.emplace_back();
  auto& p = pages_[idx];
  p.alive = true;
  p.free.clear();
  // Reverse so allocation hands out slot 0 first (keeps pages compact).
  for (uint32_t s = slotsPerPage(); s-- > 0;) p.free.push_back(uint16_t(s));
  return uint16_t(idx);
}

void SlotAllocator::releasePage(uint16_t page) {
  if (pageAlive(page)) {
    pages_[page].alive = false;
    pages_[page].free.clear();
  }
}

void SlotAllocator::release(SlotRef s) {
  if (pageAlive(s.page)) pages_[s.page].free.push_back(s.slot);
}

bool SlotAllocator::pageEmpty(uint16_t page) const {
  return pageAlive(page) && pages_[page].free.size() == slotsPerPage();
}

Vec2d SlotAllocator::origin(SlotRef s) const {
  return {double(s.slot % perRow_) * slotSize_, double(s.slot / perRow_) * slotSize_};
}

UvRect SlotAllocator::uv(SlotRef s, uint32_t w, uint32_t h) const {
  const Vec2d o = origin(s);
  const float k = 1.0f / kPageSize;
  return {float(o.x + 0.5) * k, float(o.y + 0.5) * k, float(o.x + w - 0.5) * k, float(o.y + h - 0.5) * k};
}

}  // namespace pb
