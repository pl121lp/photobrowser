# PhotoBrowse — PNG and HEIC Support Spec

Status: **implemented** (2026-10-23), plan `docs/plans/2026-10-23-png-heic.md`;
deviations in §8. Self-contained together with `docs/spec.md`,
`docs/architecture.md` (§13 Plugins), `docs/map-spec.md` and `CLAUDE.md`.

## 1. Intent

The library folder holds more than JPEG. In the reference library (`photos/`):
1115 JPEG, **842 HEIC** (iPhone photos), **21 PNG** (iPhone screenshots, one
AI-generated image), 39 videos. Today HEIC and PNG files are skipped, so a large part
of every iPhone trip is missing from the grid and the map.

After this work both formats show up in the grid like any JPEG: sorted by capture
time, zoomable to full resolution, and (for HEIC) placed on the map from their GPS.

## 2. Goals, non-goals, anti-goals

Goals
- **PNG in the core**, decoded with the vendored `third_party/stb/stb_image.h`
  (no new dependency; builds unchanged on Linux, Windows and macOS).
- **HEIC/HEIF as a plugin** (`plugins/heif`, libheif), loaded only via `--plugin`
  like `video` and `maptiles`.
- Capture date and GPS for both, through **one** EXIF parser (the existing one in
  `src/library/exif.cpp`).
- Upright images: HEIC rotation/mirroring and PNG EXIF orientation are honoured, never
  applied twice.

Non-goals (for now)
- Colour management. JPEGs are shown without ICC/P3 conversion today; iPhone HEICs and
  some screenshots are Display P3 and will look exactly as unmanaged as the same
  photos saved as JPEG. A colour-managed pipeline is a separate, all-format feature.
- HDR gain maps (newer iPhones): show the SDR base image only.
- Live Photos: the paired `.MOV` stays a separate video item, as today.
- AVIF, WebP, TIFF, RAW, animated PNG (show the first frame only, which stb does).
- Depth maps, auxiliary or non-primary images in a HEIF file: primary image only.
- XMP parsing. Every sample screenshot carries its date in `eXIf` too (§4.2).

Anti-goals (a failure even if it "works")
- A HEIC dependency in the core, or the core naming the heif plugin.
- Indexing 842 HEICs noticeably slower than the same number of JPEGs, because each one
  decodes the full 12 MP image just to make a 128 px thumbnail. Concrete bar (my call):
  cold-cache quick indexing of a HEIC costs at most ~3× a JPEG, and the grid still
  shows previews within the spec.md §4 target. Missing it is reported in `docs/perf.md`
  and discussed — it does not by itself justify an ABI change beyond §4.4.
- Rotated photos showing sideways or rotated twice.
- File I/O or decoding on the render thread (CLAUDE.md rules apply).

## 3. What the real files contain (checked 2026-10-23)

PNG screenshots (`IMG_3xxx.PNG`, 1170×2532 or 2532×1170):
- `eXIf` chunk (≈250 bytes, before `IDAT`): TIFF with ImageDescription "Screenshot",
  Orientation 1, DateTime and Exif `DateTimeOriginal` (0x9003). **No GPS** (iOS never
  writes GPS into screenshots).
- XMP `iTXt` with `photoshop:DateCreated` — same time as `eXIf`; not needed.
- 14 of 21 are 8-bit RGB with `sRGB`; 7 are **16-bit RGB** with `iCCP` "Display P3"
  and `cICP` = [12, 13, 0, 1] (P3 primaries, **sRGB transfer**, full range) — i.e. SDR,
  so a plain 16→8-bit reduction is correct; no HDR tone mapping is needed.
- `SBWF6980.PNG` (1024×1536): no metadata at all, only a `caBX` (C2PA) chunk → falls
  back to file time, like a JPEG without EXIF.

HEIC (`IMG_xxxx.HEIC`, iPhone 11/13, 4032×3024, tile grid):
- Standard EXIF item: `DateTimeOriginal`, `OffsetTimeOriginal`, Orientation, full GPS
  IFD (2 of 3 sampled files had GPS). Some have Orientation 6 (portrait).
