# PhotoBrowse — Architecture

Status: draft for approval · 2026-10-21 · Companion to [spec.md](spec.md)

## 1. Technology choices

| Concern | Choice | Why |
|---|---|---|
| Language | **C++20** | Predictable performance, full control of memory and threads. |
| Window / input / GL context | **SDL3** | One API for Linux (X11 + Wayland), Windows, macOS; borderless fullscreen, HiDPI, high-res wheel events. |
| GPU API | **OpenGL 4.1 core**, GLSL 410 | Ample for instanced drawing of tens of thousands of tiles; highest version macOS supports, trivially available on Windows/Linux; natural home for custom shader effects. Vulkan would add a lot of code for no gain here. The renderer is behind a narrow interface so a Vulkan/D3D backend remains possible. |
| GL function loading | Hand-written loader (only the functions we use) via `SDL_GL_GetProcAddress`, with vendored Khronos `glcorearb.h`/`khrplatform.h` | No generator dependency; identical on all platforms. |
| JPEG decode/encode | **libjpeg-turbo** (TurboJPEG API) | SIMD decode; **DCT-domain scaled decode** (1/2, 1/4, 1/8) makes thumbnails 5–20× cheaper than full decode + resize. |
| EXIF | Small in-house parser (APP1/TIFF IFD walk for `DateTimeOriginal`, `Orientation`, dimensions) | Only a few tags needed; avoids a dependency. |
| Resizing | In-house area-average (box) downscaler | Applied after DCT scaling, so only small factors remain. |
| Tests | **doctest** (vendored single header) + CTest | Zero install. |
| Build | **CMake ≥ 3.24** + **Ninja**, `CMakePresets.json` | Portable; presets for Linux (GCC/Clang) and Windows (MSVC + **vcpkg** manifest). |
| Future DB | **SQLite** (with FTS5) | Embedded, fast, cross-platform, single file. Not used in v1. |

**Dependencies to install (Linux, Ubuntu 25.10):** `libsdl3-dev`, `libturbojpeg0-dev`.
Already present: cmake, ninja, g++/clang++, pkg-config.
**Windows:** Visual Studio 2022 + vcpkg; `vcpkg.json` lists `sdl3`, `libjpeg-turbo`.

## 2. Module overview

```
src/
  main.cpp                 arg parsing, constructs App
  app/        App          main loop, event dispatch, frame timing, fullscreen
  platform/   Paths        per-user cache dir (XDG_CACHE_HOME / %LOCALAPPDATA%), UTF-8 paths
  view/       Camera       center (double world coords) + zoom; screen<->world
              Motion       momentum (exponential friction), eased tweens, smooth zoom
              Input        drag/click/double-click detection -> Camera commands
              Screensaver  random photo tour: flyTo (pull-back arc) -> open -> hold -> next
  layout/     Layout       interface: cell rect + content placement per photo index
              SquareGrid   v1 implementation (square-crop)
  library/    Catalog      ordered vector<PhotoRecord>; persisted to cache
              Scanner      recursive folder walk, change detection
              Exif         minimal EXIF reader
  imaging/    JpegCodec    TurboJPEG wrapper: header read, scaled decode, encode
              Resize       box downscale, mip generation, orientation
  cache/      ThumbCache   on-disk levels: L0 pack file + per-photo L1/L2 files
  stream/     JobSystem    worker threads, priority queue, cancellation
              Residency    decides desired level per visible photo, requests loads,
                           owns GPU slot allocation and LRU eviction
  gfx/        GL loader, Shader, PagedTexture (texture-array atlas pages),
              TileRenderer (instanced draw), Uploader (budgeted per-frame uploads)
shaders/      tile.vert / tile.frag (embedded into the binary at build time)
tests/        unit tests for pure logic
tools/        make_test_library.(sh|py) — synthetic 10k library via links
```

Dependency direction: `app → view, layout, stream, gfx, library`; `stream → cache,
imaging, gfx(interface only)`; `layout`, `view`, `library`, `imaging` have no GL or SDL
dependency and are unit-testable.

