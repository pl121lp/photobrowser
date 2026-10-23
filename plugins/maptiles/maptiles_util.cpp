#include "maptiles_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace pbmaps {

namespace fs = std::filesystem;

namespace {

void replaceAll(std::string& s, std::string_view from, std::string_view to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) {
    s.replace(at, from.size(), to);
  }
}

uint64_t fnv1a(std::string_view s) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (const char c : s) h = (h ^ uint8_t(c)) * 0x100000001b3ULL;
  return h;
}

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return out;
}

}  // namespace

std::string expandUrl(std::string_view tmpl, int z, int x, int y, std::string_view key) {
  std::string s(tmpl);
  replaceAll(s, "{z}", std::to_string(z));
  replaceAll(s, "{x}", std::to_string(x));
  replaceAll(s, "{y}", std::to_string(y));
  replaceAll(s, "{key}", key);
  return s;
}

std::string fileExtension(std::string_view tmpl) {
  std::string_view path = tmpl.substr(0, tmpl.find_first_of("?#"));
  const size_t slash = path.rfind('/'), dot = path.rfind('.');
  if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return "img";
  std::string ext = lower(path.substr(dot + 1));
  const bool ok = !ext.empty() && ext.size() <= 5 &&
                  std::all_of(ext.begin(), ext.end(), [](unsigned char c) { return std::isalnum(c); });
  return ok ? ext : "img";
}

fs::path tilePath(const fs::path& cacheDir, std::string_view tmpl, int z, int x, int y) {
  char hash[17];
  std::snprintf(hash, sizeof hash, "%016llx", static_cast<unsigned long long>(fnv1a(tmpl)));
  return cacheDir / "maptiles" / hash / std::to_string(z) / std::to_string(x) /
         (std::to_string(y) + "." + fileExtension(tmpl));
}

std::optional<int64_t> maxAgeSeconds(std::string_view headers) {
  const std::string h = lower(headers);
  for (size_t line = 0; line < h.size();) {
    const size_t end = std::min(h.find('\n', line), h.size());
    const std::string_view l = std::string_view(h).substr(line, end - line);
    line = end + 1;
    if (!l.starts_with("cache-control:")) continue;
    const size_t at = l.find("max-age=");
    if (at == std::string_view::npos) continue;
    const std::string digits(l.substr(at + 8));
    char* stop = nullptr;
    const long long v = std::strtoll(digits.c_str(), &stop, 10);
    if (stop != digits.c_str() && v >= 0) return int64_t(v);
  }
  return std::nullopt;
}

int64_t expiryFor(std::optional<int64_t> maxAge, int64_t now) { return now + maxAge.value_or(7 * 24 * 3600); }

PbTileResult mapTransfer(CURLcode code, long httpStatus) {
  switch (code) {
    case CURLE_OK: break;
    case CURLE_FILE_COULDNT_READ_FILE: return PB_TILE_MISSING;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_CONNECT:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_GOT_NOTHING: return PB_TILE_OFFLINE;
    default: return PB_TILE_ERROR;
  }
  if (httpStatus == 0 || httpStatus == 200) return PB_TILE_OK;
  if (httpStatus == 404) return PB_TILE_MISSING;
  if (httpStatus == 403 || httpStatus == 429) return PB_TILE_REFUSED;
  return PB_TILE_ERROR;
}

void trimCache(const fs::path& dir, uint64_t maxBytes) {
  struct File {
    fs::path path;
    fs::file_time_type time;
    uint64_t size;
  };
  std::vector<File> files;
  uint64_t total = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() == ".exp") continue;  // sidecars go with their tile
    File f{it->path(), it->last_write_time(ec), it->file_size(ec)};
    if (ec) continue;
    total += f.size;
    files.push_back(std::move(f));
  }
  if (total <= maxBytes) return;
  std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.time < b.time; });
  for (const File& f : files) {
    if (total <= maxBytes) break;
    if (fs::remove(f.path, ec)) total -= f.size;
    fs::remove(fs::path(f.path).concat(".exp"), ec);
  }
}

}  // namespace pbmaps
