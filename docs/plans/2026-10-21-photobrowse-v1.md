# PhotoBrowse v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A cross-platform C++20 desktop app that shows a folder of JPEGs as one smooth, zoomable, time-ordered square grid with momentum panning, click-to-center, whole-photo view, fullscreen, and a progressive on-disk/GPU thumbnail pyramid.

**Architecture:** Pure, unit-tested core (`pb_core`: geometry, camera, motion, interaction, layout, EXIF, catalog, scanner, cache files, job system, residency) with no SDL/GL; `pb_imaging` wraps libjpeg-turbo; `pb_gfx` owns OpenGL 4.1 (paged texture arrays + one instanced draw); the `photobrowse` executable wires SDL3 events, the indexer and streaming together. Main thread never does I/O or decoding.

**Tech Stack:** C++20, CMake ≥ 3.24 + Ninja + CMakePresets, SDL3, OpenGL 4.1 core (hand-written loader, vendored Khronos headers), libjpeg-turbo TurboJPEG v2 API (2.1.x on Ubuntu), doctest (vendored), vcpkg on Windows.

**Spec:** `docs/spec.md` and `docs/architecture.md` (read both before starting any task).

## Global Constraints

- C++20; compiles warning-free with `-Wall -Wextra` on GCC 15 and Clang 20; MSVC `/W4` must not be broken by POSIX-only code (POSIX calls only in `src/platform/`).
- Dependencies: SDL3, libjpeg-turbo only. Everything else vendored in `third_party/` or written in-house. **Never install system packages** — the user installs `libsdl3-dev` and `libturbojpeg0-dev`. If they are missing, CMake must still configure and build/test `pb_core` (warning, skip other targets).
- OpenGL **4.1 core**, GLSL `#version 410 core`. No `glTexStorage*`, no compute, no bindless.
- The photo folder is **read-only**. Cache lives in `<user-cache>/photobrowse/<library-hash>/` (`$XDG_CACHE_HOME` or `~/.cache`; `%LOCALAPPDATA%` on Windows), overridable by `--cache-dir`.
- Pyramid slot sizes exactly `{32, 128, 256, 512, 1024}` + full level; L0 pack + L1 made at index time; L2–L4 lazy; page size 4096×4096 RGBA8 (sRGB).
- Default GPU budget **640 MB** (all levels + mips + full-res); default disk-cache cap for lazy levels **4 GB**.
- Interaction constants: click < 4 px movement; double-click ≤ 350 ms; click-to-center tween 400 ms ease-in-out cubic; cross-fade 150 ms; whole-photo transition 350 ms; grid gap 3% per side; start scale = cell is 1/3 of the window's shorter side; max grid scale = cell fills shorter side; min scale = whole grid fits with 5% margin.
- Upload budget: ≤ 4 MB of texel uploads per frame (always at least one item).
- Main thread: no file I/O (except startup catalog/pack load before the first full frame) and no JPEG decoding.
- Library ordering: `(takenAt, relPath)` ascending, left→right, row by row; `cols = ceil(sqrt(N))`, `rows = ceil(N/cols)`.
- Folder changes found by the background re-scan apply on the next launch only.
- Commit after every task with a conventional message ending in `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Photos with odd or missing metadata** (no EXIF date, orientation 6/8, no embedded preview, truncated or non-JPEG data named `.jpg`) → still appear with sensible date and orientation, or are skipped with a log line; never crash. Tests: Task 4 (`exif: truncated and garbage input`), Task 8 (`indexer: file without preview falls back`).
2. **Tiny and non-square libraries** (0, 1, 2, 20 photos) → no crash; empty trailing cells are not hittable; empty folder shows an empty canvas and a message in the title. Tests: Task 2 (`grid: small counts`, `grid: hit on empty cell`).
3. **Window resize / fullscreen toggle / HiDPI change mid-animation** → the photo at the screen center stays centered; zoom limits recomputed and target scale clamped. Test: Task 3 (`interaction: viewport change keeps center`).
4. **Corrupt or old-version cache files, missing photo folder** → cache rebuilt silently; missing folder gives a clear stderr message and non-zero exit. Tests: Task 5 (`catalog: bad magic / truncated`), Task 6 (`l0pack: truncated`).
5. **Fast flings across a large library while loads are queued** → requests for photos no longer near the view are cancelled; queued work does not grow without bound. Test: Task 9 (`residency: stale requests are cancelled`).

---

## File Structure

```
CMakeLists.txt  CMakePresets.json  vcpkg.json  .gitignore  README.md  CLAUDE.md
cmake/EmbedShaders.cmake
third_party/doctest/doctest.h
third_party/khronos/GL/glcorearb.h  third_party/khronos/KHR/khrplatform.h
src/core/geom.h                 Vec2d, Rect, UvRect
src/core/levels.h               slot sizes, level constants
src/view/camera.{h,cpp}         Camera
src/view/motion.{h,cpp}         easing, Tween, SmoothValue, VelocityTracker, Momentum
src/view/interaction.{h,cpp}    input state machine -> camera + focus
src/layout/layout.h             Layout interface, ContentMode, Placement
src/layout/square_grid.{h,cpp}  SquareGridLayout
src/layout/content.{h,cpp}      fitRect, cropUv
src/library/exif.{h,cpp}        JPEG header + EXIF parser
src/library/catalog.{h,cpp}     PhotoRecord, Catalog, cacheKey, save/load
src/library/scanner.{h,cpp}     scanFolder, diffCatalog
src/platform/paths.{h,cpp}      defaultCacheRoot, libraryCacheDir, atomicWriteFile, readFileRange
src/cache/l0_pack.{h,cpp}       l0.pack read/write
src/cache/thumb_cache.{h,cpp}   per-level JPEG files, sweep
src/stream/job_system.{h,cpp}   JobSystem, ResultQueue
src/stream/slot_allocator.{h,cpp}  SlotAllocator, VramBudget
src/stream/residency.{h,cpp}    desiredLevel, Residency
src/imaging/image.h             Image
src/imaging/jpeg.{h,cpp}        decodeJpeg, encodeJpeg (TurboJPEG)
src/imaging/resize.{h,cpp}      downscaleBox, fitToLongEdge, applyOrientation, buildMips
src/library/indexer.{h,cpp}     quickIndex, buildLazyLevels
src/gfx/gl.{h,cpp}              GL function loader
src/gfx/shader.{h,cpp}          compile/link
src/gfx/paged_texture.{h,cpp}   GL_TEXTURE_2D_ARRAY pages per level
src/gfx/tile_renderer.{h,cpp}   instanced draw
shaders/tile.vert  shaders/tile.frag
src/app/app.{h,cpp}             SDL window, loop, fullscreen, stats, bench
src/app/streamer.{h,cpp}        jobs <-> residency <-> uploads
src/app/options.{h,cpp}         CLI parsing
src/main.cpp
tests/test_*.cpp                one file per core module
tools/make_test_library.py
```

Targets: `pb_core` (static, no deps), `pb_imaging` (static, turbojpeg; also holds `library/indexer`), `pb_gfx` (static, GL headers only), `photobrowse` (exe, SDL3), `pb_tests` (doctest; core tests always, imaging tests when turbojpeg found).

Build/test commands used throughout:
```
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --test-dir build/linux-debug --output-on-failure
build/linux-debug/pb_tests --test-case="<name>"
```

---

### Task 1: Project scaffold, build system, docs skeleton

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `cmake/EmbedShaders.cmake`, `third_party/…`, `src/core/geom.h`, `src/core/levels.h`, `tests/test_main.cpp`, `tests/test_geom.cpp`, `README.md`, `CLAUDE.md`
- Modify: `.gitignore` (currently contains the typo `note.xt` — change to `note.txt`, add `build/`, `.cache/`)

**Interfaces:**
- Produces (`src/core/geom.h`, namespace `pb`):
  `struct Vec2d { double x = 0, y = 0; }` with `+ - *(double) /(double) ==`, `double length(Vec2d)`;
  `struct Rect { double x0, y0, x1, y1; double width() const; double height() const; Vec2d center() const; bool contains(Vec2d) const; bool intersects(const Rect&) const; Rect inset(double d) const; }`;
  `struct UvRect { float u0 = 0, v0 = 0, u1 = 1, v1 = 1; }`.
  Coordinate convention everywhere: origin top-left, +y down (screen and world).
- Produces (`src/core/levels.h`): `constexpr int kLevelCount = 5; constexpr uint32_t kSlotSize[kLevelCount] = {32,128,256,512,1024}; constexpr int kFullLevel = 5; constexpr uint32_t kPageSize = 4096;`
- Produces (CMake): targets above; `PB_TEST_PHOTOS_DIR` compile definition on `pb_tests` = `${CMAKE_SOURCE_DIR}/photos`; option `PB_HAVE_TURBOJPEG`, `PB_HAVE_SDL3` set by detection; function `pb_embed_shaders(target OUT_HEADER files...)` generating a header with `inline constexpr const char* kShader_<name>_<ext> = R"glsl(...)glsl";`.

- [ ] **Step 1:** `git init`, fix `.gitignore`. Vendor: copy `/usr/include/GL/glcorearb.h` and `/usr/include/KHR/khrplatform.h` into `third_party/khronos/`; download doctest v2.4.12 `doctest.h` into `third_party/doctest/`.
- [ ] **Step 2: Write the failing test** `tests/test_geom.cpp`:
```cpp
TEST_CASE("geom: rect basics") {
  pb::Rect r{1, 2, 5, 4};
  CHECK(r.width() == 4); CHECK(r.height() == 2);
  CHECK(r.center() == pb::Vec2d{3, 3});
  CHECK(r.contains({1, 2})); CHECK_FALSE(r.contains({5.5, 3}));
  CHECK(r.intersects({4, 3, 9, 9})); CHECK_FALSE(r.intersects({6, 0, 7, 1}));
  CHECK(r.inset(0.5).x0 == doctest::Approx(1.5));
}
```
- [ ] **Step 3:** Write CMake: `cmake_minimum_required(3.24)`, C++20, warnings; SDL3 via `find_package(SDL3 CONFIG)`; turbojpeg via `find_package(libjpeg-turbo CONFIG)` (target `libjpeg-turbo::turbojpeg`, vcpkg) falling back to `pkg_check_modules(TURBOJPEG IMPORTED_TARGET libturbojpeg)`; missing deps → `message(WARNING)` and skip dependent targets. Presets: `linux-debug`, `linux-release` (Ninja, `build/<preset>`), `windows-msvc-release` (Ninja, `cl`, `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`). `vcpkg.json` deps: `sdl3`, `libjpeg-turbo`.
- [ ] **Step 4:** Implement `geom.h`; configure, build, run `ctest` → PASS (works even without SDL3/turbojpeg installed).
- [ ] **Step 5:** `README.md`: what it is, dependencies (Ubuntu: `sudo apt install libsdl3-dev libturbojpeg0-dev`; Windows: VS 2022 + vcpkg), build/run/test commands, controls table (copy from spec §3.2), cache location. `CLAUDE.md`: points to `docs/spec.md`, `docs/architecture.md`, `docs/plans/`; build/test commands; rules: don't install deps, photo folder read-only, no I/O on main thread, keep `pb_core` free of SDL/GL.
- [ ] **Step 6: Commit** `chore: project scaffold, build presets, docs`

### Task 2: Layout and content placement

**Files:** Create `src/layout/layout.h`, `src/layout/square_grid.{h,cpp}`, `src/layout/content.{h,cpp}`; Test `tests/test_layout.cpp`

**Interfaces:**
- Produces:
  - `enum class ContentMode { Crop, Fit };` `struct Placement { Rect cell; Rect photo; };` (photo = cell inset by gap)
  - `class Layout { public: virtual ~Layout() = default; virtual void build(uint32_t count) = 0; virtual uint32_t count() const = 0; virtual Rect bounds() const = 0; virtual Placement place(uint32_t pos) const = 0; virtual void visible(const Rect& world, std::vector<uint32_t>& out) const = 0; virtual std::optional<uint32_t> hit(Vec2d world) const = 0; virtual uint32_t centerPosition() const = 0; virtual ContentMode contentMode() const = 0; };` — positions are indices into `Catalog::order`.
  - `class SquareGridLayout final : public Layout { explicit SquareGridLayout(double gap = 0.03); uint32_t cols() const; uint32_t rows() const; … };` `visible` iterates only the overlapping row/col range (O(visible)); `hit` returns nullopt outside the photo rect or for empty trailing cells; `centerPosition` = occupied cell nearest the grid's geometric center.
  - `Rect fitRect(const Rect& box, double aspect);` (largest centered rect of `aspect`=w/h inside box)
  - `UvRect cropUv(double imageAspect, double targetAspect);` (centered sub-rect of [0,1]² with `targetAspect`)
  - `std::unique_ptr<Layout> makeLayout(std::string_view name);` — `"square-crop"` only; nullptr otherwise.

- [ ] **Step 1: Write the failing tests**
```cpp
TEST_CASE("grid: 20 photos is 5x4, 10000 is 100x100") { pb::SquareGridLayout g; g.build(20); CHECK(g.cols()==5); CHECK(g.rows()==4); g.build(10000); CHECK(g.cols()==100); CHECK(g.rows()==100); }
TEST_CASE("grid: small counts") { pb::SquareGridLayout g; for (uint32_t n : {0u,1u,2u}) { g.build(n); std::vector<uint32_t> v; g.visible({-10,-10,10,10}, v); CHECK(v.size()==n); } }
TEST_CASE("grid: placement row-major with gap") { pb::SquareGridLayout g; g.build(20); auto p = g.place(7); CHECK(p.cell.x0==2); CHECK(p.cell.y0==1); CHECK(p.photo.x0==doctest::Approx(2.03)); }
TEST_CASE("grid: hit on empty cell") { pb::SquareGridLayout g; g.build(18); CHECK(g.hit({2.5,1.5}).value()==7); CHECK_FALSE(g.hit({4.5,3.5})); /* trailing empty cell */ CHECK_FALSE(g.hit({2.01,1.5})); /* in gap */ CHECK_FALSE(g.hit({-1,0})); }
TEST_CASE("grid: visible is a tight range") { pb::SquareGridLayout g; g.build(10000); std::vector<uint32_t> v; g.visible({10.5,20.5,12.5,21.5}, v); CHECK(v.size()==6); }
TEST_CASE("grid: center position") { pb::SquareGridLayout g; g.build(20); CHECK(g.centerPosition()==7); /* (2,1) and (2,2) tie → lower */ g.build(10000); CHECK(g.centerPosition()==4949); /* four cells tie around (50,50) → lowest (49,49) */ }
TEST_CASE("content: crop portrait to square") { auto uv = pb::cropUv(3.0/4.0, 1.0); CHECK(uv.u0==0); CHECK(uv.v0==doctest::Approx(0.125)); CHECK(uv.v1==doctest::Approx(0.875)); }
TEST_CASE("content: fit landscape into square") { auto r = pb::fitRect({0,0,1,1}, 4.0/3.0); CHECK(r.y0==doctest::Approx(0.125)); CHECK(r.x1==1); }
```
`centerPosition` = occupied cell whose center is nearest `bounds().center()`; ties → lowest position.
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement. **Step 4:** Run → PASS.
- [ ] **Step 5: Commit** `feat(layout): square grid layout and content placement`

### Task 3: Camera, motion and interaction state machine

**Files:** Create `src/view/camera.{h,cpp}`, `src/view/motion.{h,cpp}`, `src/view/interaction.{h,cpp}`; Test `tests/test_camera.cpp`, `tests/test_motion.cpp`, `tests/test_interaction.cpp`

**Interfaces:**
- Consumes: Task 1 geom; Task 2 `Layout` / `SquareGridLayout` (interaction and its tests).
- Produces:
  - `class Camera { public: Vec2d center; double scale = 100; Vec2d viewport{1280,720}; Vec2d worldToScreen(Vec2d) const; Vec2d screenToWorld(Vec2d) const; Rect visibleWorld() const; void zoomAbout(Vec2d screenPivot, double newScale); };` (scale = drawable px per world unit; screen center = viewport/2 maps to `center`)
  - `double easeInOutCubic(double t);` `struct Tween { double from, to, t0, duration; double value(double now) const; bool done(double now) const; };`
  - `struct SmoothValue { double current, target; double rate = 18; void step(double dt); };` — `current += (target-current)*(1-exp(-rate*dt))`, snaps when |diff| < 1e-6·|target|.
  - `class VelocityTracker { void reset(); void add(double t, Vec2d p); Vec2d velocity(double now) const; };` — average over samples in the last 50 ms; zero if last sample older than 80 ms.
  - `struct Momentum { Vec2d velocity; double lambda = 5.0; bool active = false; void start(Vec2d v); Vec2d step(double dt); Vec2d landingOffset() const; };` — `step` returns displacement and decays velocity by `exp(-lambda*dt)`; stops below 5 px/s; landing = `v/lambda`. Start only if |v| ≥ 50 px/s.
  - `enum class Key { Left, Right, Up, Down, ZoomIn, ZoomOut, Home, Enter, Escape };`
  - `class Interaction { public: Interaction(Camera&, const Layout&); void setViewport(Vec2d px); void resetView(); void onPointerDown(Vec2d px, double t); void onPointerMove(Vec2d px, double t); void onPointerUp(Vec2d px, double t); void onWheel(Vec2d px, double notches); bool onKey(Key k, double t); /* false if unhandled (Escape when not focused) */ void update(double t, double dt); std::optional<uint32_t> focused() const; double focusProgress() const; /* 0..1 */ bool idle() const; double minScale() const; double maxScale() const; Vec2d cameraVelocityPx() const; };`
  - Behavior: drag pans 1:1 and cancels tweens/momentum; release → momentum; click (< 4 px) on a photo → 400 ms center tween; double-click (second click ≤ 350 ms, same photo) or `Enter` on the centered photo or wheel-in while target already at `maxScale` over a photo → focus that position (350 ms progress tween, camera tweens to center it); in focus: Left/Right step position ±1 (clamped to [0,count)) with center tween; Escape / wheel-out / drag start / click outside the focused rect → unfocus. Wheel: `target *= 1.25^notches` clamped, zoom about cursor via `SmoothValue` on log-scale; `Home` → `resetView()` tween; `+`/`-` → wheel about screen center; arrows (grid mode) → center tween ±1 cell.

- [ ] **Step 1: Write the failing tests**
```cpp
TEST_CASE("camera: round trip and pivot zoom invariance") {
  pb::Camera c; c.center = {10, 20}; c.scale = 50; c.viewport = {800, 600};
  CHECK(c.worldToScreen({10, 20}) == pb::Vec2d{400, 300});
  auto w = c.screenToWorld({123, 456});
  CHECK(c.worldToScreen(w).x == doctest::Approx(123));
  c.zoomAbout({123, 456}, 200);
  CHECK(c.screenToWorld({123, 456}).x == doctest::Approx(w.x));
  CHECK(c.screenToWorld({123, 456}).y == doctest::Approx(w.y));
}
TEST_CASE("motion: easing endpoints and symmetry") {
  CHECK(pb::easeInOutCubic(0) == 0); CHECK(pb::easeInOutCubic(1) == 1);
  CHECK(pb::easeInOutCubic(0.5) == doctest::Approx(0.5));
}
TEST_CASE("motion: momentum travels to analytic landing point") {
  pb::Momentum m; m.start({500, 0}); auto land = m.landingOffset();
  pb::Vec2d sum{}; for (int i = 0; i < 2000 && m.active; ++i) sum = sum + m.step(1.0 / 120);
  CHECK(sum.x == doctest::Approx(land.x).epsilon(0.02)); CHECK_FALSE(m.active);
}
TEST_CASE("motion: smooth value is frame-rate independent") {
  pb::SmoothValue a{0, 1}, b{0, 1};
  for (int i = 0; i < 6; ++i) a.step(1.0 / 60);
  for (int i = 0; i < 12; ++i) b.step(1.0 / 120);
  CHECK(a.current == doctest::Approx(b.current));
}
// Interaction tests use SquareGridLayout(20) and viewport {1000, 800}:
TEST_CASE("interaction: start view centers the center photo at 1/3 short side") // scale == 800/3; camera.center == place(centerPosition()).cell.center()
TEST_CASE("interaction: small move is a click and centers that photo") // down/up 2px apart on cell (1,1) → after update(t+0.5) camera.center == cell(1,1).center()
TEST_CASE("interaction: drag pans 1:1 and is not a click") // drag 100px right → center.x decreased by 100/scale; no tween started
TEST_CASE("interaction: fling starts momentum, grab stops it")
TEST_CASE("interaction: wheel zoom keeps point under cursor and clamps to limits")
TEST_CASE("interaction: double-click focuses, escape unfocuses") // focused()==pos, focusProgress()→1 after 0.35s; onKey(Escape) returns true and progress→0
TEST_CASE("interaction: escape when not focused is unhandled") // onKey(Escape) == false
TEST_CASE("interaction: focus left/right steps and clamps")
TEST_CASE("interaction: viewport change keeps center") // setViewport({1600,900}) mid-tween: world point at screen center unchanged, scale clamped into new limits
```
- [ ] **Step 2:** Run → FAIL (not defined).
- [ ] **Step 3:** Implement the three units per Interfaces.
- [ ] **Step 4:** Run `ctest` → PASS.
- [ ] **Step 5: Commit** `feat(view): camera, motion and interaction`

### Task 4: JPEG header / EXIF parser

**Files:** Create `src/library/exif.{h,cpp}`; Test `tests/test_exif.cpp`

**Interfaces:**
- Produces:
  - `struct JpegHeaderInfo { std::optional<std::string> dateTimeOriginal; int orientation = 1; uint32_t width = 0, height = 0; uint64_t thumbOffset = 0, thumbLength = 0; bool sawSof = false; };`
  - `std::optional<JpegHeaderInfo> parseJpegHeader(std::span<const uint8_t> head);` — nullopt if not starting with FFD8. Walk markers to SOS. APP1 `Exif\0\0`: TIFF (II/MM), IFD0 Orientation (0x0112), ExifIFD (0x8769) → DateTimeOriginal (0x9003); IFD1 0x0201/0x0202 → thumbnail offset/length (offset is relative to TIFF header; store absolute file offset). SOF0/1/2 → width/height (pre-orientation). Every read bounds-checked; truncated data returns what was found so far (`sawSof=false` if SOF not reached).
  - `std::optional<int64_t> parseExifDateTime(std::string_view s);` — `"YYYY:MM:DD HH:MM:SS"` → seconds since 1970 treating the naive time as UTC (days-from-civil). Rejects malformed/`0000:00:00`.

- [ ] **Step 1: Write the failing tests** (fixtures read first 64 KB from `PB_TEST_PHOTOS_DIR`)
```cpp
TEST_CASE("exif: pixel photo") { auto i = parse("PXL_20260321_003010085.jpg");
  CHECK(*i->dateTimeOriginal == "2026:03:20 20:30:10"); CHECK(i->orientation == 1);
  CHECK(i->width == 3072); CHECK(i->height == 4080);
  CHECK(i->thumbOffset == 1310); CHECK(i->thumbLength == 23268); }
