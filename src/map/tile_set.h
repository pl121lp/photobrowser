#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/geom.h"
#include "map/map_camera.h"
#include "map/mercator.h"
#include "map/tile_source.h"
#include "stream/job_system.h"
#include "stream/slot_allocator.h"

namespace pb {

struct MapTileDraw {
  Rect screen;        // where the wanted tile goes, panel-relative px
  UvRect uv, uvPrev;  // in the map page: the tile (or an ancestor's part), and what it fades in over
  float fade = 1;     // weight of uv over uvPrev
};

struct TileRequest {
  TileKey key;
  int64_t priority;  // lower = sooner
  std::shared_ptr<CancelToken> cancel;
};

// Which map tiles a view needs, which are loaded (in the 256 slots of one 4096x4096
// page), and what to draw meanwhile: the nearest loaded ancestor's matching part,
// scaled up. Requests the visible tiles plus coarse "backstop" ancestors (level 2 and
// three levels up) so something is on screen quickly. Single-threaded (main thread).
class TileSet {
 public:
  static constexpr double kFadeS = 0.15, kOfflineRetryS = 5, kRefusedRetryS = 60;

  TileSet(int minZ, int maxZ);

  // Plans one frame: what to draw and which tiles to request (sorted, soonest first).
  // Requested tiles no longer wanted are cancelled.
  void frame(const MapCamera& cam, double now, std::vector<MapTileDraw>& draws, std::vector<TileRequest>& requests);
  // A tile arrived: its slot (evicting the least recently used tile not drawn this
  // frame), or nullopt to drop it.
  std::optional<SlotRef> loaded(TileKey k, double now);
  void failed(TileKey k, TileStatus s, double now);
  void cancelAll();
  size_t pending() const { return pending_; }  // requested, not yet loaded or failed
  bool fading(double now) const { return now < fadeUntil_; }
  Vec2d slotOrigin(SlotRef s) const { return slots_.origin(s); }

 private:
  enum class State : uint8_t { None, Requested, Resident, Failed, Retry };
  struct Entry {
    TileKey key;
    State state = State::None;
    SlotRef slot;
    uint64_t lastUsed = 0;
    double loadedAt = 0, retryAt = 0;
    std::shared_ptr<CancelToken> cancel;
  };

  Entry& entry(TileKey k);
  void want(TileKey k, int64_t priority, double now, std::vector<TileRequest>& out);
  // Nearest resident strict ancestor of k and the part of it that k covers.
  const Entry* ancestor(TileKey k, UvRect& sub);
  UvRect slotUv(SlotRef s) const { return slots_.uv(s, 256, 256); }

  int minZ_, maxZ_;
  SlotAllocator slots_{256};
  std::unordered_map<uint64_t, Entry> entries_;
  uint64_t frame_ = 0;
  size_t pending_ = 0;
  double fadeUntil_ = 0;
};

}  // namespace pb
