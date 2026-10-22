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
| `--config <file>` | settings file to use instead of the default location |
| `--cache-dir <dir>` | thumbnail cache root (default: `$XDG_CACHE_HOME` or `~/.cache`; `%LOCALAPPDATA%` on Windows) |
| `--rebuild-cache` | discard this library's cache and re-index |
| `--layout square-crop` | layout mode (only one so far) |
| `--stats` | start with the F3 stats display on |
| `--bench` | fullscreen scripted pan/zoom tour; prints frame-time percentiles and exits |
| `--snapshot <file.jpg>` | save the settled start view (or the end of `--bench`) and exit |

The first run on a folder indexes it (progress in the window title): dates and the
small preview most cameras embed are read from the start of each file, so 10,000
photos take seconds. Later runs open instantly from the cache in
`<cache>/photobrowse/<library-hash>/`. The photo folder is never modified. Photos
added or removed while the app runs show up on the next launch.

### Settings file

On first run PhotoBrowse writes a documented settings file to
`~/.config/photobrowse/config.ini` (`%APPDATA%\photobrowse\config.ini` on Windows);
`--config <file>` uses another one. Uncomment a line to change it:

```ini
[library]
folder = ~/Pictures        # photo folder (the command-line folder wins)

[view]
gap = 0.03                 # space around photos, 0 = none, max 0.2
max_photo_zoom = 4         # focused-photo zoom limit, relative to fit (1 to 16)

[stats]
enabled = false            # stats display at startup (F3 toggles)
graph = false              # frame-time graph in the fullscreen panel

[display]
gpu = auto                 # auto | integrated | discrete (laptops with two GPUs, Linux)

[background]
mode = shader              # solid | gradient | vignette | shader
shader = aurora            # a built-in preset name, or a path to your own .frag
color = #6fa8dc            # main colour (presets use both colours)
color2 = #0b1020
speed = 1                  # animation speed: 0.5 = half, 2 = double, 0 = frozen
density = 1                # stars/lights in warp, starfield, bokeh (0.1 to 4)
```

`gpu = discrete` runs on the dedicated GPU (NVIDIA PRIME offload, or `DRI_PRIME=1`
for other dual-GPU setups); the startup line `OpenGL: ...` shows which one is used.
With `auto`, launchers such as `switcherooctl launch -g 1 photobrowse` also work. On
Windows choose the GPU in Settings > System > Display > Graphics.

Built-in background presets (in `shaders/backgrounds/`, compiled into the program):

| Still | Animated |
|---|---|
| `gradient`, `vignette`, `grain`, `dots`, `spotlight` | `waves`, `aurora`, `nebula`, `bokeh`, `starfield`, `warp` |

Animated backgrounds redraw continuously (a little more power); still ones don't.
Your own shader is a complete `#version 410 core` fragment shader writing
`out vec4 fragColor` and may use `uResolution`, `uTime` (already scaled by `speed`),
`uColor`, `uColor2` and `uDensity` — copy a
preset as a starting point. The active background is printed at startup, and
mistakes in the settings file are reported with the line number.

### Controls

| Input | Action |
|---|---|
| Drag | pan (release while moving to glide); pan the photo when zoomed in whole-photo view |
| Click a photo | glide it to the center |
| Double-click / `Enter` / scroll in past max zoom | whole-photo view (uncropped) |
| Mouse wheel | zoom about the cursor; in whole-photo view, zoom the focused photo up to `max_photo_zoom` |
| Right-click | reset a zoomed focused photo to fit the window |
| Arrow keys | move one photo (in whole-photo view: ←/→ previous/next) |
| `+` / `-` | zoom about the center |
| `Home` | back to the start view |
| `F11` or `F` | toggle fullscreen |
| `Esc` | leave whole-photo view, then fullscreen |
| `F2` | photo info (name, size, date taken, file size) while a photo is open in whole-photo view |
| `F3` | stats: an on-screen panel in fullscreen (fps, frame times, p99, loads, GPU memory, and a 4-second frame-time graph — green on time, yellow late, red dropped); the title bar in a window |
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
