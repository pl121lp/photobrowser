#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "imaging/image.h"
#include "map/map_camera.h"
#include "map/tile_set.h"
#include "map/tile_source.h"
#include "stream/job_system.h"

namespace pb {

// GPU side of the map tiles (OpenGL in the app, faked in tests): one 4096x4096 page of
// 256-px slots, created ahead of need.
class MapTileSink {
 public:
  virtual ~MapTileSink() = default;
  virtual bool mapPageReady() const = 0;  // false: results wait
  virtual void uploadMapTile(Vec2d origin, const std::vector<Image>& mips) = 0;
};

// Fetches map tiles from a TileSource on its own small pool of threads (a slow or dead
// network never delays photo loading), uploads finished ones within a per-frame budget
// and reports what to draw. Main thread only, except the fetches.
class MapTiles {
 public:
  static constexpr size_t kUploadsPerFrame = 4;

  MapTiles(const TileSource& source, MapTileSink& sink, unsigned threads = 2);
  ~MapTiles();  // drops queued fetches, waits for running ones

  // Takes finished fetches (uploading at most kUploadsPerFrame), requests what the view
  // needs and fills `draws` (panel-relative).
  void frame(double now, const MapCamera& cam, std::vector<MapTileDraw>& draws);
  std::vector<TileStatus> takeResults();  // fetch outcomes since the last call
  void cancelAll();                       // map hidden: queued fetches dropped, results ignored
  size_t pending() const { return set_.pending() + waiting_.size(); }
  bool fading(double now) const { return set_.fading(now); }

 private:
  struct Result {
    TileKey key;
    TileStatus status;
    std::vector<Image> mips;
    uint64_t generation;
  };

  const TileSource& source_;
  MapTileSink& sink_;
  TileSet set_;
  std::shared_ptr<ResultQueue<Result>> results_ = std::make_shared<ResultQueue<Result>>();
  std::deque<Result> waiting_;  // fetched, not yet uploaded
  std::vector<TileStatus> statuses_;
  std::vector<TileRequest> requests_;
  uint64_t generation_ = 0;  // bumped by cancelAll: older results are dropped
  std::unique_ptr<JobSystem> jobs_;
};

}  // namespace pb
