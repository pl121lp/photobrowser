// PhotoBrowse heif plugin: HEIC / HEIF photos (e.g. from iPhones) appear in the grid
// like JPEGs, with their capture date and GPS position (EXIF parsed by the host).
// Built on libheif with an HEVC decoder (libde265). Talks to the application only
// through src/plugin/photobrowse_plugin.h.

#include <libheif/heif.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "heif_util.h"
#include "plugin/photobrowse_plugin.h"

namespace {

const PbHost* g_host = nullptr;

void logMsg(int level, const char* msg) {
  if (g_host && g_host->log) g_host->log(g_host->ctx, level, msg);
}

// ---- Per-thread file cache ---------------------------------------------------------
// Indexing probes a file and then decodes it on the same worker thread, so each thread
// keeps its last file (bytes + parsed context) and reuses it while path, size and
// modification time match.

struct Cached {
  std::string path;
  uintmax_t size = 0;
  std::filesystem::file_time_type mtime{};
  std::vector<uint8_t> bytes;  // the context reads from these without a copy
  heif_context* ctx = nullptr;

  ~Cached() {
    if (ctx) heif_context_free(ctx);
  }
};

std::mutex g_cacheMutex;
std::unordered_map<std::thread::id, std::unique_ptr<Cached>> g_cache;

// The calling thread's context for `path`, or nullptr if the file can't be read or
// parsed. Only this thread uses the returned context.
heif_context* contextFor(const char* pathUtf8) {
  if (!pathUtf8) return nullptr;
  const std::filesystem::path fsPath(std::u8string(reinterpret_cast<const char8_t*>(pathUtf8)));
  std::error_code ec;
  const uintmax_t size = std::filesystem::file_size(fsPath, ec);
  if (ec) return nullptr;
  const auto mtime = std::filesystem::last_write_time(fsPath, ec);
  if (ec) return nullptr;

  const auto self = std::this_thread::get_id();
  {
    std::lock_guard lock(g_cacheMutex);
    auto it = g_cache.find(self);
    if (it != g_cache.end()) {
      const Cached& c = *it->second;
      if (c.path == pathUtf8 && c.size == size && c.mtime == mtime) return c.ctx;
      g_cache.erase(it);
    }
  }

  auto c = std::make_unique<Cached>();
  c->path = pathUtf8;
  c->size = size;
  c->mtime = mtime;
  auto bytes = pbheif::readFileUtf8(pathUtf8);
  if (!bytes || bytes->empty()) return nullptr;
  c->bytes = std::move(*bytes);
  c->ctx = heif_context_alloc();
  if (!c->ctx) return nullptr;
  const heif_error err = heif_context_read_from_memory_without_copy(c->ctx, c->bytes.data(), c->bytes.size(), nullptr);
  if (err.code != heif_error_Ok) return nullptr;
  heif_context* ctx = c->ctx;
  std::lock_guard lock(g_cacheMutex);
  g_cache[self] = std::move(c);
  return ctx;
}

struct HandleDeleter {
  void operator()(heif_image_handle* h) const { heif_image_handle_release(h); }
};
struct ImageDeleter {
  void operator()(heif_image* img) const { heif_image_release(img); }
};
using HandlePtr = std::unique_ptr<heif_image_handle, HandleDeleter>;
using ImagePtr = std::unique_ptr<heif_image, ImageDeleter>;

HandlePtr primaryHandle(heif_context* ctx) {
  heif_image_handle* h = nullptr;
  if (heif_context_get_primary_image_handle(ctx, &h).code != heif_error_Ok) return nullptr;
  return HandlePtr(h);
}

// ---- Plugin functions --------------------------------------------------------------

// The first "Exif" block of the primary image, handed to the host's EXIF parser.
void readExif(const heif_image_handle* handle, PbMediaInfo* out) {
  if (!g_host || g_host->struct_size < offsetof(PbHost, parse_exif) + sizeof(g_host->parse_exif) ||
      !g_host->parse_exif)
    return;
  heif_item_id id = 0;
  if (heif_image_handle_get_list_of_metadata_block_IDs(handle, "Exif", &id, 1) < 1) return;
  std::vector<uint8_t> item(heif_image_handle_get_metadata_size(handle, id));
  if (item.empty() || heif_image_handle_get_metadata(handle, id, item.data()).code != heif_error_Ok) return;
  const auto tiff = pbheif::exifTiffFromItem(item);
  if (!tiff.empty()) g_host->parse_exif(g_host->ctx, tiff.data(), tiff.size(), out);
}

int probe(void*, const char* path, PbMediaInfo* out) {
  heif_context* ctx = contextFor(path);
  if (!ctx) return 0;
  HandlePtr handle = primaryHandle(ctx);
  if (!handle) return 0;
  // As displayed: libheif reports the size after the irot / imir transformations.
  const int w = heif_image_handle_get_width(handle.get());
  const int h = heif_image_handle_get_height(handle.get());
  if (w <= 0 || h <= 0) return 0;
  out->width = uint32_t(w);
  out->height = uint32_t(h);
  readExif(handle.get(), out);
  return 1;
}

// The thumbnail chosen for min_long_edge, or the primary image itself.
HandlePtr stillHandle(heif_context* ctx, uint32_t minLongEdge) {
  HandlePtr primary = primaryHandle(ctx);
  if (!primary) return nullptr;
  const int n = heif_image_handle_get_number_of_thumbnails(primary.get());
  if (n <= 0) return primary;
  std::vector<heif_item_id> ids(static_cast<size_t>(n));
  ids.resize(size_t(heif_image_handle_get_list_of_thumbnail_IDs(primary.get(), ids.data(), n)));
  std::vector<HandlePtr> thumbs;
  std::vector<std::pair<uint32_t, uint32_t>> sizes;
  for (heif_item_id id : ids) {
    heif_image_handle* t = nullptr;
    if (heif_image_handle_get_thumbnail(primary.get(), id, &t).code != heif_error_Ok) continue;
    thumbs.emplace_back(t);
    sizes.emplace_back(uint32_t(heif_image_handle_get_width(t)), uint32_t(heif_image_handle_get_height(t)));
  }
  const int pick = pbheif::chooseThumbnail(sizes, minLongEdge);
  return pick >= 0 ? std::move(thumbs[size_t(pick)]) : std::move(primary);
}

// Streaming calls only decode, and index workers exit after the first run: drop the
// calling thread's cached file so it is not held for the whole session.
void forgetThisThread() {
  std::lock_guard lock(g_cacheMutex);
  g_cache.erase(std::this_thread::get_id());
}

struct CacheRelease {
  ~CacheRelease() { forgetThisThread(); }
};

int decodeStill(void*, const char* path, uint32_t minLongEdge, PbImageOut* out) {
  heif_context* ctx = contextFor(path);
  if (!ctx) return 0;
  CacheRelease release;  // runs after the handle and image below are released
  HandlePtr handle = stillHandle(ctx, minLongEdge);
  if (!handle) return 0;
  heif_image* raw = nullptr;  // default options: irot / imir applied, so upright
  if (heif_decode_image(handle.get(), &raw, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr).code !=
      heif_error_Ok)
    return 0;
  ImagePtr img(raw);
  const int w = heif_image_get_width(img.get(), heif_channel_interleaved);
  const int h = heif_image_get_height(img.get(), heif_channel_interleaved);
  if (w <= 0 || h <= 0) return 0;
#if LIBHEIF_NUMERIC_VERSION >= ((1 << 24) | (20 << 16))
  size_t stride = 0;
  const uint8_t* src = heif_image_get_plane_readonly2(img.get(), heif_channel_interleaved, &stride);
#else
  int strideInt = 0;
  const uint8_t* src = heif_image_get_plane_readonly(img.get(), heif_channel_interleaved, &strideInt);
  const size_t stride = size_t(strideInt);
#endif
  if (!src || stride < size_t(w) * 4) return 0;
  uint8_t* dst = out->alloc(out->ctx, uint32_t(w), uint32_t(h));
  if (!dst) return 0;
  const size_t row = size_t(w) * 4;
  for (int y = 0; y < h; ++y) std::memcpy(dst + size_t(y) * row, src + size_t(y) * stride, row);
  return 1;
}

void destroy(void*) {
  {
    std::lock_guard lock(g_cacheMutex);
    g_cache.clear();
  }
  heif_deinit();
}

const char* const kExtensions[] = {".heic", ".heif", nullptr};

PbPlugin kPlugin = {
    PB_PLUGIN_ABI_VERSION,
    sizeof(PbPlugin),
    "heif",
    "1.0",
    "HEIC / HEIF photos (libheif)",
    nullptr,
    kExtensions,
    probe,
    decodeStill,
    nullptr,  // stills only: no player
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    destroy,
    nullptr,  // no map tiles
    0,
    0,
    nullptr,
    nullptr,
};

}  // namespace

extern "C" PB_PLUGIN_EXPORT const PbPlugin* pb_plugin_entry(const PbHost* host) {
  if (!host || host->abi_version != PB_PLUGIN_ABI_VERSION) return nullptr;
  g_host = host;
  const heif_error initErr = heif_init(nullptr);  // loads libheif's decoder plugins (e.g. libde265)
  if (initErr.code != heif_error_Ok) {
    logMsg(PB_LOG_ERROR, (std::string("libheif failed to initialise: ") + (initErr.message ? initErr.message : "")).c_str());
    kPlugin.extensions = nullptr;
    return &kPlugin;
  }
  if (!heif_have_decoder_for_format(heif_compression_HEVC)) {
    logMsg(PB_LOG_ERROR,
           "libheif has no HEVC decoder; install libheif-plugin-libde265 (Linux) or a libheif with libde265");
    kPlugin.extensions = nullptr;
  } else {
    kPlugin.extensions = kExtensions;
  }
  return &kPlugin;
}
