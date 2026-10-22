# CLAUDE.md

PhotoBrowse: a fast, cross-platform (Linux first, Windows-ready) C++20 / SDL3 /
OpenGL 4.1 photo browser showing a whole library as one zoomable grid.

## Read first
- `docs/spec.md` — product spec: behavior, controls, performance targets, decisions log.
- `docs/architecture.md` — stack, module map, image pyramid/caching, rendering,
  threading, and how future features (prefetch, search + fly-to, DB, layouts,
  shader effects) slot in.
- `docs/plans/` — implementation plans.

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
- `pb_core` (src/core, view, layout, library parsing, cache, stream) stays free of
  SDL and OpenGL so it is unit-testable; GL lives in `src/gfx`, SDL in `src/app`.
- POSIX/Win32-specific code only in `src/platform/`.
- OpenGL 4.1 core / GLSL 410 only (no glTexStorage, compute, bindless).
- Write the failing test first for all pure logic.
