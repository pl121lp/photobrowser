/* Minimal plugin for the plugin-host tests: ".fake" files are 40x20 still images
 * whose colour is the file's first byte; a player shows a 4x2 frame while "playing". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin/photobrowse_plugin.h"

static const PbHost* g_host;

struct PbPlayer {
  int playing;
  int activations;
  uint8_t pixels[4 * 2 * 4];
};

static int firstByte(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return -1;
  const int c = fgetc(f);
  fclose(f);
  return c;
}

static int probe(void* self, const char* path, PbMediaInfo* out) {
  (void)self;
  if (firstByte(path) < 0) return 0;
  out->width = 40;
  out->height = 20;
  out->taken_at = 1234;
  return 1;
}

static int decodeStill(void* self, const char* path, uint32_t minLongEdge, PbImageOut* out) {
  (void)self;
  (void)minLongEdge;
  const int c = firstByte(path);
  if (c < 0) return 0;
  uint8_t* px = out->alloc(out->ctx, 40, 20);
  if (!px) return 0;
  for (int i = 0; i < 40 * 20; ++i) {
    px[i * 4 + 0] = (uint8_t)c;
    px[i * 4 + 1] = 0;
    px[i * 4 + 2] = 0;
    px[i * 4 + 3] = 255;
  }
  return 1;
}

static PbPlayer* playerOpen(void* self, const char* path) {
  (void)self;
  (void)path;
  PbPlayer* p = calloc(1, sizeof *p);
  memset(p->pixels, 200, sizeof p->pixels);
  return p;
}

static void playerClose(PbPlayer* p) { free(p); }

static void playerEvent(PbPlayer* p, int event) {
  if (event != PB_EVENT_ACTIVATE) return;
  p->playing = !p->playing;
  p->activations++;
  /* Exercise the host services: no audio backend in tests, so this is NULL. */
  PbAudioOut* audio = g_host->audio_open(g_host->ctx, 48000, 2);
  if (audio) g_host->audio_close(audio);
}

static int playerFrame(PbPlayer* p, PbFrame* out) {
  if (!p->activations) return 0;
  out->width = 4;
  out->height = 2;
  out->stride = 16;
  out->rgba = p->pixels;
  out->serial = (uint64_t)p->activations;
  return 1;
}

static int playerActive(PbPlayer* p) { return p->playing; }

static void destroy(void* self) { (void)self; }

static const char* const kExtensions[] = {".fake", NULL};

static const PbPlugin kPlugin = {
    PB_PLUGIN_ABI_VERSION, sizeof(PbPlugin), "testfake", "1.0", "test plugin", NULL, kExtensions,
    probe, decodeStill, playerOpen, playerClose, playerEvent, playerFrame, playerActive, destroy,
};

PB_PLUGIN_EXPORT const PbPlugin* pb_plugin_entry(const PbHost* host) {
  if (!host || host->abi_version != PB_PLUGIN_ABI_VERSION) return NULL;
  g_host = host;
  host->log(host->ctx, PB_LOG_INFO, "test plugin loaded");
  return &kPlugin;
}
