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

## Test

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --test-dir build/linux-debug --output-on-failure
```
Some tests read sample JPEGs from `photos/` (not committed).
