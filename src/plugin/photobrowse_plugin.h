/* PhotoBrowse plugin API — the only header a plugin includes.
 *
 * Plain C, so a plugin can be built with any compiler (and a different C++ runtime)
 * than the application. A plugin is a shared library (.so / .dll / .dylib) exporting
 * one function, pb_plugin_entry, which receives the host's services and returns a
 * table describing what the plugin offers. The application loads plugins only when
 * asked with --plugin <name|path>; it never depends on any particular plugin.
 *
 * Threading contract:
 *   - probe / decode_still run on background worker threads, possibly several at once
 *     for different files. They may do file I/O and decoding.
 *   - player_* run on the main (render) thread. They must return within a fraction of
 *     a millisecond: no file I/O, no decoding, no waiting on locks held by slow work.
 *     A plugin that plays content runs its own threads.
 *   - Host services (PbHost) may be called from any thread.
 *
 * Memory: the host allocates images (PbImageOut.alloc), so no memory crosses the
 * boundary in the other direction. Frames returned by player_frame stay owned by the
 * plugin and must remain valid until the next player_* call on that player.
 *
 * Versioning: PB_PLUGIN_ABI_VERSION changes whenever a struct layout or a function
 * signature changes. Fields are only ever appended; struct_size tells the other side
 * how much of a struct it may read.
 */
#ifndef PHOTOBROWSE_PLUGIN_H
#define PHOTOBROWSE_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PB_PLUGIN_ABI_VERSION 1u
#define PB_PLUGIN_ENTRY_NAME "pb_plugin_entry"

#if defined(_WIN32)
#define PB_PLUGIN_EXPORT __declspec(dllexport)
#else
#define PB_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* ---- Host services ------------------------------------------------------------ */

enum PbLogLevel { PB_LOG_INFO = 0, PB_LOG_WARNING = 1, PB_LOG_ERROR = 2 };

typedef struct PbAudioOut PbAudioOut; /* opaque, owned by the host */

typedef struct PbHost {
  uint32_t abi_version; /* PB_PLUGIN_ABI_VERSION of the host */
  uint32_t struct_size; /* sizeof(PbHost) as compiled into the host */
  void* ctx;            /* pass back as the first argument */

  void (*log)(void* ctx, int level, const char* message);

  /* Audio output: interleaved 32-bit float samples. Returns NULL if no audio device
   * is available (play silently then). Starts paused. */
  PbAudioOut* (*audio_open)(void* ctx, int sample_rate, int channels);
  void (*audio_write)(PbAudioOut* out, const float* samples, uint32_t frames);
  double (*audio_queued_seconds)(PbAudioOut* out); /* written but not yet played */
  void (*audio_pause)(PbAudioOut* out, int paused);
  void (*audio_clear)(PbAudioOut* out); /* drop everything queued */
  void (*audio_close)(PbAudioOut* out);
} PbHost;

/* ---- Media: still images for the grid --------------------------------------------- */

typedef struct PbMediaInfo {
  uint32_t width, height; /* as displayed (after any rotation), in pixels */
  int64_t taken_at;       /* capture time, seconds since 1970 in local wall-clock time
                           * (like EXIF DateTimeOriginal); 0 = unknown (file time is used) */
} PbMediaInfo;

/* Destination for decoded pixels: alloc returns a buffer of width*height*4 bytes
 * (RGBA, 8 bits per channel, sRGB, rows top to bottom, tightly packed) or NULL. Call
 * it at most once per decode. */
typedef struct PbImageOut {
  void* ctx;
  uint8_t* (*alloc)(void* ctx, uint32_t width, uint32_t height);
} PbImageOut;

/* ---- Players: live content shown when an item is open in whole-photo view ------- */

typedef struct PbPlayer PbPlayer; /* opaque, owned by the plugin */

enum PbPlayerEvent {
  PB_EVENT_ACTIVATE = 1, /* primary click on the open item (e.g. play / pause) */
};

typedef struct PbFrame {
  uint32_t width, height; /* same aspect as PbMediaInfo */
  uint32_t stride;        /* bytes per row, a multiple of 4 */
  const uint8_t* rgba;    /* RGBA, sRGB, top row first */
  uint64_t serial;        /* changes whenever the pixels change (the host skips re-uploads) */
} PbFrame;

/* ---- The plugin -------------------------------------------------------------------- */

typedef struct PbPlugin {
  uint32_t abi_version; /* PB_PLUGIN_ABI_VERSION the plugin was built against */
  uint32_t struct_size; /* sizeof(PbPlugin) as compiled into the plugin */
  const char* name;     /* short identifier, e.g. "video" */
  const char* version;
  const char* description;
  void* self; /* passed back as the first argument below */

  /* File extensions this plugin turns into grid items: lower case with the dot,
   * NULL-terminated, e.g. { ".mp4", ".mov", NULL }. NULL = none. */
  const char* const* extensions;

  /* Worker threads. Return 1 on success, 0 if the file can't be handled. */
  int (*probe)(void* self, const char* path_utf8, PbMediaInfo* out);
  /* A representative still image, upright, with its long edge at least
   * min_long_edge pixels (or its natural size if smaller). */
  int (*decode_still)(void* self, const char* path_utf8, uint32_t min_long_edge, PbImageOut* out);

  /* Main thread, optional (NULL = items are still images only). player_open is called
   * when an item from this plugin is opened in whole-photo view and player_close when
   * it is left; neither may block. */
  PbPlayer* (*player_open)(void* self, const char* path_utf8);
  void (*player_close)(PbPlayer* player);
  void (*player_event)(PbPlayer* player, int event);
  /* 1 and fills *out when a frame should be shown instead of the still image (it may
   * be the same frame as last time); 0 to show the still. */
  int (*player_frame)(PbPlayer* player, PbFrame* out);
  /* 1 while the content changes on its own (the host keeps redrawing). */
  int (*player_active)(PbPlayer* player);

  /* Called once before the library is unloaded; must stop all plugin threads. */
  void (*destroy)(void* self);
} PbPlugin;

/* The single exported symbol. Returns NULL if the plugin can't work with this host
 * (e.g. an incompatible abi_version). */
typedef const PbPlugin* (*PbPluginEntry)(const PbHost* host);

#ifdef __cplusplus
}
#endif

#endif /* PHOTOBROWSE_PLUGIN_H */
