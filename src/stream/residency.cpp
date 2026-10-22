#include "stream/residency.h"

#include <algorithm>
#include <utility>

namespace pb {

namespace {
constexpr double kUpsample = 1.4;
constexpr double kHysteresis = 0.8;
constexpr uint64_t kStaleFrames = 30;
constexpr uint64_t kBlockFrames = 60;
constexpr uint64_t kIdleFrames = 30;      // under pressure, reclaim entries unseen for 0.5 s
constexpr uint64_t kProtectFrames = 2;    // never evict what was visible this or last frame
}  // namespace

int desiredLevel(double neededPx, int current) {
  int level = kFullLevel;
  for (int l = 0; l < kLevelCount; ++l) {
    if (kSlotSize[l] * kUpsample >= neededPx) {
      level = l;
      break;
    }
  }
  if (current > level && current >= 1 && current <= kFullLevel &&
      neededPx > kSlotSize[current - 1] * kUpsample * kHysteresis) {
    return current;
  }
  return level;
}

Residency::Residency(uint32_t photoCount, size_t budgetBytes)
    : entries_(size_t(photoCount) * kManaged),
      pools_{SlotAllocator(kSlotSize[1]), SlotAllocator(kSlotSize[2]), SlotAllocator(kSlotSize[3]),
             SlotAllocator(kSlotSize[4])},
      budgetPages_(budgetBytes / kPageBytes) {}

void Residency::beginFrame() {
  ++frame_;
  pressure_ = false;
}

std::optional<DrawSource> Residency::touch(PhotoId id, int desired, double distancePx, std::vector<Request>& out) {
  desired = std::min(desired, kLevelCount - 1);
  // A level that just ran out of budget is not requested for photos lacking it.
  while (desired >= 1 && blockedUntil_[desired - 1] > frame_ && at(id, desired).state != State::Resident) {
    --desired;
  }
  if (desired < 1) return std::nullopt;

  Entry& want = at(id, desired);
  want.lastTouched = frame_;
  if (want.state == State::None) {
    want.state = State::Requested;
    want.cancel = std::make_shared<CancelToken>();
    ++pending_;
    requested_.push_back(id * kManaged + uint32_t(desired - 1));
    out.push_back({id, desired, int64_t(desired) * 1'000'000 + int64_t(distancePx), want.cancel});
  }

  auto source = [&](int l) -> std::optional<DrawSource> {
    Entry& e = at(id, l);
    if (e.state != State::Resident) return std::nullopt;
    e.lastTouched = frame_;
    return DrawSource{l, e.slot, e.w, e.h};
  };
  // Prefer the desired level, then sharper ones, then the best coarser one.
  for (int l = desired; l < kLevelCount; ++l) {
    if (auto s = source(l)) return s;
  }
  for (int l = desired - 1; l >= 1; --l) {
    if (auto s = source(l)) return s;
  }
  return std::nullopt;
}

std::optional<DrawSource> Residency::resident(PhotoId id, int level) {
  if (level < 1 || level >= kLevelCount) return std::nullopt;
  Entry& e = at(id, level);
  if (e.state != State::Resident) return std::nullopt;
  e.lastTouched = frame_;
  return DrawSource{level, e.slot, e.w, e.h};
}

bool Residency::tryAddPage(int level) {
  if (pagesInUse_ + 1 > budgetPages_) return false;
  const uint16_t page = pools_[level - 1].addPage();
  ++pagesInUse_;
  events_.push_back({level, page, true});
  return true;
}

void Residency::evict(PhotoId id, int level) {
  Entry& e = at(id, level);
  pools_[level - 1].release(e.slot);
  e.state = State::None;
  auto& ids = residentIds_[level - 1];
  ids.erase(std::find(ids.begin(), ids.end(), id));
}

std::optional<SlotRef> Residency::place(PhotoId id, int level, uint32_t w, uint32_t h) {
  if (level < 1 || level >= kLevelCount) return std::nullopt;
  Entry& e = at(id, level);
  if (e.state != State::Requested) return std::nullopt;  // cancelled or stale
  --pending_;
  e.cancel.reset();
  auto& pool = pools_[level - 1];

  std::optional<SlotRef> slot = pool.allocate();
  if (!slot && tryAddPage(level)) slot = pool.allocate();
  if (!slot) {
    // Evict the least recently used photo of this level that is not on screen.
    // Results are placed before this frame's touches, so "visible" means touched
    // this frame or the previous one.
    auto& ids = residentIds_[level - 1];
    PhotoId victim = 0;
    uint64_t oldest = frame_;
    for (PhotoId other : ids) {
      const uint64_t t = at(other, level).lastTouched;
      if (t < oldest) {
        oldest = t;
        victim = other;
      }
    }
    if (oldest + kProtectFrames <= frame_) {
      evict(victim, level);
      slot = pool.allocate();
    }
  }
  if (!slot) {
    e.state = State::None;
    blockedUntil_[level - 1] = frame_ + kBlockFrames;
    pressure_ = true;
    return std::nullopt;
  }
  e.state = State::Resident;
  e.slot = *slot;
  e.w = uint16_t(w);
  e.h = uint16_t(h);
  e.lastTouched = frame_;
  residentIds_[level - 1].push_back(id);
  return slot;
}

void Residency::requestFailed(PhotoId id, int level) {
  Entry& e = at(id, level);
  if (e.state == State::Requested) --pending_;
  e.state = State::Failed;
  e.cancel.reset();
}

void Residency::endFrame() {
  // Cancel requests for photos that have not been visible for a while.
  std::erase_if(requested_, [&](uint32_t idx) {
    Entry& e = entries_[idx];
    if (e.state != State::Requested) return true;
    if (frame_ - e.lastTouched < kStaleFrames) return false;
    if (e.cancel) e.cancel->cancelled = true;
    e.cancel.reset();
    e.state = State::None;
    --pending_;
    return true;
  });
  if (pressure_) releaseIdle(kIdleFrames);
}

void Residency::releaseIdle(uint64_t frames) {
  for (int level = 1; level < kLevelCount; ++level) {
    auto ids = residentIds_[level - 1];
    for (PhotoId id : ids) {
      if (frame_ - at(id, level).lastTouched >= frames) evict(id, level);
    }
    auto& pool = pools_[level - 1];
    for (size_t p = 0; p < pool.pageSlots(); ++p) {
      if (pool.pageEmpty(uint16_t(p))) {
        pool.releasePage(uint16_t(p));
        --pagesInUse_;
        events_.push_back({level, uint16_t(p), false});
      }
    }
  }
}

std::vector<Residency::PageEvent> Residency::takePageEvents() { return std::exchange(events_, {}); }

}  // namespace pb
