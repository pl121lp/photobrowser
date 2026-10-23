// Map tiles plugin: raster tiles over HTTP(S) (OpenStreetMap by default) with a disk
// cache. Settings come from the [plugin.maptiles] config section (see README).
//
// OSM tile usage policy (https://operations.osmfoundation.org/policies/tiles/): an
// identifying User-Agent, tiles cached on disk for as long as the server allows (at
// least 7 days when it says nothing), at most two connections (the host calls from two
// threads), no prefetching (the host asks only for what is shown), a back-off after
// 403/429, and the attribution shown on the map by the host.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "maptiles_util.h"
#include "plugin/photobrowse_plugin.h"

namespace {

namespace fs = std::filesystem;

constexpr const char* kDefaultUrl = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
constexpr const char* kDefaultAttribution = "© OpenStreetMap contributors";
constexpr const char* kUserAgent = "PhotoBrowse/" PB_VERSION " (+https://github.com/pl121lp/photobrowser)";
constexpr int64_t kRefusedBackoffS = 60;
constexpr int kTrimEveryWrites = 300;

const PbHost* g_host;

struct State {
  std::string url, apiKey, attribution;
  int maxZoom = 19;
  uint64_t diskBytes = uint64_t(500) << 20;
  fs::path cacheDir;  // empty: no disk cache

  std::mutex poolMutex;
  std::vector<CURL*> pool;  // idle handles (each keeps its connection alive)
  std::atomic<int64_t> refusedUntil{0};
  std::atomic<int> writes{0};
  std::atomic<bool> trimming{false}, trimmed{false};
  std::atomic<int> errorsLogged{0};
  std::atomic<bool> sizeLogged{false};
};
State* g;
PbPlugin g_plugin;

void log(int level, const std::string& msg) { g_host->log(g_host->ctx, level, msg.c_str()); }

int64_t nowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string setting(const char* key, const char* fallback) {
  const char* v = g_host->setting ? g_host->setting(g_host->ctx, key) : nullptr;
  return v && *v ? v : fallback;
}

std::optional<std::vector<uint8_t>> readFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

int64_t readExpiry(const fs::path& tile) {
  std::ifstream f(fs::path(tile).concat(".exp"));
  long long v = 0;
  return f >> v ? int64_t(v) : 0;
}

// Temp file next to `path`, then rename over it: readers never see half a file.
bool atomicWrite(const fs::path& path, const void* data, size_t size) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  thread_local std::mt19937_64 rng{std::random_device{}()};
  const fs::path tmp = fs::path(path).concat(".tmp" + std::to_string(rng()));
  {
    std::ofstream f(tmp, std::ios::binary);
    if (!f.write(static_cast<const char*>(data), std::streamsize(size))) return false;
  }
  fs::rename(tmp, path, ec);
  if (ec) fs::remove(tmp, ec);
  return !ec;
}

// Decodes a PNG/JPEG tile into the host's buffer: 256x256 only.
int decode(const std::vector<uint8_t>& data, PbImageOut* out) {
  int w = 0, h = 0, n = 0;
  stbi_uc* px = stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &n, 4);
  if (!px) return PB_TILE_ERROR;
  int result = PB_TILE_ERROR;
  if (w == 256 && h == 256) {
    if (uint8_t* dst = out->alloc(out->ctx, 256, 256)) {
      std::memcpy(dst, px, 256 * 256 * 4);
      result = PB_TILE_OK;
    }
  } else if (!g->sizeLogged.exchange(true)) {
    log(PB_LOG_WARNING, "tiles must be 256x256 px, the server sends " + std::to_string(w) + "x" + std::to_string(h));
  }
  stbi_image_free(px);
  return result;
}

CURL* takeHandle() {
  {
    std::lock_guard lock(g->poolMutex);
    if (!g->pool.empty()) {
      CURL* c = g->pool.back();
      g->pool.pop_back();
      return c;
    }
  }
  return curl_easy_init();
}

void returnHandle(CURL* c) {
  std::lock_guard lock(g->poolMutex);
  g->pool.push_back(c);
}

size_t appendBytes(char* p, size_t size, size_t n, void* user) {
  auto* v = static_cast<std::vector<uint8_t>*>(user);
  v->insert(v->end(), p, p + size * n);
  return size * n;
}

size_t appendHeader(char* p, size_t size, size_t n, void* user) {
  static_cast<std::string*>(user)->append(p, size * n);
  return size * n;
}

struct Fetched {
  PbTileResult result = PB_TILE_ERROR;
  std::vector<uint8_t> body;
  std::string headers;
};

