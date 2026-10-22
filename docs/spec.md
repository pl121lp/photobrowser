# PhotoBrowse — Product Spec

Status: draft for approval · 2026-10-21

## 1. Intent

PhotoBrowse is a desktop app for **reliving trips and browsing memories** across a
large personal photo collection (10,000+ photos), and occasionally **hunting for one
specific shot**. It is a memory lane, not a photo manager: no editing, no culling,
no file management.

The defining quality is **speed and fluidity**. Every interaction — panning,
flicking, zooming, clicking — must feel instant and never stutter. Performance is
the primary requirement; features come second.

## 2. Goals, non-goals, anti-goals

**Goals**
- All photos live on one continuous, zoomable surface laid out as a square grid.
- Butter-smooth pan / zoom / click-to-center at the display's refresh rate, with
  10,000+ photos.
- Never show an empty tile: show *something* immediately, sharpen progressively.
- Cross-platform: developed and evaluated on Linux first, architected and buildable
  for Windows (macOS not required, but nothing should rule it out).
- Architecture ready for: predictive prefetch, search + "fly to" results, a photo
  database, alternative layouts, and GPU shader special effects.

**Non-goals (for now)**
- Editing, rating, tagging, deleting, moving, or otherwise changing photos.
- Video, RAW, HEIC, PNG (JPEG only in v1; the decoder is behind an interface).
- Network/cloud libraries, multi-user, sharing.

**Anti-goals** (would count as failure even if "working")
- Stutter, hitching, or dropped frames while interacting, including while photos
  load in the background.
- Grey/empty grid holes while moving around.
- The UI waiting on disk: no interaction may block on I/O or decoding.
- A slow startup with a warm cache (re-opening a 10k library must be near-instant).
- Modifying anything in the user's photo folder (it is strictly read-only).

## 3. The experience

### 3.1 The surface
- All photos are placed on a single 2D canvas in a **square grid**:
  `cols = ceil(sqrt(N))`, `rows = ceil(N / cols)`. (20 photos → 5×4; 10,000 → 100×100.)
- **Order: by time taken**, oldest first, filled **left to right, row by row**.
  Time taken = EXIF `DateTimeOriginal`; if missing, the file's modification time.
  Ties broken by file name.
- **Default layout mode: square crop** — each photo is center-cropped to fill its
  square cell. There is a small gap between cells and a dark background.
- Layout is a **selectable mode** (see §5). Future modes include square cells with
  whole photos letterboxed in black frames, full-rectangle photos, and creative
  layouts. Switching modes must not require rebuilding the thumbnail cache.
- On launch, the camera is centered on the **center photo** — the occupied cell
  nearest the geometric center of the grid — zoomed so that photo fills about a third
  of the window's shorter side and its neighbours are visible around it.

### 3.2 Interaction (v1)
| Input | Behavior |
|---|---|
| Left-drag | Pan the surface; in a zoomed whole-photo view, pan the focused photo. Content tracks the cursor 1:1. |
| Release while moving | **Momentum**: the surface keeps gliding and decelerates smoothly. |
| Left-click (no drag) on a photo | That photo **glides to the center** with ease-in-out motion (~400 ms). |
| Double-click on a photo | Open it in the **whole-photo view** (§3.3). |
| Mouse wheel | **Smooth zoom about the cursor** (point under cursor stays put), including within whole-photo view. |
| Right-click | In whole-photo view, reset the focused photo to fit the window. |
| Arrow keys | Move one cell (eased). In whole-photo view: ←/→ previous/next photo in time order. |
| `+` / `-` | Zoom in/out around screen center. |
| `Home` | Return to the start view (center photo). |
| `F11` or `F` | Toggle **fullscreen** (borderless desktop fullscreen on the current monitor). The app starts fullscreen unless `[display] fullscreen = false` or `--windowed`. |
| `Esc` | Leave whole-photo view; otherwise leave fullscreen; otherwise nothing. |
| `Q` / window close | Quit. |
| `F2` | Toggle photo info: while a photo is fully open in whole-photo view, a bottom-left panel shows its file name, width × height (megapixels), date taken (EXIF; file modification date if none) and file size. Off by default. |
| `F3` | Toggle performance stats: on-screen overlay in fullscreen (fps, frame time, p99, loads, GPU memory, frame-time graph); window title otherwise. |
| `F5` | Toggle **screensaver mode**: fly to a random photo and open it in the whole-photo view, hold it for `[screensaver] hold` seconds (30 by default), then travel to another random photo. Each journey is eased and pulls back (zooms out) mid-way, then zooms in on arrival; longer distances take longer (2–6 s). Repeats until turned off; any click, scroll or key (other than F2/F3/F5/F11/F) also stops it. The cursor is hidden while it runs. Off by default; `[screensaver] enabled = on` starts the app in it. |
| `F6` | Cycle the **background**: solid → each built-in preset (gradient, vignette, grain, …, warp) → the `[background] shader` file if one is set and readable → solid. Starts from the configured background; not saved. |

- A click is a press+release with < ~4 px of movement; anything more is a drag.
- Grabbing during momentum or an eased animation stops it immediately.
- Zoom range: from **whole grid fits the window** (with a small margin) to **one
  cell fills the window's shorter side**. Zooming in further enters the whole-photo view.

### 3.3 Whole-photo view
Square crops hide part of every photo (a quarter of a 3:4 portrait), so there is a
way to see one photo whole:
- **Enter**: double-click a photo, or wheel-zoom in past the max grid zoom while over
  a photo, or `Enter` on the centered photo.
