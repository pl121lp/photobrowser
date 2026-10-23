#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

#include "cache/l0_pack.h"
#include "cache/thumb_cache.h"
#include "library/catalog.h"
#include "library/media_format.h"
#include "library/scanner.h"
#include "stream/job_system.h"

namespace pb {

struct LibraryState {
  Catalog catalog;
  L0Entries l0;  // keyed by PhotoRecord::key
};

// Catalog + L0 pack from a previous run, if both are present and valid and the
// catalog was indexed with the same set of formats (plugins).
std::optional<LibraryState> loadWarm(const std::filesystem::path& libDir,
                                     const MediaFormats& formats = MediaFormats::none());

// Indexes one file: quick pass (embedded preview), falling back to a 1/8 decode.
// Writes the L1 thumbnail to the cache. Returns the record and its L0 image.
std::pair<PhotoRecord, Image> indexOne(const std::filesystem::path& root, const ScanEntry& e, const ThumbCache& thumbs,
                                       const MediaFormats& formats = MediaFormats::none());

// First-run indexing on the job system: scan, then one job per photo. When finished,
// the sorted catalog and L0 pack are saved to libDir and available via take().
class Indexer {
 public:
  Indexer(std::filesystem::path root, std::filesystem::path libDir, JobSystem& jobs, const ThumbCache& thumbs,
          const MediaFormats& formats = MediaFormats::none());
  ~Indexer();  // abandons queued work and waits for running jobs
  void start();
  uint32_t total() const { return total_; }
  uint32_t done() const { return done_; }
  bool scanned() const { return scanned_; }
  bool finished() const { return finished_; }
  LibraryState take();

 private:
  void finish();

  std::filesystem::path root_, libDir_;
  JobSystem& jobs_;
  const ThumbCache& thumbs_;
  const MediaFormats& formats_;
  std::mutex m_;
  std::vector<std::pair<PhotoRecord, Image>> results_;
  LibraryState state_;
  std::atomic<uint32_t> total_{0}, done_{0};
  std::atomic<int> outstanding_{0};
  std::atomic<bool> scanned_{false}, finished_{false}, cancelled_{false};
};

// Re-scans the folder; if anything was added, removed or changed, indexes the new
// files and saves an updated catalog + L0 pack for the next launch. Returns true if
// the saved library changed. Runs synchronously (call it from a worker); returns
// early without saving once *stop becomes true.
bool refreshLibrary(const std::filesystem::path& root, const std::filesystem::path& libDir,
                    const LibraryState& current, const ThumbCache& thumbs,
                    const std::atomic<bool>* stop = nullptr, const MediaFormats& formats = MediaFormats::none());

}  // namespace pb
