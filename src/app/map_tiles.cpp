#include "app/map_tiles.h"

#include "imaging/resize.h"

namespace pb {

MapTiles::MapTiles(const TileSource& source, MapTileSink& sink, unsigned threads)
    : source_(source),
      sink_(sink),
      set_(source.minZoom(), source.maxZoom()),
      jobs_(std::make_unique<JobSystem>(threads)) {}

MapTiles::~MapTiles() { jobs_.reset(); }

void MapTiles::frame(double now, const MapCamera& cam, std::vector<MapTileDraw>& draws) {
  for (Result& r : results_->drain()) {
    if (r.generation != generation_) continue;
    statuses_.push_back(r.status);
    if (r.status == TileStatus::Ok) {
      waiting_.push_back(std::move(r));
    } else {
      set_.failed(r.key, r.status, now);
    }
  }
  if (sink_.mapPageReady()) {
    for (size_t n = 0; n < kUploadsPerFrame && !waiting_.empty(); ++n) {
      const Result& r = waiting_.front();
      if (const auto slot = set_.loaded(r.key, now)) sink_.uploadMapTile(set_.slotOrigin(*slot), r.mips);
      waiting_.pop_front();
    }
  }

  requests_.clear();
  set_.frame(cam, now, draws, requests_);
  for (TileRequest& req : requests_) {
    jobs_->submit(req.priority, [src = &source_, results = results_, key = req.key, cancel = std::move(req.cancel),
                                 generation = generation_] {
      if (cancel->cancelled) return;
      TileResult t = src->get(key.z, key.x, key.y);
      Result r{key, t.status, {}, generation};
      if (t.status == TileStatus::Ok) r.mips = buildMips(t.image);
      results->push(std::move(r));
    });
  }
}

std::vector<TileStatus> MapTiles::takeResults() { return std::exchange(statuses_, {}); }

void MapTiles::cancelAll() {
  jobs_->clearPending();
  set_.cancelAll();
  ++generation_;
  waiting_.clear();
  statuses_.clear();
}

}  // namespace pb