Fetched fetch(const std::string& url) {
  Fetched f;
  CURL* c = takeHandle();
  if (!c) return f;
  curl_easy_reset(c);
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
  curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, appendBytes);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &f.body);
  curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, appendHeader);
  curl_easy_setopt(c, CURLOPT_HEADERDATA, &f.headers);
  const CURLcode code = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  f.result = pbmaps::mapTransfer(code, status);
  if (f.result == PB_TILE_ERROR && g->errorsLogged++ < 5) {
    log(PB_LOG_WARNING, url + ": " + (code != CURLE_OK ? curl_easy_strerror(code) : "HTTP " + std::to_string(status)));
  }
  returnHandle(c);
  return f;
}

void maybeTrim() {
  if (g->cacheDir.empty()) return;
  if (g->trimmed.load() && g->writes.load() < kTrimEveryWrites) return;
  if (g->trimming.exchange(true)) return;  // another thread is at it
  g->writes = 0;
  g->trimmed = true;
  pbmaps::trimCache(g->cacheDir / "maptiles", g->diskBytes);
  g->trimming = false;
}

int tileGet(void*, int z, int x, int y, PbImageOut* out) {
  if (z < 0 || z > g->maxZoom || x < 0 || y < 0 || x >= (1 << z) || y >= (1 << z)) return PB_TILE_MISSING;
  maybeTrim();
  const int64_t now = nowSeconds();
  fs::path path;
  std::optional<std::vector<uint8_t>> cached;
  if (!g->cacheDir.empty()) {
    path = pbmaps::tilePath(g->cacheDir, g->url, z, x, y);
    cached = readFile(path);
    if (cached && now < readExpiry(path) && decode(*cached, out) == PB_TILE_OK) {
      std::error_code ec;
      fs::last_write_time(path, fs::file_time_type::clock::now(), ec);  // recently used: trimmed last
      return PB_TILE_OK;
    }
  }
  // A stale cached tile beats none when the server can't be asked.
  auto stale = [&](int result) { return cached && decode(*cached, out) == PB_TILE_OK ? int(PB_TILE_OK) : result; };
  if (now < g->refusedUntil.load()) return stale(PB_TILE_REFUSED);

  Fetched f = fetch(pbmaps::expandUrl(g->url, z, x, y, g->apiKey));
  switch (f.result) {
    case PB_TILE_OK: {
      const int r = decode(f.body, out);
      if (r == PB_TILE_OK && !path.empty()) {
        const std::string expiry = std::to_string(pbmaps::expiryFor(pbmaps::maxAgeSeconds(f.headers), now));
        if (atomicWrite(path, f.body.data(), f.body.size())) {
          atomicWrite(fs::path(path).concat(".exp"), expiry.data(), expiry.size());
          ++g->writes;
        }
      }
      return r;
    }
    case PB_TILE_REFUSED:
      if (g->refusedUntil.exchange(now + kRefusedBackoffS) < now) {
        log(PB_LOG_WARNING, "the tile server refused requests (HTTP 403/429); pausing for a minute");
      }
      return stale(PB_TILE_REFUSED);
    case PB_TILE_OFFLINE: return stale(PB_TILE_OFFLINE);
    default: return f.result;
  }
}

void destroy(void*) {
  if (!g) return;
  for (CURL* c : g->pool) curl_easy_cleanup(c);
  delete g;
  g = nullptr;
  curl_global_cleanup();
}

}  // namespace

extern "C" PB_PLUGIN_EXPORT const PbPlugin* pb_plugin_entry(const PbHost* host) {
  if (!host || host->abi_version != PB_PLUGIN_ABI_VERSION || host->struct_size < sizeof(PbHost)) return nullptr;
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return nullptr;
  g_host = host;
  g = new State;
  g->url = setting("url", kDefaultUrl);
  g->apiKey = setting("api_key", "");
  g->attribution = setting("attribution", kDefaultAttribution);
  g->maxZoom = std::clamp(std::atoi(setting("max_zoom", "19").c_str()), 0, 22);
  g->diskBytes = uint64_t(std::max(10.0, std::atof(setting("disk_cache", "500").c_str())) * double(1 << 20));
  if (host->cache_dir) g->cacheDir = fs::path(host->cache_dir);

  g_plugin = {};
  g_plugin.abi_version = PB_PLUGIN_ABI_VERSION;
  g_plugin.struct_size = sizeof(PbPlugin);
  g_plugin.name = "maptiles";
  g_plugin.version = "1.0";
  g_plugin.description = "Map tiles for the map view (M): OpenStreetMap by default, cached on disk";
  g_plugin.destroy = destroy;
  g_plugin.tile_attribution = g->attribution.c_str();
  g_plugin.tile_min_zoom = 0;
  g_plugin.tile_max_zoom = g->maxZoom;
  g_plugin.tile_get = tileGet;
  return &g_plugin;
}
