#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "library/catalog.h"
#include "library/media_format.h"

namespace pb {

struct ScanEntry {
  std::string relPath;  // UTF-8, '/' separators
  uint64_t size = 0;
  int64_t mtime = 0;
};

// Recursive walk for .jpg/.jpeg and the extensions in `formats` (any case), sorted by
// relPath. Unreadable entries skipped.
std::vector<ScanEntry> scanFolder(const std::filesystem::path& root,
                                  const MediaFormats& formats = MediaFormats::none());

struct ScanDiff {
  std::vector<ScanEntry> added;
  std::vector<PhotoId> removed;
  std::vector<PhotoId> changed;  // same path, different size or mtime
  bool empty() const { return added.empty() && removed.empty() && changed.empty(); }
};

ScanDiff diffCatalog(const Catalog& catalog, const std::vector<ScanEntry>& scan);

}  // namespace pb
