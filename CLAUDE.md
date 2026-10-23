# CLAUDE.md

PhotoBrowse: a fast, cross-platform (Linux first, Windows-ready) C++20 / SDL3 /
OpenGL 4.1 photo browser showing a whole library as one zoomable grid.

## Read first
- `docs/spec.md` — product spec: behavior, controls, performance targets, decisions log.
- `docs/architecture.md` — stack, module map, image pyramid/caching, rendering,
  threading, and how future features (prefetch, search + fly-to, DB, layouts,
  shader effects) slot in.
- `docs/plans/` — implementation plans.
- `docs/perf.md` — benchmark method, results, and the causes of past hitches.
- `docs/map-spec.md` — map view (`M`): decisions and architecture, not yet implemented.

## Module map
- `src/app/config` user settings (config.ini) — new settings go there with a
  documented entry in `defaultConfigText()` and a test.
- `src/core` geometry, levels, frame stats · `src/view` camera, motion, interaction,
  bench script · `src/layout` layouts and crop/fit math · `src/library` EXIF, catalog,
  scanner, indexer (decode helpers) · `src/cache` l0.pack + per-level JPEG cache ·
  `src/stream` job system, residency (VRAM slots/budget) · `src/imaging` TurboJPEG,
  resize · `src/app` options, library indexing, streamer (GL-free, via `TextureSink`),
  GL texture sink, SDL app loop · `src/gfx` GL loader, shaders, paged textures,
  upload ring, tile renderer · `shaders/` GLSL (embedded at build time); `shaders/backgrounds/*.frag` are the
  background presets, picked up automatically by name (add one + its name in
  `src/app/background_presets.cpp`); `shaders/photo/*.frag` are the photo effects (keys
  1-9), spliced into `tile.frag` and numbered in `src/gfx/tile_renderer.cpp`. Keep animated ones cheap: benchmark at 4K.
- `src/plugin` the plugin C ABI (`photobrowse_plugin.h`) and `PluginHost` ·
  `plugins/<name>/` optional plugins (shared libraries, built into `<build>/plugins/`;
  `video` uses FFmpeg) · `tests/plugins/` fake plugin for host tests.
- Libraries: `pb_core` (no deps) → `pb_imaging` (+turbojpeg) → `pb_app` (no SDL/GL)
  → `pb_gfx` + `photobrowse` exe (SDL3, GL).

## Build & test
```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --test-dir build/linux-debug --output-on-failure
build/linux-debug/pb_tests --test-case="<name>"      # single test
```

## Rules
- Never install system packages; tell the user what is needed (see README).
- The photo folder is strictly read-only; all generated data goes to the cache dir.
- No file I/O or JPEG decoding on the main/render thread.
- Keep the core nimble: a larger feature, especially one needing a new dependency, is a
  plugin (architecture §13), loaded only via `--plugin`. The core must never name or
  depend on a specific plugin; extend the C ABI instead (append fields, bump the version).
- `pb_core` (src/core, view, layout, library parsing, cache, stream) stays free of
  SDL and OpenGL so it is unit-testable; GL lives in `src/gfx`, SDL in `src/app`.
- POSIX/Win32-specific code only in `src/platform/`.
- OpenGL 4.1 core / GLSL 410 only (no glTexStorage, compute, bindless).
- Write the failing test first for all pure logic.
- Performance changes: verify with `--bench` (release build, fullscreen) before and
  after, and record results in `docs/perf.md`. Never block the render thread on GPU
  allocations, big uploads, or locks held by workers.
- `--snapshot <file.jpg>` lets you look at what the app renders without screenshots.
