#include "library/scanner.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "platform/paths.h"

namespace pb {

namespace fs = std::filesystem;

namespace {
bool isJpegExt(const fs::path& p) {
  std::string e = toUtf8(p.extension());
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return e == ".jpg" || e == ".jpeg";
}
}  // namespace

std::vector<ScanEntry> scanFolder(const fs::path& root) {
  std::vector<ScanEntry> out;
  std::error_code ec;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    const auto& entry = *it;
    std::error_code e2;
    if (!entry.is_regular_file(e2) || !isJpegExt(entry.path())) continue;
    const auto size = entry.file_size(e2);
    if (e2) continue;
    const auto mt = entry.last_write_time(e2);
    if (e2) continue;
    out.push_back({toUtf8(entry.path().lexically_relative(root)), size, toUnixSeconds(mt)});
  }
  std::sort(out.begin(), out.end(), [](const ScanEntry& a, const ScanEntry& b) { return a.relPath < b.relPath; });
  return out;
}

ScanDiff diffCatalog(const Catalog& catalog, const std::vector<ScanEntry>& scan) {
  ScanDiff d;
  std::unordered_map<std::string_view, const ScanEntry*> byPath;
  for (const auto& e : scan) byPath.emplace(e.relPath, &e);
  std::unordered_map<std::string_view, PhotoId> known;
  for (PhotoId id = 0; id < catalog.photos.size(); ++id) {
    const auto& p = catalog.photos[id];
    known.emplace(p.relPath, id);
    auto it = byPath.find(p.relPath);
    if (it == byPath.end()) {
      d.removed.push_back(id);
    } else if (it->second->size != p.fileSize || it->second->mtime != p.mtime) {
      d.changed.push_back(id);
    }
  }
  for (const auto& e : scan) {
    if (!known.contains(e.relPath)) d.added.push_back(e);
  }
  return d;
}

}  // namespace pb