TEST_CASE("exif: edited copy has no embedded preview") { auto i = parse("PXL_20260321_012354015~2.jpg"); CHECK(i->thumbLength == 0); CHECK(i->width == 3048); CHECK(i->height == 3418); }
TEST_CASE("exif: datetime parse") { CHECK(*pb::parseExifDateTime("1970:01:02 00:00:01") == 86401); CHECK_FALSE(pb::parseExifDateTime("0000:00:00 00:00:00")); CHECK_FALSE(pb::parseExifDateTime("garbage")); }
TEST_CASE("exif: truncated and garbage input") { auto full = readHead("PXL_20260321_003010085.jpg");
  for (size_t n : {0, 1, 2, 3, 10, 100, 500, 1400}) CHECK_NOTHROW(pb::parseJpegHeader({full.data(), n}));
  std::vector<uint8_t> junk(4096, 0xFF); CHECK_NOTHROW(pb::parseJpegHeader(junk));
  std::vector<uint8_t> notJpeg{'P','N','G'}; CHECK_FALSE(pb::parseJpegHeader(notJpeg)); }
TEST_CASE("exif: big-endian and orientation 6") // build a minimal synthetic MM TIFF APP1 in the test with Orientation=6; CHECK(i->orientation==6)
```
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement. **Step 4:** Run → PASS (also under `-fsanitize=address,undefined` for this test: add `linux-asan` preset).
- [ ] **Step 5: Commit** `feat(library): JPEG header and EXIF parser`

### Task 5: Catalog, scanner, platform paths

**Files:** Create `src/library/catalog.{h,cpp}`, `src/library/scanner.{h,cpp}`, `src/platform/paths.{h,cpp}`; Test `tests/test_catalog.cpp`, `tests/test_scanner.cpp`

**Interfaces:**
- Produces:
  - `using PhotoId = uint32_t;`
  - `struct PhotoRecord { std::string relPath; uint64_t fileSize = 0; int64_t mtime = 0; int64_t takenAt = 0; uint32_t width = 0, height = 0; uint8_t orientation = 1; uint8_t flags = 0; uint64_t key = 0; uint64_t thumbOffset = 0, thumbLength = 0; double aspect() const; /* w/h post-orientation, 1.0 if unknown */ };` flags: `kBroken = 1`, `kNeedsDecodeL1 = 2`.
  - `uint64_t cacheKey(std::string_view absPathUtf8, uint64_t size, int64_t mtime);` — FNV-1a 64 over the bytes of all three.
  - `struct Catalog { std::vector<PhotoRecord> photos; std::vector<PhotoId> order; void sortOrder(); };` — order sorted by `(takenAt, relPath)`; broken photos excluded from `order`.
  - `bool saveCatalog(const Catalog&, const std::filesystem::path&); std::optional<Catalog> loadCatalog(const std::filesystem::path&);` — magic `"PBCAT\0\0\1"`, little-endian, length-prefixed strings; nullopt on bad magic/version/truncation.
  - `struct ScanEntry { std::string relPath; uint64_t size; int64_t mtime; };` `std::vector<ScanEntry> scanFolder(const std::filesystem::path& root);` (recursive, `.jpg/.jpeg` case-insensitive, relPath UTF-8 with `/`, sorted; unreadable entries skipped).
  - `struct ScanDiff { std::vector<ScanEntry> added; std::vector<PhotoId> removed; std::vector<PhotoId> changed; bool empty() const; };` `ScanDiff diffCatalog(const Catalog&, const std::vector<ScanEntry>&);`
  - `paths.h`: `std::filesystem::path defaultCacheRoot();` `std::filesystem::path libraryCacheDir(const std::filesystem::path& cacheRoot, const std::filesystem::path& libraryAbs);` (`<root>/photobrowse/<16 hex of FNV(abs path)>`), `bool atomicWriteFile(const std::filesystem::path&, std::span<const uint8_t>);` (write `.tmp` + rename), `std::optional<std::vector<uint8_t>> readFileRange(const std::filesystem::path&, uint64_t offset, uint64_t maxBytes);`, `int64_t fileMtimeSeconds(const std::filesystem::path&);`

- [ ] **Step 1: Write the failing tests**
```cpp
TEST_CASE("catalog: sort by time then name, broken excluded")
TEST_CASE("catalog: save/load round trip") // all fields equal incl. unicode relPath "Résumé/日本.jpg"
TEST_CASE("catalog: bad magic / truncated") // file "XXXX" → nullopt; valid file cut to half → nullopt
TEST_CASE("catalog: cache key changes with mtime and size")
TEST_CASE("scanner: recursive, extension filter, unicode") // temp dir: a.JPG, sub/b.jpeg, sub/ø.jpg, c.png, d.txt → 3 entries sorted
TEST_CASE("scanner: diff detects added, removed, changed")
TEST_CASE("paths: atomic write replaces and leaves no tmp")
TEST_CASE("paths: library cache dir is stable and distinct per library")
```
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement (temp dirs via `std::filesystem::temp_directory_path()/unique`). **Step 4:** Run → PASS.
- [ ] **Step 5: Commit** `feat(library): catalog, scanner and cache paths`

### Task 6: Imaging (TurboJPEG) and on-disk thumbnail cache

Requires `libturbojpeg0-dev`. If missing, stop and ask the user to install it.

**Files:** Create `src/imaging/image.h`, `src/imaging/jpeg.{h,cpp}`, `src/imaging/resize.{h,cpp}`, `src/cache/l0_pack.{h,cpp}`, `src/cache/thumb_cache.{h,cpp}`; Test `tests/test_imaging.cpp`, `tests/test_cache.cpp`

**Interfaces:**
- Produces:
  - `struct Image { uint32_t w = 0, h = 0; std::vector<uint8_t> rgba; bool empty() const; };`
  - `std::optional<Image> decodeJpeg(std::span<const uint8_t> data, uint32_t minLongEdge = 0);` — chooses the smallest of scales {1/8,1/4,1/2,1} whose long edge ≥ `minLongEdge` (0 → full); output RGBA; thread-safe (one tjhandle per thread via `thread_local`).
  - `std::optional<std::vector<uint8_t>> encodeJpeg(const Image&, int quality = 88);`
  - `Image downscaleBox(const Image&, uint32_t w, uint32_t h);` (area average, any ratio ≥ 1) · `Image fitToLongEdge(const Image&, uint32_t longEdge);` (downscale only; returns copy if already ≤) · `Image applyOrientation(const Image&, int exifOrientation);` (1–8) · `std::vector<Image> buildMips(const Image&);` (level 0 = input, halve (ceil) until 1×1)
  - `bool writeL0Pack(const std::filesystem::path&, const std::vector<std::pair<uint64_t, Image>>&);` `std::optional<std::vector<std::pair<uint64_t, Image>>> readL0Pack(const std::filesystem::path&);` — magic `"PBL0\0\0\0\1"`, count, then per entry `key u64, w u16, h u16, 32*32*4 bytes` (image top-left in the 32×32 block).
  - `class ThumbCache { public: explicit ThumbCache(std::filesystem::path dir); std::filesystem::path levelPath(int level, uint64_t key) const; /* dir/l<level>/<hh>/<16hex>.jpg */ bool write(int level, uint64_t key, const Image&) const; std::optional<Image> read(int level, uint64_t key) const; /* updates mtime */ uint64_t sweep(uint64_t maxBytes) const; /* deletes oldest lazy files (levels ≥2) until under cap; returns bytes freed */ };`

- [ ] **Step 1: Write the failing tests**
```cpp
TEST_CASE("jpeg: scaled decode picks smallest sufficient scale") { auto d = file("PXL_20260321_003010085.jpg");
  auto i = pb::decodeJpeg(d, 500); CHECK(i->h == 510); CHECK(i->w == 384);   // 1/8
  auto j = pb::decodeJpeg(d, 1000); CHECK(j->h == 1020);                     // 1/4
  CHECK_FALSE(pb::decodeJpeg(std::vector<uint8_t>{1,2,3})); }