## 3. Coordinate system and camera
- World units: one grid cell = 1.0 × 1.0. Cell *(c, r)* spans `[c, c+1] × [r, r+1]`;
  the photo occupies the cell minus a gap (default 3% each side).
- Camera = `center` (double2, world) + `scale` (screen pixels per world unit, in
  *drawable* pixels to handle HiDPI). Shaders receive camera-relative float coords,
  so precision holds for any library size.
- Zoom is applied about a pivot (cursor) so the world point under the pivot is
  invariant. Wheel input sets a *target* scale (multiplicative per notch, handles
  fractional/precise wheel deltas); actual scale approaches it exponentially
  (frame-rate independent: `s += (t - s) * (1 - exp(-k·dt))`).
- Momentum: on release, velocity = recent drag samples (last ~50 ms) averaged; decays
  as `v·exp(-λt)`. The landing point is analytic (`p + v/λ`) — used later by prefetch.
- Click-to-center: tween of `center` with ease-in-out cubic, ~400 ms; double-click
  also tweens `log(scale)` to fill the screen.
- All motion is driven by real `dt` from a monotonic clock; clamp large `dt`.

## 4. Layout abstraction
```cpp
struct Placement { Rect cell; ContentMode mode; };  // mode: Crop | Fit (letterbox) | ...
class Layout {
  virtual void build(std::span<const PhotoRecord>) = 0;
  virtual Rect bounds() const = 0;
  virtual Placement place(uint32_t index) const = 0;
  virtual void visible(const Rect& worldView, std::vector<uint32_t>& out) const = 0; // O(visible)
  virtual std::optional<uint32_t> hit(Vec2 world) const = 0;
};
```
- SquareGrid computes visibility by row/column ranges (no per-photo scan).
- The **content UV rect** (crop vs. fit) is computed per visible tile on the CPU from
  the photo's aspect ratio, the cell rect, and the mode, and passed per instance. Thumbnails are stored with their
  *full aspect ratio*, so changing layout mode never touches the cache.
- Letterbox frames (future) = draw cell background black, image fitted inside.

## 5. Image pyramid and caching

Every photo has a pyramid of levels with a **factor-2 step** (fine steps keep GPU
memory close to what the screen actually shows). Each stored image **preserves aspect
ratio**, fitted inside a square of the level's slot size (long edge = slot size).

| Level | Slot | Produced from | Disk cache | GPU |
|---|---|---|---|---|
| L0 | 32 px | embedded EXIF preview, else 1/8 decode | `l0.pack` — one file, raw RGBA, 4 KB/photo | **always resident, all photos** |
| L1 | 128 px | embedded EXIF preview, else 1/8 decode | per-photo JPEG (~6 KB) | pool |
| L2 | 256 px | original, 1/8 decode | per-photo JPEG, lazy | pool |
| L3 | 512 px | original, 1/4 decode | per-photo JPEG, lazy | pool |
| L4 | 1024 px | original, 1/2 decode | per-photo JPEG, lazy | pool |
| Full | screen | original, smallest DCT scale ≥ needed | none | ≤ 3 single textures |

- **One read of the original produces all missing lazy levels** (decode at the scale
  the largest needs, then box-downscale the cascade 1024 → 512 → 256) and writes them.
- Level choice per visible photo: on-screen size in drawable pixels vs. the level's
  usable resolution (short edge for crop mode, long edge for fit/whole-photo), allowing
  up to ~1.4× upsampling, with hysteresis so levels don't thrash at boundaries.
- **Cache key**: hash of (absolute path, file size, mtime). Changed files get new keys.
- **Cache layout** (`<user-cache>/photobrowse/<library-hash>/`): `catalog.bin`
  (versioned header + records), `l0.pack` (versioned), `l<n>/<key[0:2]>/<key>.jpg`.
  Writes go to a temp file then atomic rename; a corrupt/old-version file is rebuilt.
- **Disk cache size cap** (default 4 GB, configurable) for lazy levels, LRU-swept by
  file access time at startup. For 10k photos: catalog ≈ 2 MB, L0 ≈ 40 MB, L1 ≈ 60 MB
  — the rest grows only with what is actually viewed.
- EXIF orientation is applied when producing every level (pixels stored upright);
  catalog width/height are post-orientation.

