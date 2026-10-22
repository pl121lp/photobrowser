#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/levels.h"
#include "library/catalog.h"
#include "stream/job_system.h"
#include "stream/slot_allocator.h"

namespace pb {

// Level needed to show an image whose stored long edge must supply `neededPx`
// on-screen pixels. Allows ~1.4x upsampling; hysteresis avoids flicker at boundaries.
int desiredLevel(double neededPx, int current);

struct DrawSource {
  int level = 0;
  SlotRef slot;
  uint32_t w = 0, h = 0;  // image size inside the slot
};

// Decides which pyramid levels (1..4) are resident on the GPU for which photos,
// within a VRAM budget. Level 0 is always resident and not managed here; the full
// level is managed by the caller. Single-threaded (main thread).
class Residency {
 public:
  struct Request {
    PhotoId id;
    int level;
    int64_t priority;  // lower = sooner: coarser levels first, then nearer the center
    std::shared_ptr<CancelToken> cancel;
  };
  struct PageEvent {
    int level;
    uint16_t page;
    bool added;  // false = released
  };

  Residency(uint32_t photoCount, size_t budgetBytes);

  void beginFrame();
  // Marks the photo visible this frame; returns the best resident source (nullopt =
  // use L0) and appends a request if the desired level isn't resident or pending.
  std::optional<DrawSource> touch(PhotoId id, int desired, double distancePx, std::vector<Request>& out);
  // Resident source at exactly `level` (touched so it stays resident), if any.
  std::optional<DrawSource> resident(PhotoId id, int level);
  // A load finished: assign a slot (adding a page within budget or evicting the least
  // recently used photo not visible this frame). nullopt = drop the result.
  std::optional<SlotRef> place(PhotoId id, int level, uint32_t w, uint32_t h);
  void requestFailed(PhotoId id, int level);
  void endFrame();
  // Evicts entries not touched for `frames` frames and releases pages left empty.
  void releaseIdle(uint64_t frames);

  std::vector<PageEvent> takePageEvents();
  size_t bytesUsed() const { return pagesInUse_ * kPageBytes; }
  size_t pendingRequests() const { return pending_; }
  uint32_t pageCount(int level) const { return pools_[level - 1].pageCount(); }
  const SlotAllocator& pool(int level) const { return pools_[level - 1]; }

 private:
  enum class State : uint8_t { None, Requested, Resident, Failed };
  struct Entry {
    State state = State::None;
    SlotRef slot;
    uint16_t w = 0, h = 0;
    uint64_t lastTouched = 0;
    std::shared_ptr<CancelToken> cancel;
  };
  static constexpr int kManaged = kLevelCount - 1;  // levels 1..4

  Entry& at(PhotoId id, int level) { return entries_[size_t(id) * kManaged + size_t(level - 1)]; }
  void evict(PhotoId id, int level);
  bool tryAddPage(int level);

  std::vector<Entry> entries_;
  std::array<SlotAllocator, kManaged> pools_;
  std::array<std::vector<PhotoId>, kManaged> residentIds_;
  std::array<uint64_t, kManaged> blockedUntil_{};
  std::vector<PhotoId> requested_;
  std::vector<PageEvent> events_;
  size_t budgetPages_, pagesInUse_ = 0, pending_ = 0;
  uint64_t frame_ = 1;
  bool pressure_ = false;
};

}  // namespace pb