- The EXIF item payload starts with a 4-byte big-endian offset to the TIFF header
  (ISO 23008-12), which must be skipped.

## 4. Design

### 4.1 Shared EXIF parsing (pb_core)

`src/library/exif.cpp` already has an internal `parseExif(tiff, …, JpegHeaderInfo&)`.
Expose a public function that parses a **bare TIFF/EXIF block** (starting at the
`II*\0` / `MM\0*` header) and returns date-time-original, orientation and GPS. The
JPEG path keeps using it internally; PNG and the plugin host use it too. Its output
type may be a new small struct (e.g. `ExifInfo`) that `JpegHeaderInfo` reuses —
builder's call. Tests first: little- and big-endian blocks, no GPS, truncated block.

### 4.2 PNG in the core

- **Header parser (pb_core, pure, tested):** `parsePngHeader(span)` walks chunks from
  the signature up to the first `IDAT`, returning width/height from `IHDR` and the
  `eXIf` payload parsed with §4.1. Ignores CRCs (stb checks the data it decodes), stops
  cleanly on truncation, rejects a bad signature. `eXIf` after `IDAT` is ignored
  (the PNG spec recommends it before; all sample files comply).
- **Decoder (pb_imaging):** `stb_image` from `third_party/stb`, requesting 4 channels,
  8 bits (16-bit sources reduced by stb). Then apply EXIF orientation with the same
  code the JPEG path uses. Full decode only (PNG has no scaled decode); a 3 MP
  screenshot is cheap. Define `STB_IMAGE_IMPLEMENTATION` with only PNG enabled
  (`STBI_ONLY_PNG`) in one pb_imaging translation unit — it must not clash with the
  maptiles plugin's own copy (separate shared library, so no symbol conflict, but keep
  the core's symbols `static`/internal: `STB_IMAGE_STATIC`).
- **Wiring:** a built-in `MediaHandler` for `.png`, registered in the `MediaFormats`
  before any plugin's handlers, with an origin like `"core png 1"` so the catalog
  signature changes and existing libraries re-index once. `PluginHost` currently owns
  `MediaFormats`; how the built-in handler is added there (or the app composes the
  two) is the builder's call, but **pb_core must not depend on stb** — the handler
  implementation lives in pb_imaging/pb_app, only the interface and header parsing in
  pb_core.
- Dates: `DateTimeOriginal` → `takenAt`; missing → file time (existing fallback).

### 4.3 HEIC plugin (`plugins/heif`)

- Extensions `.heic`, `.heif`. Built by `pb_add_plugin` only when libheif is found
  (pkg-config `libheif`, or vcpkg/CMake config on Windows), like `video`.
- **On load:** if `heif_have_decoder_for_format(heif_compression_HEVC)` is false
  (libheif installed without the libde265 plugin), log one clear message naming the
  missing package and return no extensions (or NULL), so files aren't half-supported.
- **probe:** read the primary image handle; report width/height **as displayed** (after
  `irot`/`imir`; verify with an Orientation-6 sample). Fetch the `"Exif"` metadata
  block, skip its 4-byte offset prefix, and hand the TIFF block to the host's
  `parse_exif` (§4.4) for `taken_at` and GPS. A missing or unparsable EXIF block is
  not an error: `probe` still succeeds, with `taken_at = 0` (file time is used, like a
  JPEG without EXIF) and no GPS. Only a file libheif can't open fails `probe`.
- **No double work per file:** the host's quick index calls `probe` and then
  `decode_still` on the same path from the same worker thread. The plugin keeps the
  last file's bytes and `heif_context` in a `thread_local` slot (keyed by path, and by
  size and mtime) so the second call reuses them; the slot is freed on the next file and
  on `destroy`. No ABI change is needed for this.
