#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <filesystem>
#include <optional>
#include <vector>

#include "cache/l0_pack.h"
#include "cache/thumb_cache.h"
#include "layout/layout.h"
#include "library/catalog.h"
#include "stream/job_system.h"
#include "stream/residency.h"
#include "view/camera.h"

namespace pb {

// GPU-side operations the streamer needs (implemented with OpenGL in the app,
// faked in tests). Level 0..4 pages are 4096x4096 atlases; full images are separate.
class TextureSink {
 public:
  virtual ~TextureSink() = default;
  virtual void addPage(int level, uint16_t page) = 0;
  virtual void releasePage(int level, uint16_t page) = 0;
  virtual void upload(int level, uint16_t page, Vec2d origin, const std::vector<Image>& mips) = 0;
  virtual void uploadFull(int index, const std::vector<Image>& mips) = 0;
};

// Texture region to sample. level -1 = placeholder (nothing loaded yet);
// level kFullLevel = full-resolution texture number `page`.
struct TexRef {
  int level = -1;
  uint16_t page = 0;
  UvRect uv;
};

struct TileDraw {
  uint32_t pos = 0;  // layout position
  PhotoId id = 0;
  TexRef cur, prev;  // prev = what was shown before cur arrived (cross-fade)
  float fade = 1;    // weight of cur
};

struct StreamStats {
  size_t pendingJobs = 0;
  size_t pendingRequests = 0;
  size_t vramBytes = 0;
  size_t uploadsThisFrame = 0;
};

// Per frame: picks the pyramid level each visible photo needs, requests loads on the
// job system, uploads finished images within a per-frame byte budget, and reports
// what to draw (with cross-fades). Main thread only.
class Streamer {
 public:
  static constexpr int kFullSlots = 3;

  Streamer(const Catalog& catalog, std::filesystem::path photoRoot, const ThumbCache& thumbs, JobSystem& jobs,
           TextureSink& sink, size_t vramBudgetBytes);
  ~Streamer();

  void setL0(const L0Entries& entries);  // uploads the always-resident 32 px level
  void setUploadBudget(size_t bytesPerFrame) { uploadBudget_ = bytesPerFrame; }
  void frame(double now, const Camera& cam, const Layout& layout, std::optional<uint32_t> focusedPos,
             std::vector<TileDraw>& out);
  StreamStats stats() const;

 private:
  // Lets queued jobs outlive the Streamer safely: they bail out once alive is false,
  // and the destructor waits for running ones.
  struct JobGuard {
    std::atomic<bool> alive{true};
    std::atomic<int> running{0};
  };
  struct Result {
    PhotoId id;
    int level;
    std::vector<Image> mips;  // empty = failed
  };
  struct Full {
    bool used = false;
    bool loading = false;
    PhotoId id = 0;
    uint32_t w = 0, h = 0, longEdge = 0;
    double lastUsed = 0;
    std::shared_ptr<CancelToken> cancel;
  };
  struct Shown {
    int8_t level = -1;
    int8_t from = -1;
    float since = -1e9f;
  };

  void submit(const Residency::Request& r);
  std::optional<TexRef> fullFor(PhotoId id, double now, uint32_t longEdge, bool request);
  void applyPageEvents();
  TexRef refFor(PhotoId id, int level);
  TexRef l0Ref(PhotoId id) const;

  const Catalog& catalog_;
  std::filesystem::path root_;
  const ThumbCache& thumbs_;
  JobSystem& jobs_;
  TextureSink& sink_;
  Residency residency_;
  SlotAllocator l0Slots_{kSlotSize[0]};
  std::vector<std::pair<uint16_t, uint16_t>> l0Dims_;  // per PhotoId, (0,0) = no L0
  std::vector<int8_t> desired_;
  std::vector<Shown> shown_;
  std::array<Full, kFullSlots> full_;
  std::shared_ptr<ResultQueue<Result>> results_ = std::make_shared<ResultQueue<Result>>();
  std::shared_ptr<JobGuard> guard_ = std::make_shared<JobGuard>();
  std::vector<Residency::Request> requests_;
  std::vector<uint32_t> visible_;
  size_t uploadBudget_ = 4u << 20;
  size_t uploadsThisFrame_ = 0;
};

}  // namespace pb