## 6. GPU texture organization
- Each pyramid level is a **paged atlas**: a set of 4096×4096 RGBA8 `GL_TEXTURE_2D`
  pages (one texture per page, since GL 4.1 can't grow an array texture cheaply) split into power-of-two slots (L0 16,384 slots/page, L1 1,024, L2 256, L3 64,
  L4 16). Aligned power-of-two slots make mip levels nest inside slots → mipmaps without
  bleeding (sample with a half-texel inset). Workers produce mips on the CPU; they are
  uploaded per level (no `glGenerateMipmap` on whole arrays). A page with mips ≈ 85 MB.
- **One shared VRAM budget** (default 640 MB ≈ 7 pages + full-res textures). L0 needs
  one page per 16k photos. Other levels get pages on demand from the budget; slots are
  recycled LRU among non-visible photos, and empty pages are released back to the
  budget so memory flows to whichever level the current zoom needs.
- When demand exceeds the budget, Residency lowers the desired level for the tiles
  farthest from the screen center first → softer, never stalled.
- Full-res: individual `GL_TEXTURE_2D`s (≤ 3: current whole-photo + neighbours).
- sRGB internal formats + sRGB framebuffer for correct filtering/blending.
- Future lever (also for 100k): BC1/BC7 compressed slots (4–8× less memory).

## 7. Rendering
- One **instanced draw** for all visible tiles: per-instance data = cell rect
  (camera-relative), photo aspect, content mode, and up to two sources
  `{level, page, slot}` plus a cross-fade factor (coarse → fine). Draws are batched
  per page texture (a handful of draw calls). Second pass for L3.
- **Whole-photo view**: the focused photo is drawn last, on top, with its rect and
  UV rect interpolated from the square cell/crop to the window-fitted/full rect; the
  grid gets a dim factor uniform. Focused-photo detail zoom scales that fitted rect
  in screen space and clamps its pan offset so an oversized axis continues to cover
  the viewport.
- Instance buffer rebuilt each frame from the visible list (≤ ~10k instances at full
  zoom-out; ~1 MB) with buffer orphaning — cheap.
- VSync on; adaptive vsync if available.
- Future effects: render the scene into an offscreen FBO and run a **post-process
  chain** (motion blur from camera velocity, vignette, grain, color grading). Per-tile
  effects plug into the tile shader via per-instance parameters. v1 renders directly
  but keeps the tile shader and frame composition in one place so the FBO is a small
  change.

## 8. Threading and streaming

```
 main thread (SDL events, camera, layout, residency decisions, GL uploads, draw)
     │ requests (photo, level, priority, generation)       ▲ results (pixels+mips)
     ▼                                                      │ lock-free/mutex queue
 JobSystem: N workers (hardware_concurrency - 1, min 2)
     read cache file  ─or─  read original + scaled decode → resize → mips → (write cache)
```
- **Main thread never does I/O or decoding.** It only uploads finished pixels, under a
  per-frame **upload budget** (e.g. ≤ 4 MB or ≤ 2 ms per frame), so loads can't hitch.
- Priority = level (coarser first) then distance from screen center; requests for
  photos that leave the view are cancelled via generation counters (workers check
  before expensive steps).
- Residency each frame: for each visible photo pick the desired level; if not
  resident, draw the best resident level and request the desired one. v1 also
  requests a one-ring margin around the viewport (cheap prefetch).
- Indexing (first run) runs on the same job system at background priority; visible
  photos' L0/L1 jump the queue.

## 9. Startup and library indexing
**Photo identity.** Each photo gets a stable `PhotoId` (index into the catalog's
record table, preserved across runs via the cache key). Layout position is a separate
mapping, so GPU slots, pending jobs and cancellation are keyed by `PhotoId` — this is
what later allows re-layout, new sort orders and search without invalidating caches.

**Warm start.** Load `catalog.bin` + `l0.pack` → build layout → upload L0 → full
frame (10k: ~42 MB sequential read). Then a background re-scan records differences,
which are merged on the **next** launch (no mid-session reshuffle).