TEST_CASE("jpeg: embedded preview decodes") // bytes [1310, 1310+23268) → 382x507
TEST_CASE("resize: box average of 2x2 block") // 2x2 {0,255,255,0 gray} → 1x1 ≈ 127/128
TEST_CASE("resize: fit to long edge keeps aspect") // 384x510 → 128 long: 96x128
TEST_CASE("resize: orientation 6 rotates 90 cw") // 2x1 image → 1x2, pixel order checked
TEST_CASE("resize: mips go to 1x1") // 100x40 → 7 levels, last 1x1
TEST_CASE("l0pack: round trip") TEST_CASE("l0pack: truncated") // → nullopt
TEST_CASE("thumbcache: write/read and sweep removes oldest lazy files only")
```
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement. **Step 4:** Run → PASS.
- [ ] **Step 5: Commit** `feat(imaging): turbojpeg codec, resize, l0 pack and thumb cache`

### Task 7: Job system

**Files:** Create `src/stream/job_system.{h,cpp}`; Test `tests/test_jobs.cpp`

**Interfaces:**
- Produces: `class JobSystem { public: explicit JobSystem(unsigned threads); ~JobSystem(); /* stops, drops pending, joins */ void submit(int64_t priority, std::function<void()> job); /* lower runs sooner; FIFO within equal priority */ size_t pending() const; void clearPending(); };` `unsigned defaultWorkerCount();` (= max(2, hw-1)). `template <class T> class ResultQueue { public: void push(T); std::vector<T> drain(size_t max = SIZE_MAX); size_t size() const; };` `struct CancelToken { std::atomic<bool> cancelled{false}; };`

- [ ] **Step 1: Failing tests:** `jobs: all jobs run`, `jobs: priority order with one worker` (block the worker with a latch, submit 5,1,3 → run order 1,3,5), `jobs: clearPending drops queued work`, `jobs: destructor with pending work does not hang`, `results: drain respects max`.
- [ ] **Step 2–4:** FAIL → implement (mutex + condvar + `std::priority_queue` keyed by (priority, seq), `std::jthread`) → PASS.
- [ ] **Step 5: Commit** `feat(stream): job system and result queue`

### Task 8: Indexer (quick pass + lazy levels)

**Files:** Create `src/library/indexer.{h,cpp}` (in `pb_imaging`); Test `tests/test_indexer.cpp`

**Interfaces:**
- Consumes: Tasks 4–6.
- Produces:
  - `struct QuickResult { PhotoRecord rec; Image l0, l1; /* empty if fallback needed */ };`
  - `QuickResult quickIndex(const std::filesystem::path& root, const ScanEntry& e);` — read first 64 KB (re-read up to 512 KB if the preview or SOF lies beyond); fill record (`takenAt` from EXIF else mtime; width/height swapped for orientation 5–8; `key` from abs path); decode embedded preview → orient → `fitToLongEdge` 128 (L1) and 32 (L0). No preview → set `kNeedsDecodeL1`. Not a JPEG → `kBroken`.
  - `bool decodeL0L1FromOriginal(const std::filesystem::path& abs, PhotoRecord&, Image& l0, Image& l1);` — 1/8 decode path.
  - `std::array<Image, kLevelCount> buildLazyLevels(const std::filesystem::path& abs, const PhotoRecord&, int highestLevel);` — one decode with `minLongEdge = kSlotSize[highestLevel]`, orient, fit to that slot, cascade-downscale for levels 2..highest-1 (index entries below 2 left empty).
  - `std::optional<Image> decodeFull(const std::filesystem::path& abs, const PhotoRecord&, uint32_t minLongEdge);`

- [ ] **Step 1: Failing tests:** `indexer: pixel photo quick pass uses preview` (date 2026-03-20 20:30:10 → takenAt; l1 is 96×128; l0 is 24×32; no `kNeedsDecodeL1`), `indexer: file without preview falls back` (`~2` file → flag set; `decodeL0L1FromOriginal` gives l1 long edge 128), `indexer: non-jpeg is broken` (temp file "x.jpg" with text), `indexer: lazy levels cascade` (highest=4 → [4] long edge 1024, [3] 512, [2] 256).
- [ ] **Step 2–4:** FAIL → implement → PASS.
- [ ] **Step 5: Commit** `feat(library): quick-pass indexer and lazy level builder`

### Task 9: Residency (level choice, slots, budget, LRU)

**Files:** Create `src/stream/slot_allocator.{h,cpp}`, `src/stream/residency.{h,cpp}`; Test `tests/test_residency.cpp`

**Interfaces:**
- Produces:
  - `struct SlotRef { uint16_t page = 0; uint16_t slot = 0; };`
  - `class SlotAllocator { public: explicit SlotAllocator(uint32_t slotSize); uint32_t slotsPerPage() const; uint32_t pageCount() const; std::optional<SlotRef> allocate(); void addPage(); void release(SlotRef); bool pageEmpty(uint16_t page) const; UvRect uv(SlotRef, uint32_t w, uint32_t h) const; /* content rect in page UV, half-texel inset */ Vec2d origin(SlotRef) const; /* texel x,y */ };`
  - `constexpr size_t kPageBytes = size_t(kPageSize) * kPageSize * 4 * 4 / 3;`
  - `int desiredLevel(double neededPx, int current);` — `neededPx` = on-screen px the image must supply along its stored long edge (caller divides by usable fraction). Smallest L with `kSlotSize[L]*1.4 >= neededPx`, else `kFullLevel`. Hysteresis: keep `current` if `current` > result and `neededPx > kSlotSize[current-1]*1.4*0.8`.
  - `struct DrawSource { int level; SlotRef slot; uint32_t w, h; };`
  - `class Residency { public: struct Request { PhotoId id; int level; int64_t priority; std::shared_ptr<CancelToken> cancel; }; Residency(uint32_t photoCount, size_t budgetBytes); void beginFrame(); std::optional<DrawSource> touch(PhotoId, int desired, double distancePx, std::vector<Request>& out); /* returns best resident ≤ desired (else best resident); emits a request if desired not resident/requested; priority = level*1e6 + distancePx */ std::optional<SlotRef> place(PhotoId, int level, uint32_t w, uint32_t h); /* on completed load: allocates (adding a page if budget allows, else evicting the LRU slot not touched this frame); nullopt → drop result */ void requestFailed(PhotoId, int level); void endFrame(); /* cancels requests not touched for 30 frames */ size_t bytesUsed() const; size_t pendingRequests() const; uint32_t pageCount(int level) const; };`
  - L0 is not managed here (its slot = `{id/16384, id%16384}` in the L0 texture); levels 1..4 managed; full level managed by `Streamer` (Task 11).

- [ ] **Step 1: Failing tests:** `levels: thresholds` (`desiredLevel(40,-1)==0`, `(50,-1)==1`, `(1500,-1)==kFullLevel`), `levels: hysteresis` (`(40,1)==1`, `(30,1)==0`), `slots: allocate fills page then needs new page` (L4: 16 slots/page), `slots: uv inset` , `residency: request deduped across frames`, `residency: falls back to best resident level`, `residency: budget exhausted evicts LRU but never touched-this-frame` (budget = 1 page), `residency: stale requests are cancelled` (touch once, 31 empty frames → token cancelled, pendingRequests()==0), `residency: empty pages are released to budget`.
- [ ] **Step 2–4:** FAIL → implement → PASS.
- [ ] **Step 5: Commit** `feat(stream): residency, slot allocation and VRAM budget`

### Task 10: OpenGL renderer + SDL window with fullscreen

Requires `libsdl3-dev`. If missing, stop and ask the user to install it.

**Files:** Create `src/gfx/gl.{h,cpp}`, `src/gfx/shader.{h,cpp}`, `src/gfx/paged_texture.{h,cpp}`, `src/gfx/tile_renderer.{h,cpp}`, `shaders/tile.vert`, `shaders/tile.frag`, `src/app/options.{h,cpp}`, `src/app/app.{h,cpp}`, `src/main.cpp`; Test `tests/test_options.cpp`

**Interfaces:**
- Produces:
  - `bool loadGl(void* (*getProc)(const char*));` exposing only used functions as `pb::gl::GenTextures` etc. (or `#define`-free function pointers in namespace `gl`).
  - `class PagedTexture { public: explicit PagedTexture(uint32_t slotSize); void addPage(); void releasePage(uint16_t page); void upload(SlotRef, const std::vector<Image>& mips); GLuint texture(uint16_t page) const; };` — each page is its own `GL_TEXTURE_2D` (4096², `GL_SRGB8_ALPHA8`, mip chain allocated with `glTexImage2D` per level, `GL_LINEAR_MIPMAP_LINEAR`, `GL_TEXTURE_MAX_LEVEL = log2(slotSize)`), because GL 4.1 can't grow an array texture without a full copy. This refines architecture §6 (pages are separate textures, not one array). Draws are batched per (texture, prevTexture) pair; with ≤ ~10 pages that is a handful of draw calls.
  - `struct TileInstance { float rect[4]; /* screen px x0,y0,x1,y1 */ float uv[4]; float uvPrev[4]; float fade; /* 0..1 weight of current over prev */ float dim; };`
  - `class TileRenderer { public: bool init(); void beginFrame(Vec2d viewportPx, float clear[3]); void draw(GLuint tex, GLuint texPrev, std::span<const TileInstance>); /* one instanced draw */ };` — vertex shader maps px → NDC; fragment shader samples `uTex` at `uv` and `uTexPrev` at `uvPrev` with `textureGrad` (derivatives computed before any branch), mixes by `fade`, multiplies by `1-dim`. Batch key = (tex, texPrev).
  - `struct Options { std::filesystem::path photoDir = "photos"; std::optional<std::filesystem::path> cacheDir; bool rebuildCache = false; std::string layout = "square-crop"; bool bench = false; }; std::variant<Options, std::string> parseOptions(int argc, char** argv);` (error string for unknown flags/layouts; `--help` text)
  - `class App { public: int run(const Options&); };` — SDL3 init video, GL 4.1 core context, `SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|SDL_WINDOW_OPENGL`, vsync `SDL_GL_SetSwapInterval(-1)` falling back to 1; `F11`/`F` → `SDL_SetWindowFullscreenMode(w, nullptr)` + `SDL_SetWindowFullscreen(w, !isFullscreen)`; `Esc` → `interaction.onKey(Escape)`; unhandled → leave fullscreen; `Q`/close → quit; drawable size from `SDL_GetWindowSizeInPixels`, mouse coordinates multiplied by `SDL_GetWindowPixelDensity`. Monotonic time `SDL_GetTicksNS`.
