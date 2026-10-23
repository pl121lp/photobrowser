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
| FFmpeg (optional, video plugin) | `sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev` | vcpkg `ffmpeg` (with `avformat`, `avcodec`, `swscale`, `swresample`) |
| libcurl (optional, maptiles plugin) | `sudo apt install libcurl4-openssl-dev` | vcpkg `curl` |
| libheif (optional, heif plugin) | `sudo apt install libheif-dev libheif-plugin-libde265` | vcpkg `libheif` (macOS: Homebrew `libheif`) |

Everything else (doctest, Khronos GL headers, stb_image v2.30 — public domain / MIT) is
vendored in `third_party/`. Supported files: JPEG and PNG built in; video, HEIC etc. via plugins.
Without SDL3/libjpeg-turbo the core library and its tests still build. Plugins build
only when their dependencies are found (`-DPB_BUILD_PLUGINS=OFF` skips them all); the
program itself never needs them.

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
| `--cache-dir <dir>` | thumbnail cache root (default: `[performance] cache_dir`, else `$XDG_CACHE_HOME` or `~/.cache`; `%LOCALAPPDATA%` on Windows) |
| `--rebuild-cache` | discard this library's cache and re-index |
| `--layout square-crop` | layout mode (only one so far) |
| `--windowed` | start in a window instead of fullscreen (overrides `[display] fullscreen`) |
| `--stats` | start with the F3 stats display on |
| `--bench` | fullscreen scripted pan/zoom tour; prints frame-time percentiles and exits |
| `--plugin <name\|path>` | load a plugin, e.g. `--plugin video` (repeatable; see [Plugins](#plugins)) |
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
recursive = off            # on = include photos in subfolders (off = only the folder itself)
sort = oldest_first        # oldest_first | newest_first

[view]
gap = 0.03                 # space around photos, 0 = none, max 0.2
start_at = center          # center | oldest | newest | random (Home returns there)
start_focus = focus        # focus (photo + neighbours) | photo (opened whole; arrows step on) | full (all photos)
autoplay = off             # on = videos play by themselves once opened (off = click to play)
fit = fit                  # fit = fill each square (edges cropped) | letterbox = whole photo, black bars
letterbox_bars = black     # letterbox bars: black | transparent (background shows through)
corner_radius = 0          # rounded corners: fraction of the shorter side, 0.05 = subtle, max 0.5
max_photo_zoom = 4         # focused-photo zoom limit, relative to fit (1 to 16)

[stats]
enabled = false            # stats display at startup (F3 toggles)
graph = false              # frame-time graph in the fullscreen panel

[display]
fullscreen = true          # start fullscreen (--windowed starts in a window)
gpu = auto                 # auto | integrated | discrete (laptops with two GPUs, Linux)

[performance]
gpu_memory = 640           # MB of GPU memory for photo images (256 to 16384)
disk_cache = 4             # GB limit of the thumbnail cache on disk (trimmed at startup)
cache_dir =                # cache location; empty = per-user default (--cache-dir wins)
decode_threads = auto      # threads decoding while browsing: auto (2-4) or 1 to 32
vsync = on                 # off = draw as fast as possible (for measuring only)

[screensaver]
enabled = off              # on = start in screensaver mode (F5 toggles)
hold = 30                  # seconds on each photo
slow_zoom = 1              # slow zoom into each photo while shown: 1 = off, 1.15 = gentle, max 2
photo_only = off           # on = hide the other photos while one is shown

[background]
mode = shader              # solid | gradient | vignette | shader
shader = aurora            # a built-in preset name, or a path to your own .frag
color = #6fa8dc            # main colour (presets use both colours)
color2 = #0b1020
speed = 1                  # animation speed: 0.5 = half, 2 = double, 0 = frozen
density = 1                # stars/lights in warp, starfield, bokeh (0.1 to 4)

[background.aurora]        # optional: colours for one preset when F6 shows it
color = #3fd6a0            # (each preset has its own built-in colours otherwise)
color2 = #0b1020
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
| Click the open photo | its plugin action, if any (video: play / pause) |
| Arrow keys | move one photo (in whole-photo view: ←/→ previous/next, ↑/↓ closest photo one row up/down) |
| `Shift` + arrow keys | the same, five photos (or rows) at a time; stops at the edge |
| `+` / `-` | zoom about the center |
| `Home` | back to the start view |
| `F11` or `F` | toggle fullscreen |
| `Esc` | close the help, leave whole-photo view, then fullscreen |
| `F1` | help: show / hide a list of all keys and mouse actions |
| `F2` | photo info (name, size, date taken, file size) while a photo is open in whole-photo view |
| `F3` | stats: an on-screen panel in fullscreen (fps, frame times, p99, loads, GPU memory, and a 4-second frame-time graph — green on time, yellow late, red dropped); the title bar in a window |
| `F5` | screensaver: tours random photos, flying between them; videos (with the plugin) play by themselves and the tour waits for them to end (any click, scroll or key stops it) |
| `F6` | next background: solid, each built-in preset, your shader file, then solid again |
| `M` | map: where the open photo was taken, or the visible photos (needs the `maptiles` plugin, see below) |
| `Q` | quit |

## Plugins

The core stays small; optional features with their own dependencies are plugins
(shared libraries in `build/<preset>/plugins/`), loaded only when named on the command
line. Design and API: [docs/architecture.md §13](docs/architecture.md#13-plugins).

| Plugin | Needs | What it does |
|---|---|---|
| `heif` | libheif with an HEVC decoder (see Dependencies) | `.heic .heif` photos (e.g. from iPhones) join the grid like JPEGs, upright, with their capture date and GPS position (shown on the map). Indexing decodes the small embedded thumbnail when there is one. |
| `maptiles` | libcurl (see Dependencies) | Map tiles for the map view (`M`): OpenStreetMap by default, cached on disk. See [Map](#map). |
| `video` | FFmpeg (see Dependencies) | `.mp4 .mov .m4v .mkv .webm .avi .3gp` join the grid as their first frame with a play badge. Open one (double-click) and click it to play with sound, click again to pause; at the end a click plays it again (or it loops, see below). A bar at the bottom shows how far it has played. HDR phone videos are tone-mapped. No seeking yet. |

```sh
build/linux-release/photobrowse ~/Pictures --plugin video
```

To have videos start over by themselves at the end (the screensaver still moves on
when a video ends), add to the config file:

```ini
[plugin.video]
loop = on   # default off
```

Enabling or disabling a plugin that adds file types re-indexes the library once.
Video dates come from the file's UTC creation time shown in this computer's time zone,
so clips filmed in another time zone sort a few hours off from the photos around them.

## Map

`M` slides in a street map on the right showing where the open photo was taken (the
map flies from place to place as you step through photos), or a dot for every visible
photo in the grid (the map fits itself to them once the grid rests). Drag, wheel and
double-click move the map; `M` or `Esc` closes it. It needs a tile plugin:

```sh
build/linux-release/photobrowse ~/Pictures --plugin maptiles
```

Settings go in the `[map]` and `[plugin.maptiles]` sections of the config file:

```ini
[map]
width = 0.4        # fraction of the window
photo_zoom = 15    # zoom for one photo (2-19)

[plugin.maptiles]
# url = https://tile.openstreetmap.org/{z}/{x}/{y}.png   ({key} is replaced by api_key)
# api_key =
# attribution = © OpenStreetMap contributors
# max_zoom = 19
# disk_cache = 500   (MB, shared by all libraries, in the cache folder's maptiles/)
```

- **OpenStreetMap's tile servers are run by volunteers.** The plugin follows their
  [tile usage policy](https://operations.osmfoundation.org/policies/tiles/) (identifying
  User-Agent, disk cache, at most two connections, no prefetching, the attribution on
  the map), but for heavy use set `url` to a provider with your own key.
- **Privacy:** tile requests tell the tile server roughly where the photos you are
  looking at were taken. Tiles are requested only while the map is open.
- Without a connection, tiles already in the cache still show; if nothing can be shown
  the map closes again with a message.

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