- **Look**: the photo smoothly animates from its square crop to the **full, uncropped
  photo fitted to the window**, shown at screen resolution from the original. The rest
  of the grid dims behind it.
- **Inspect**: wheel in to enlarge the focused photo about the cursor, up to
  `view.max_photo_zoom` (4× fit-to-window by default), and left-drag to pan it. Wheel
  out returns to the fitted photo before a further wheel-out leaves the view;
  right-click resets directly to fit-to-window.
- **Navigate**: ←/→ (and wheel-free left/right flick) step to the previous/next photo
  in time order; the grid behind follows so that photo is centered underneath.
- **Leave**: `Esc`, click outside the photo, wheel-zoom out from the fitted view, or
  start dragging while fitted — the photo animates back into its square cell.

### 3.4 Image quality behavior ("blurry first, then sharp")
- Every visible photo is drawn immediately from the lowest-resolution level that is
  available (always resident after first indexing).
- As higher-resolution versions arrive they **cross-fade in** (~150 ms) — no pop.
- When a photo is large on screen (or in whole-photo view) it is shown at screen
  resolution decoded from the original.
- If GPU memory runs short (e.g. 4K mid-zoom), tiles use a slightly softer level
  rather than stutter.

### 3.5 First run vs. later runs
- **First run** on a folder: the window opens immediately, the folder is scanned
  (recursively, `.jpg`/`.jpeg`). A **quick pass** reads just the start of each file
  (date + the small preview most cameras embed) so the whole grid fills in within
  seconds; sharper thumbnails are then generated in the background — **visible
  photos first** — with progress shown, and cached on disk. Browsing works throughout;
  a minute or two of background work on a 10k library is acceptable.
- **Later runs**: catalog and the smallest thumbnail level load from cache; the
  first full frame appears in well under a second. The folder is re-checked in the
  background; added/removed/changed photos are picked up **on the next launch** (the
  grid never reshuffles under the user mid-session).
- The cache lives in the per-user cache directory, never in the photo folder.

### 3.6 Running it
`photobrowse [photo-folder]` — defaults to `./photos`.
Options: `--cache-dir <dir>`, `--rebuild-cache`, `--layout square-crop`.

## 4. Performance targets (v1)
Reference machine (the dev laptop, worst case): **3840×2160 @ 60 Hz on the integrated
Intel UHD 630**; library on internal NVMe SSD. If it's smooth there it's smooth on the
discrete RTX GPU too.
- Rendering holds the display refresh rate (≥ 60 fps) with a 10,000-photo synthetic
  library while panning, zooming, and loading; 99th-percentile frame time below one
  refresh interval.
- Warm start (cache present): first complete frame < 1 s for 10k photos.
- No main-thread disk I/O or JPEG decoding, ever.
- GPU memory for photo textures stays within a configurable budget (default 640 MB,
  all levels and mipmaps included); exceeding demand degrades sharpness, never smoothness.
- First run on a 10k library: whole grid shows previews within ~10 s; full
  background indexing within a few minutes.

**Scale:** v1 targets ~10k photos. 50k–100k is expected eventually; v1 must not paint
itself into a corner (stable photo IDs, versioned cache formats), but optimizing for
that size is deferred (see architecture §10).

## 5. Future capabilities (architecture must support; not in v1)
1. **Predictive prefetch** — load ahead of where the user is heading (momentum
   landing point, eased-animation target, pan direction, fly-to destination, idle
   warm-up of neighbors).
2. **Search + fly-to** — search by date/date range, file name, and later other keys
   (tags, places, people). Selecting a result **flies** the camera to the photo or
   group: zoom out, travel, zoom in, along a smooth optimal path. Motion blur and
   the always-resident low-res level make the trip look seamless ("fake it").
3. **Photo database** — fast indexed metadata store backing search.
4. **Alternative layouts** — letterboxed squares, full rectangles, justified rows,
   clusters by trip/day, etc.
5. **Shader special effects** — post-processing and per-photo effects (motion blur,
   vignette, film grain, color grading, transitions).

## 6. Decisions log
Decided by the user:
- Use case: reliving trips/memories; sometimes hunting for one shot.
- Order by time taken, row by row; other orders possibly later.
- Momentum panning and blurry-first progressive loading.
- Stack: C++ / OpenGL / SDL3 / libjpeg-turbo / CMake (as proposed).
- Square crop now; layout configurable later (letterbox frames, full rectangles, creative).
- Fullscreen mode. Future shader special effects.
- Whole-photo view in v1.
- Library: ~10k on internal SSD now; 50k–100k later (optimize later). First-run indexing
  time of a minute or two is fine.

Calls made by Claude (open to change):
- Key bindings; double-click / zoom-past-max / `Enter` open whole-photo view.
- `Esc` leaves whole-photo view, then fullscreen; `Q` quits.
- Max grid zoom = one cell fills the shorter window side.
- Perf reference = 4K on the integrated Intel GPU; GPU budget 640 MB with soft fallback.
- Folder changes apply on next launch.
- Date fallback = file mtime; tie-break = file name; folder scan is recursive.
- JPEG only in v1.
- Cross-fade duration ~150 ms; click-to-center ~400 ms ease-in-out.
- Screensaver: `F5`, off by default (`[screensaver] enabled`), hold time measured from
  when the photo is fully open; user input stops it rather than fighting it.