- In this task tiles are drawn with a 1×1 dark-grey placeholder texture (no photo pixels yet), with full interaction from Task 3, against a layout of N = number of scanned files (scanning on the main thread is acceptable *only in this task*; Task 11 moves it).

- [ ] **Step 1: Failing tests:** `options: defaults`, `options: folder and flags`, `options: unknown flag errors`, `options: unknown layout errors`.
- [ ] **Step 2–4:** FAIL → implement options → PASS. Implement gl loader, shaders (embedded via `pb_embed_shaders`), PagedTexture, TileRenderer, App.
- [ ] **Step 5: Manual verification:** `build/linux-debug/photobrowse photos` → window shows a 5×4 grid of grey tiles centered on the center tile; drag pans, fling glides, click centers smoothly, wheel zooms about cursor, double-click zooms the tile to window-fitted size, `F11` toggles borderless fullscreen and `Esc` leaves it, resize keeps the center. Check no GL errors (`glGetError` in debug after each frame, logged).
- [ ] **Step 6: Commit** `feat(app): SDL3 window, GL 4.1 tile renderer, fullscreen`

### Task 11: Indexing pipeline and streaming integration

**Files:** Create `src/app/streamer.{h,cpp}`; Modify `src/app/app.{h,cpp}`

**Interfaces:**
- Consumes: Tasks 5–10.
- Produces: `class Streamer { public: Streamer(const Catalog&, const std::filesystem::path& photoRoot, ThumbCache&, JobSystem&, size_t budgetBytes); void uploadL0(const std::vector<std::pair<uint64_t, Image>>&); /* main thread at startup */ void frame(const Camera&, const Layout&, const Interaction&, std::vector<TileDraw>& out); /* touch visible + 1-cell ring, collect requests, submit jobs, drain results under 4 MB budget, upload, produce draw list with cross-fade state */ size_t bytesUsed() const; size_t pendingJobs() const; };` `struct TileDraw { uint32_t pos; DrawSource cur; std::optional<DrawSource> prev; float fade; };` (full level draws use their own `GL_TEXTURE_2D`, ≤ 3, LRU, decoded with `decodeFull(minLongEdge = window long edge)` when `desiredLevel == kFullLevel` or the photo is focused.)
- Startup sequence in `App::run` (architecture §9): resolve cache dir → `loadCatalog` + `readL0Pack` (warm) or first-run: window opens, `scanFolder` + `quickIndex` on the JobSystem with progress in the window title (`PhotoBrowse — indexing 1234/10000`), then `sortOrder`, `saveCatalog`, `writeL0Pack`, L1 files written by workers; fallbacks (`kNeedsDecodeL1`) queued at background priority. Warm start launches a background re-scan; a non-empty diff runs quick pass for added/changed and writes the updated catalog + pack for the next launch. `--rebuild-cache` deletes the library cache dir first. On startup call `ThumbCache::sweep(4 GB)` on a worker. Missing photo dir → stderr message, exit code 2. Empty dir → title `PhotoBrowse — no photos in <dir>`.
- Lazy job: read `ThumbCache::read(level)`; if missing → `buildLazyLevels(highest = max requested level ≤ 4)`, write all produced levels, push the requested one; produce mips on the worker (`buildMips`). Jobs check `cancel->cancelled` before reading and before decoding.

