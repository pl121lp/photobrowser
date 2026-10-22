# PhotoBrowse

A very fast, GPU-accelerated photo browser. All photos are laid out on one zoomable
surface as a square grid, ordered by the time they were taken. Drag to pan (with
momentum), click to center a photo, scroll to zoom, double-click to see a photo whole,
and go fullscreen for the full effect.

Design documents: [docs/spec.md](docs/spec.md), [docs/architecture.md](docs/architecture.md).

## Dependencies

| | Linux (Ubuntu 25.10) | Windows |
|---|---|---|
| Toolchain | `cmake` ≥ 3.24, `ninja-build`, `g++` ≥ 13 or `clang++` ≥ 17 | Visual Studio 2022 (C++ workload), CMake, Ninja |
| SDL3 | `sudo apt install libsdl3-dev` | via vcpkg (`vcpkg.json`) |
| libjpeg-turbo | `sudo apt install libturbojpeg0-dev` | via vcpkg (`vcpkg.json`) |

Everything else (doctest, Khronos GL headers) is vendored in `third_party/`.
Without SDL3/libjpeg-turbo the core library and its tests still build.

## Build

Linux:
```sh
cmake --preset linux-release
cmake --build --preset linux-release
```
(`linux-debug` and `linux-asan` presets also exist; output goes to `build/<preset>/`.)

Windows (from a "x64 Native Tools" prompt, with `VCPKG_ROOT` set):
```bat
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

## Run

```sh
build/linux-release/photobrowse [photo-folder]     # defaults to ./photos
```

| Option | |
|---|---|
| `--cache-dir <dir>` | thumbnail cache root (default: `$XDG_CACHE_HOME` or `~/.cache`; `%LOCALAPPDATA%` on Windows) |
| `--rebuild-cache` | discard this library's cache and re-index |
| `--layout square-crop` | layout mode (only one so far) |
| `--bench` | fullscreen scripted pan/zoom tour; prints frame-time percentiles and exits |
| `--snapshot <file.jpg>` | save the settled start view (or the end of `--bench`) and exit |

The first run on a folder indexes it (progress in the window title): dates and the
small preview most cameras embed are read from the start of each file, so 10,000
photos take seconds. Later runs open instantly from the cache in
`<cache>/photobrowse/<library-hash>/`. The photo folder is never modified. Photos
added or removed while the app runs show up on the next launch.

### Controls

| Input | Action |
|---|---|
| Drag | pan (release while moving to glide) |
| Click a photo | glide it to the center |
| Double-click / `Enter` / scroll in past max zoom | whole-photo view (uncropped) |
| Mouse wheel | zoom about the cursor |
| Arrow keys | move one photo (in whole-photo view: ←/→ previous/next) |
| `+` / `-` | zoom about the center |
| `Home` | back to the start view |
| `F11` or `F` | toggle fullscreen |
| `Esc` | leave whole-photo view, then fullscreen |
| `F3` | frame-time / memory stats in the title bar |
| `Q` | quit |

## Performance testing

```sh
python3 tools/make_test_library.py photos /tmp/lib10k --count 10000
build/linux-release/photobrowse /tmp/lib10k --bench
```
Results and history: [docs/perf.md](docs/perf.md).

## Test

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --test-dir build/linux-debug --output-on-failure
```
Some tests read sample JPEGs from `photos/` (not committed).