**First run** (also used for new files found on a later launch):
1. *Scan*: recursive walk collecting `.jpg/.jpeg` path, size, mtime (seconds for 10k).
2. *Quick pass* (workers, read only the first ~64 KB of each file): parse EXIF for
   `DateTimeOriginal`, orientation, dimensions, and the **embedded preview JPEG**
   (Pixel photos carry a ~380×507 one in APP1). Decode it → L0 + L1. ~30 KB read per
   photo → 10k in a few seconds on SSD. Photos without a preview (e.g. edited copies)
   are queued for a 1/8 decode of the original instead.
3. Sort by date → write `catalog.bin` → build layout → grid is complete (L0 everywhere).
4. *Background*: 1/8-decode fallbacks; persist `l0.pack`, L1 files. Lazy levels are
   generated on demand while browsing, visible photos first, at higher priority than
   any background work.
- During step 1–2 the window shows a progress indicator (window title in v1).
- Unreadable/corrupt files are skipped with a log message.

## 10. Future features — how they slot in

**Predictive prefetch.** Add *predictors* that feed extra low-priority requests into
Residency: momentum landing point (analytic), tween targets, velocity-extrapolated
viewport (~0.5 s ahead), fly-to destination, idle warm-up of L1 in rings around the
view. Later: a dedicated I/O thread ordering reads to suit HDDs, `posix_fadvise` /
`PrefetchVirtualMemory`-style OS hints, and packing L1 into a few large files for
sequential reads.

**Database + search.** Replace/augment `catalog.bin` with SQLite in the cache dir:
`photos(id, path, name, taken_at, width, height, orientation, key, …)`, index on
`taken_at`, FTS5 table on names/keys/tags; later EXIF GPS, places, faces. Queries run
on a worker; results are photo ids → layout indices → world rects.

**Fly-to.** A `CameraPath` implementing van Wijk & Nuij's *smooth and efficient
zooming and panning* (optimal zoom-out/travel/zoom-in curve, duration ∝ path length,
clamped to ~1–2 s). For a group, target = bounding box of results. On start:
prefetch destination L1/L2. During flight: motion-blur post-effect scaled by
screen-space camera velocity; L0 is always resident, so nothing is ever empty — the
blur hides its low resolution. Result highlighting = per-instance shader parameter.

**Layouts.** New `Layout` implementations + `ContentMode`s; selection via CLI now,
settings/UI later. Non-grid layouts implement `visible()` with a spatial index
(uniform grid buckets).

**Shader effects.** FBO + post chain (§7); effect parameters exposed as uniforms; the
GLSL lives in `shaders/` and could be hot-reloaded in debug builds.

**Scaling to 50k–100k photos** (deferred; v1 keeps the door open): L0 at 16 px or
BC1-compressed (100k × 16 px ≈ 100 MB raw / 13 MB BC1); memory-map `l0.pack` and
upload only pages near the view first; catalog in SQLite instead of one blob;
layout `visible()` already O(visible); per-frame instance count at full zoom-out
capped by drawing a pre-rendered overview mosaic texture instead of 100k instances.

**Other formats.** `ImageCodec` interface with JPEG first; PNG/HEIC/RAW decoders later.

## 11. Testing and measurement
- Unit tests (no GPU): layout math & visibility & hit-testing, camera transforms and
  pivot-zoom invariance, momentum/tween math, level selection + hysteresis, EXIF
  parsing (fixtures from `photos/`), cache key, catalog round-trip, pack file I/O.
- `tools/make_test_library` builds a 10k-photo library of links to `photos/` with
  distinct names, for scale/perf runs. Note: links keep sources in the OS file cache,
  so first-run I/O timings need a real library; frame-rate testing is unaffected.
- `F3` stats + `--bench` mode (scripted pan/zoom path, prints frame-time percentiles)
  to verify the performance targets.

## 12. Portability notes
- Filesystem via `std::filesystem`; paths stored as UTF-8; on Windows open files via
  wide paths. No POSIX-only calls outside `platform/`.
- Build presets: `linux-debug`, `linux-release`, `windows-msvc-release` (vcpkg toolchain).
- Windows is architected for and buildable, but verified on Linux only until a
  Windows machine/CI is available.