- [ ] **Step 1: Manual verification, cold:** `rm -rf ~/.cache/photobrowse && build/linux-debug/photobrowse photos` → grid appears in date order within seconds, previews sharp at 128 px; zooming in sharpens with a cross-fade (no pops, no empty tiles); `~/.cache/photobrowse/<hash>/` contains `catalog.bin`, `l0.pack`, `l1/`, and `l2…l4/` only for photos zoomed into; the photos folder is unchanged (`git status`/`ls -la` mtimes).
- [ ] **Step 2: Manual verification, warm:** relaunch → full grid on the first frame. Copy `photos/` to a scratch dir, run on it, add one more JPEG while it runs → it does not appear now, and appears on the following launch.
- [ ] **Step 3:** Run `ctest` → all PASS.
- [ ] **Step 4: Commit** `feat(app): indexing pipeline and progressive streaming`

### Task 12: Whole-photo view rendering, stats, bench, scale test, docs

**Files:** Modify `src/app/app.cpp`, `src/app/streamer.cpp`; Create `tools/make_test_library.py`; Modify `README.md`, `CLAUDE.md`

**Interfaces:**
- Consumes: `Interaction::focused()/focusProgress()`, `Streamer`.
- Produces:
  - Whole-photo draw: focused tile drawn last; rect = lerp(cell photo rect on screen → `fitRect(window inset 2%, aspect)`), UV = lerp(`cropUv` → full); all other tiles `dim = 0.85 * progress`; full-res texture requested for the focused photo and its ±1 neighbours.
  - `F3` → window title shows `fps · p99 ms · jobs · VRAM MB/640` updated every 0.5 s.
  - `--bench`: scripted 10 s path (zoom out to whole grid, fling across, zoom into a corner, focus/unfocus) driven through `Interaction` with synthetic events; prints `frames=… p50=…ms p95=…ms p99=…ms max=…ms` to stdout and exits 0.
  - `tools/make_test_library.py <src> <dst> --count 10000`: creates `dst/0000/…` subfolders of symlinks (`os.symlink`, hardlinks on Windows) named `IMG_%05d.jpg` round-robin over `src` photos.

- [ ] **Step 1:** Implement whole-photo rendering; manual check: double-click a portrait photo → animates from square crop to full uncropped portrait over 350 ms, background dims, ←/→ steps through photos in time order, `Esc`/drag/wheel-out returns to the square.
- [ ] **Step 2:** Implement F3 stats and `--bench`.
- [ ] **Step 3: Scale verification:** `python3 tools/make_test_library.py photos /tmp-scratch/lib10k --count 10000`; cold run indexes in the background while staying interactive; `photobrowse --bench /…/lib10k` on the default (Intel) GPU in fullscreen 4K → record p99; target p99 < 16.7 ms. Record the numbers in `docs/perf.md` (date, GPU, resolution, p50/p95/p99, VRAM, cold index time). If the target is missed, profile and fix before continuing (likely suspects: instance buffer upload, per-batch state changes, upload budget).
- [ ] **Step 4:** Update `README.md` (final controls, options, cache, bench, test library) and `CLAUDE.md` (module map, where things live, how to verify perf). `ctest` → PASS.
- [ ] **Step 5: Commit** `feat: whole-photo view, stats, bench and scale tooling`
