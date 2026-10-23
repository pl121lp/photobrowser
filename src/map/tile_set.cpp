#include "map/tile_set.h"

#include <algorithm>
#include <cmath>

namespace pb {

namespace {

constexpr int kBackstopLevel = 2;
constexpr int kBackstopUp = 3;  // second backstop: this many levels above the visible one

UvRect compose(const UvRect& outer, const UvRect& inner) {
  const float w = outer.u1 - outer.u0, h = outer.v1 - outer.v0;
  return {outer.u0 + w * inner.u0, outer.v0 + h * inner.v0, outer.u0 + w * inner.u1, outer.v0 + h * inner.v1};
}

}  // namespace

TileSet::TileSet(int minZ, int maxZ) : minZ_(minZ), maxZ_(maxZ) { slots_.addPage(); }

TileSet::Entry& TileSet::entry(TileKey k) {
  Entry& e = entries_[k.packed()];
  e.key = k;
  return e;
}

void TileSet::want(TileKey k, int64_t priority, double now, std::vector<TileRequest>& out) {
  Entry& e = entry(k);
  e.lastUsed = frame_;
  if (e.state == State::Retry && now >= e.retryAt) e.state = State::None;
  if (e.state != State::None) return;
  e.state = State::Requested;
  e.cancel = std::make_shared<CancelToken>();
  ++pending_;
  out.push_back({k, priority, e.cancel});
}

const TileSet::Entry* TileSet::ancestor(TileKey k, UvRect& sub) {
  for (int dz = 1; dz <= k.z; ++dz) {
    auto it = entries_.find(TileKey{k.z - dz, k.x >> dz, k.y >> dz}.packed());
    if (it == entries_.end() || it->second.state != State::Resident) continue;
    it->second.lastUsed = frame_;
    const int n = 1 << dz;
    const float size = 1.0f / float(n);
    const float u = float(k.x % n) * size, v = float(k.y % n) * size;
    sub = {u, v, u + size, v + size};
    return &it->second;
  }
  return nullptr;
}

void TileSet::frame(const MapCamera& cam, double now, std::vector<MapTileDraw>& draws,
                    std::vector<TileRequest>& requests) {
  ++frame_;
  const size_t firstRequest = requests.size();
  const Rect view = cam.visibleWorld();
  const int level = tileLevel(cam.zoom, cam.uiScale, minZ_, maxZ_);

  // Backstops first: a few coarse tiles that cover the whole view.
  if (level > kBackstopLevel) {
    for (const TileCover& c : tilesCovering(view, std::max(kBackstopLevel, minZ_))) want(c.key, 0, now, requests);
  }
  if (level - kBackstopUp > kBackstopLevel) {
    for (const TileCover& c : tilesCovering(view, level - kBackstopUp)) want(c.key, int64_t(1) << 32, now, requests);
  }

  const Vec2d mid = cam.viewport * 0.5;
  for (const TileCover& c : tilesCovering(view, level)) {
    const Vec2d a = cam.worldToScreen({c.world.x0, c.world.y0}), b = cam.worldToScreen({c.world.x1, c.world.y1});
    const Vec2d d = (a + b) * 0.5 - mid;
    want(c.key, (int64_t(2) << 32) + int64_t(std::llround(d.x * d.x + d.y * d.y)), now, requests);

    MapTileDraw draw{{a.x, a.y, b.x, b.y}, {}, {}, 1};
    UvRect sub;
    const Entry& e = entry(c.key);
    const Entry* up = ancestor(c.key, sub);
    if (e.state == State::Resident) {
      draw.uv = slotUv(e.slot);
      draw.fade = float(std::clamp((now - e.loadedAt) / kFadeS, 0.0, 1.0));
      if (draw.fade < 1 && up) {
        draw.uvPrev = compose(slotUv(up->slot), sub);
        fadeUntil_ = std::max(fadeUntil_, e.loadedAt + kFadeS);
      } else {
        draw.uvPrev = draw.uv;
        draw.fade = 1;
      }
    } else if (up) {
      draw.uv = draw.uvPrev = compose(slotUv(up->slot), sub);
    } else {
      continue;  // nothing to show yet
    }
    draws.push_back(draw);
  }

  // Cancel what is no longer wanted.
  for (auto& [key, e] : entries_) {
    if (e.state == State::Requested && e.lastUsed != frame_) {
      e.cancel->cancelled = true;
      e.cancel.reset();
      e.state = State::None;
      --pending_;
    }
  }
  std::stable_sort(requests.begin() + long(firstRequest), requests.end(),
                   [](const TileRequest& a, const TileRequest& b) { return a.priority < b.priority; });
}

std::optional<SlotRef> TileSet::loaded(TileKey k, double now) {
  Entry& e = entry(k);
  if (e.state == State::Resident) return std::nullopt;
  if (e.state == State::Requested) --pending_;
  e.cancel.reset();
  std::optional<SlotRef> slot = slots_.allocate();
  if (!slot) {
    // Evict the least recently used tile not drawn this frame (deeper levels first on ties).
    Entry* victim = nullptr;
    for (auto& [key, other] : entries_) {
      if (other.state != State::Resident || other.lastUsed == frame_) continue;
      if (!victim || other.lastUsed < victim->lastUsed ||
          (other.lastUsed == victim->lastUsed && other.key.z > victim->key.z)) {
        victim = &other;
      }
    }
    if (!victim) {
      e.state = State::Retry;  // every slot is on screen: try again a little later
      e.retryAt = now + 1;
      return std::nullopt;
    }
    victim->state = State::None;
    slot = victim->slot;
  }
  e.state = State::Resident;
  e.slot = *slot;
  e.loadedAt = now;
  e.lastUsed = frame_;
  return slot;
}

void TileSet::failed(TileKey k, TileStatus s, double now) {
  Entry& e = entry(k);
  if (e.state == State::Resident) return;
  if (e.state == State::Requested) --pending_;
  e.cancel.reset();
  switch (s) {
    case TileStatus::Offline:
      e.state = State::Retry;
      e.retryAt = now + kOfflineRetryS;
      break;
    case TileStatus::Refused:
      e.state = State::Retry;
      e.retryAt = now + kRefusedRetryS;
      break;
    default: e.state = State::Failed; break;
  }
}

void TileSet::cancelAll() {
  for (auto& [key, e] : entries_) {
    if (e.state != State::Requested) continue;
    e.cancel->cancelled = true;
    e.cancel.reset();
    e.state = State::None;
  }
  pending_ = 0;
}

}  // namespace pb