- **decode_still:** libheif applies `irot`/`imir`/`clap` itself (default decoding
  options), so **EXIF orientation is ignored** for HEIC. Decode to 8-bit interleaved
  RGBA and copy rows (respecting the stride) into `PbImageOut`.
  - If the primary image has a thumbnail whose long edge ≥ `min_long_edge`, decode the
    thumbnail instead (indexing asks for 128 px — this is what keeps indexing fast).
  - Otherwise decode the full image and return it at natural size (the contract
    allows that); downscaling in the plugin is optional, builder's call after measuring.
- **File access:** read the file into memory with UTF-8-safe I/O and use
  `heif_context_read_from_memory_without_copy`, so non-ASCII paths work on Windows.
- **Threads:** probe/decode run concurrently on host workers; each call uses its own
  `heif_context`. libheif decodes grid tiles with its own threads; whether to cap that
  (`heif_context_set_max_decoding_threads`) is decided by measuring indexing time and
  full-zoom latency.
- **Config:** none needed now.

### 4.4 Plugin ABI version 5

Append to `PbHost` (after `cache_dir`), bump `PB_PLUGIN_ABI_VERSION` to 5:

```c
/* ---- Since version 5 ---- */
/* Parses a TIFF/EXIF block (starting at "II*\0" or "MM\0*") and sets taken_at and the
 * GPS fields of *out (width/height untouched). 1 if the block was valid, else 0.
 * Thread-safe; callable from probe. */
int (*parse_exif)(void* ctx, const uint8_t* tiff, size_t size, PbMediaInfo* out);
```

The plugin checks `host->struct_size` before using it; on an older host it simply has
no date/GPS. Older plugins (v1–v4 tables) must keep loading — extend the existing
`testfake_v1` style test.

## 5. Platforms and dependencies

| | Linux | Windows | macOS |
|---|---|---|---|
| PNG (core) | vendored stb | vendored stb | vendored stb |
| HEIC plugin | `libheif-dev` + `libheif-plugin-libde265` (Ubuntu 24.04+; older: `libheif-dev` includes it) | vcpkg `libheif` (pulls libde265) | Homebrew `libheif` |

Nothing in this design is platform-specific; no code goes in `src/platform/`. The
project builds and is tested on Linux only today; Windows/macOS are expected to work
but are not verified as part of this work.
Licences: libheif and libde265 are LGPL-3, loaded dynamically by an optional plugin.
HEVC may be patent-encumbered in some countries; that is why it stays optional.
The README lists the new optional packages (never install them — CLAUDE.md).

## 6. Verification

- Unit tests first for §4.1, §4.2 header parsing, §4.4 host callback (via the fake
  plugin) and ABI compatibility.
- A tiny synthetic PNG fixture with an `eXIf` chunk is generated in the test (no user
  photos in the repo). HEIC has no automated decode test (needs an encoder); it is
  checked manually against `photos/` with `--snapshot`: an Orientation-6 HEIC upright,
  a 16-bit P3 screenshot not washed out/black, GPS dots on the map.
- Indexing time of the reference library (cold cache, release build) before/after,
  recorded in `docs/perf.md`; `--bench` unchanged.
- Docs: `docs/spec.md` (supported formats), `docs/architecture.md` (§13, ABI v5),
  README (packages).

## 7. Left to the builder

Names of new types/functions, where the built-in PNG handler is registered, whether
the heif plugin downscales or caps libheif threads (measure), and the exact CMake
detection of libheif per platform.

## 8. Deviations made during implementation

- The plugin must call `heif_init`/`heif_deinit` (libheif then loads its decoder
  plugins, e.g. libde265); without it no HEIC decodes.
- libheif's image handle size is already post-rotation (`irot`/`imir` applied), so the
  plugin reports it as is and never swaps width and height.
- Some iPhone HEICs (e.g. `IMG_5693.HEIC`) have no embedded thumbnail; `quickIndex` then
  does a full 12 MP decode for them (~200-300 ms). In 4 of 50 sampled files; see
  `docs/perf.md`. The mean per-file cost is dominated by these, so the "<= ~3x JPEG"
  bar holds for HEICs with a thumbnail (median) but not for the mean of a mixed library.
- A truncated HEIC with an intact thumbnail still indexes; only the larger levels fail.
