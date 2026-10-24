# PhotoBrowse — Map View Spec

Status: **implemented** (2026-10-22, plan `docs/plans/2026-10-22-map-view.md`).
Self-contained: everything decided about the map feature is here, together with
`docs/spec.md`, `docs/architecture.md` (§13 Plugins) and `CLAUDE.md`.

Deviations and choices made while implementing:
- Dot colour fixed `#e8452c` (§9 open question); dots are 13 px (single) and 12 px
  (grid) at display scale 1, including their white ring.
- The panel background, the 1-px separator and the tile drawing use scissored clears
  and the existing `TileRenderer`; `MapRenderer` draws only the dots and halo.
- `[map] open = on` (start with the map shown) is a documented setting, not hidden.
- At most 4 map tile uploads per frame (≈1.4 MB with mips).
- Backstop tiles while loading: level 2 and three levels above the visible one.
- Windows: libcurl comes from vcpkg `curl` like FFmpeg for the video plugin; it is not
  in `vcpkg.json` (the plugin is optional).

## 1. Intent

Show **where a photo was taken** without leaving the browser. Pressing `M` slides the
current view to the left and a zoomable street map in from the right, with a dot for
the open photo, or dots for every photo visible in the grid. Stepping through photos
makes the map **fly** from one location to the next, so a trip's geography unfolds as
you browse it.

The photo data is already there: EXIF GPS is parsed at indexing time and stored in the
catalog (`PhotoRecord::gps`, catalog format v4). In the reference library about 97% of
JPEGs have a position.

## 2. Goals, non-goals

**Goals (v1 of the map)**
- `M` toggles a split view: content left, map right, in both whole-photo view and grid.
- Online **OpenStreetMap** raster tiles, no API key.
- Map is pannable and zoomable with the mouse.
- Single photo: one location dot; **fly-between-locations** when the open photo changes.
- Grid: dots for the **visible photos only**, the map fitting itself to them.
- Clear error message when the map can't be shown (no internet, no tile plugin).
- All network code and its dependencies live in a **plugin**; the core stays offline
  and dependency-free.

**Non-goals (v1)**
- Vector maps of any kind (MapLibre, PMTiles, ...): too heavy, explicitly avoided.
- Google Maps (API key + billing account, session tokens, strict caching terms).
- Offline maps / an offline fallback basemap.
- Place names, address lookup (reverse geocoding), search on the map.
- (Clicking and hovering dots was a non-goal of v1; added 2026-10-23, see §3.5.)

## 3. The experience

### 3.1 Toggling
- `M` opens the map; `M` again closes it. The state survives switching between grid
  and whole-photo view (open a photo with the map open: the map switches to that
  photo; leave the photo: it switches to the visible-photos dots). Not saved across
  launches.
- `Esc` order becomes: close help → **close map** → leave whole-photo view → leave
  fullscreen.
- Opening and closing animate over 350 ms (ease-in-out, like the whole-photo
  transition): the content area narrows from the full window to the left part while
  the map panel slides in from the right edge.
- The help (`F1`) lists `M`.

### 3.2 Layout
- Map panel: right side, `[map] width` of the window (default 0.4); the content area
  (grid or whole photo) is the remaining left part. A 1-device-px dark separator.
- The **content area behaves exactly like a smaller window**: the grid keeps the photo
  at its centre centred; the whole photo is fitted (and zoomable) inside the left area.
  Implemented by giving `Interaction`/camera the content area as the viewport, so all
  existing layout, focus-morph and zoom logic applies unchanged.
- Overlays follow the content area: stats top-left (unchanged), photo info (`F2`)
  bottom-left (unchanged), photo-effect label top-right **of the content area**, i.e.
  left of the map. Help stays centred on the whole window.
- Attribution `© OpenStreetMap contributors` (text from the tile plugin) is always
  shown in the map panel's bottom-right corner, on a translucent panel (required by
  the OSM tile usage policy).

### 3.3 Map interaction
Pointer events over the map panel go to the map, everything else to the content:
- Drag pans the map (momentum like the grid); wheel zooms about the cursor (smooth,
  like the grid); double-click zooms in one level.
- Zoom range: 2 to the tile source's maximum (OSM: 19). Fractional zoom while
  animating; tiles are drawn scaled.
- Keyboard keeps driving the content (arrows, `+`/`-`, `Home`, `Enter`, digits).

### 3.4 Whole-photo view (single photo)
- One location dot at the photo's position: a filled accent-colour circle with a white
  ring (about 12 px at display scale 1), plus a pulsing halo for 2 s after the map
  arrives, then static (so an idle app stops redrawing).
- Map centred on the dot at `[map] photo_zoom` (default 15).
- **Fly between locations**: when the open photo changes (arrows, `Home`,
  screensaver), the map flies from the current view to the new position and zoom along
  the van Wijk & Nuij "smooth and efficient zooming and panning" path (as used by
  d3-zoom and Mapbox `flyTo`, ρ ≈ 1.42): it zooms out as far as the distance needs,
  travels, and zooms back in. Duration proportional to the path length, clamped to
  0.6–3 s. A new change mid-flight starts a new flight from the current state.
- On opening the map with a photo open, the map starts at zoom 4 around the photo and
  flies in to `photo_zoom` during the slide-in.
- If the user has panned or zoomed the map manually, the next photo change still
  flies to the new photo (manual view is not sticky in single-photo mode).
- Screensaver: as soon as it starts flying through the grid to the next photo, the map
  flies to that photo's location too, taking exactly as long as the grid flight (not
  clamped), so both land together; the pulse waits until the photo opens. A photo
  without GPS dims the map from the start of the flight.
- Photo without GPS: the map keeps its last view, dimmed, with a centred message
  "No location for this item". The next photo with GPS flies from there.
- **Trail** (added 2026-10-23; with `[map] trip_path = on`, see §3.5): stepping through
  photos leaves the earlier photos' locations as gray dots (white ring, like the red
  one) joined in the order shown by a 3 px gray line, under the red dot of the open
  photo. Photos without GPS and repeats of the same spot add nothing; the line takes the
  short way across the antimeridian. Opening a photo from the grid (or opening the map)
  starts a new trail; going back to the grid clears it. At most the last 1000 points.
  The screensaver draws the trail only with `[screensaver] order = sequential` (a random
  tour's trail would just criss-cross the map).

### 3.5 Grid view (multiple photos)
- Dots (about 12 px) for every **visible** photo with GPS. Visible = its cell
  intersects the content area (from the layout, not the streamer's prefetch set).
  Photos at the same position draw one dot.
- **Auto-fit**: the map fits the bounding box of the dots with 10% padding, at most
  zoom 16 (one dot alone: zoom 15). Refitting flies (same path as §3.4).
- While the grid moves, dots update every frame but the map view stays put; it refits
  once the grid has been at rest for 300 ms. This avoids a map that never stops moving
  during a fling.
- Manual pan/zoom of the map pauses auto-fit until the visible set changes again (the
  next grid move), then auto-fit resumes.
- No visible photo has GPS: keep the last view, dimmed, message "No locations among
  the visible photos".
- **Linking grid and map** (added 2026-10-23; grid only, off while a photo is open):
  hovering a dot dims the other photos and puts a subtle white frame around the photo(s)
  taken there, enlarges the dot and shows a pointing hand; clicking it opens that photo
  (of several at one spot: the one nearest the grid's centre), after which the map flies
  to it as in §3.4. Hovering a photo in the grid enlarges its dot.
- **Trip path** (added 2026-10-23; `[map] trip_path = on`, off by default; grid only): a
  3 px line under the dots joins the visible photos in the order they were taken,
  coloured by date from blue (earliest visible) through yellow to red (latest). It breaks
  where more than `[map] trip_gap` hours (default 10, 0.5–720) pass between two photos,
  so separate days or a flight home are not joined; photos in a row at one spot are one point; it crosses the antimeridian the
  short way; points closer than 2 px on screen are merged. Built in `src/map/trip_path`.

### 3.6 Errors
Shown as a message panel centred near the top of the window for 4 s (new overlay
element, same style as the photo-info panel):
- **No tile source loaded** (no plugin offers map tiles): "Map needs a map tile
  plugin (start with --plugin; see README)". The map does not open. (The core never
  names a specific plugin; the README does.)
- **No internet**: the map opens (immediate feedback) and requests the visible tiles.
  If every tile of that first request fails with *offline* and none is in the disk
  cache, the map slides closed again and shows "Map unavailable: no internet
  connection". Tiles already in the disk cache still display without a connection.
- **Connection lost later**: missing tiles keep showing their blurry parent tile; the
  same message appears once per session, the map stays open.
- **Server errors** (HTTP 4xx/5xx other than 404): logged; tile treated as missing.
  HTTP 403/429 (blocked or rate-limited) shows "Map tile server refused requests"
  once.

## 4. Map rendering

### 4.1 Projection and tiles
- Web Mercator (EPSG:3857), 256 px tiles, standard XYZ scheme (`z/x/y`, y from the
  north). Latitude clamped to ±85.0511°. World coordinates in doubles: at zoom `z`
  the world is `256·2^z` px wide.
- Horizontal wrap: tile `x` modulo `2^z`; the camera may pan freely east/west.
  Auto-fit uses the simple min/max longitude box (photos on both sides of the
  antimeridian fit poorly; accepted for v1).
- **Tile level** for a fractional map zoom: the level whose texels are closest to
  device pixels (accounting for the display's pixel density), never magnified more
  than about 1.4×. HiDPI shows smaller labels; accepted for v1 (OSM has no @2x tiles).

### 4.2 Blurry first, then sharp
Same philosophy as photos: while a tile loads, draw the closest loaded **ancestor**
tile's matching quarter, scaled up; level 0–2 tiles are requested first whenever the
map opens so something is always on screen. Loaded tiles cross-fade in (150 ms).

### 4.3 GPU side
- Tiles live in a `PagedTexture` with **256-px slots** (the existing class: mips per
  slot, no bleeding between slots), one 4096² page = 256 tiles (64 MB with mips ~85 MB).
  This counts against `[performance] gpu_memory`.
- The page is created ahead of need: only when a tile-source plugin is loaded, and by
  the existing idle prewarm, never on the frame `M` is pressed (allocating a page
  can take ~100 ms on some drivers).
- LRU eviction by slot; ancestors of visible tiles are kept preferentially.
- Uploads go through the existing upload ring and count toward the per-frame upload
  budget.
- Tile quads are drawn with the existing `TileRenderer` (plain textured quads, no
  effect, no letterbox, scissored to the map panel). Dots, halo and separator with a
  small new `MapRenderer` program (signed-distance circles: filled, ring, halo), one
  instanced draw. Text (attribution, messages) via the existing overlay renderer.
- Font: the overlay font gains `©` (and `°` for the GPS line) via `tools/gen_font.py`;
  non-ASCII characters otherwise still render as `?`.

### 4.4 Performance
- Target unchanged: 60 fps at 4K on the integrated Intel GPU with the map open, also
  while flying. Verify with `--bench` (add a map-open variant of the bench script)
  before/after; record in `docs/perf.md`.
- No network, file I/O or PNG decoding on the main thread. Dots for ≤ 10k visible
  photos are one instanced draw.
- While the map is idle (no flight, no pulse, no pending tiles) the app idles as it
  does today.

## 5. Architecture

### 5.1 Split: core vs plugin

| Core (`pb_core` / `pb_app` / `pb_gfx`, no new dependencies) | Plugin `plugins/maptiles` (libcurl + stb_image) |
|---|---|
| `M` toggle, split layout and its animation, input routing | HTTP(S) download of tiles |
| Web Mercator maths, tile ranges, ancestor fallback, LRU | URL template expansion, User-Agent |
| Map camera: pan/zoom/momentum, fly path, auto-fit | Disk tile cache (honours HTTP caching headers) |
| Dot placement for single/visible photos | PNG/JPEG decode to RGBA (`PbImageOut`) |
| Tile requests on a dedicated worker pool, cancellation | Mapping network failures to status codes |
| Drawing tiles, dots, attribution, messages | Attribution text, zoom range |

Without the plugin the core contains all map code but `M` only shows the "needs a map
tile plugin" message. The core never names the plugin; it looks for *any* loaded
plugin offering a tile source (the first one wins; more than one logs a warning).

### 5.2 Plugin ABI v2 (append-only, `PB_PLUGIN_ABI_VERSION` 1 → 2)

Appended to `PbHost`:
```c
/* Value of `key` in this plugin's config section [plugin.<name>], or NULL if unset.
 * The string stays valid for the plugin's lifetime. Any thread. */
const char* (*setting)(void* ctx, const char* key);
/* Per-user cache root (UTF-8, exists), shared by all libraries, e.g.
 * ~/.cache/photobrowse. A plugin keeps its files in a subfolder named after itself. */
const char* cache_dir;
```

Appended to `PbPlugin`:
```c
/* ---- Map tiles (optional; tile_get NULL = no tile source) ---- */
const char* tile_attribution; /* UTF-8, shown on the map, e.g. "© OpenStreetMap contributors" */
int tile_min_zoom, tile_max_zoom; /* e.g. 0, 19 */
/* Worker threads (a dedicated pool, see §5.4); may block on the network for up to its
 * own timeout. Fills `out` with a 256x256 RGBA image on PB_TILE_OK. */
int (*tile_get)(void* self, int z, int x, int y, PbImageOut* out);
```
with
```c
enum PbTileResult {
  PB_TILE_OK = 0,
  PB_TILE_MISSING = 1, /* no such tile (404, out of range): draw the ancestor, don't retry */
  PB_TILE_OFFLINE = 2, /* network unreachable / DNS / timeout: retry later */
  PB_TILE_REFUSED = 3, /* 403 / 429: back off, show the "refused" message once */
  PB_TILE_ERROR = 4,   /* anything else: log, treat as missing for this session */
};
```
Today host and plugins require an exact `abi_version` match (`plugin_host.cpp`,
`video_plugin.cpp`, `test_plugin.c`), so a plugin binary built against v1 is refused
with the existing clear message and must be rebuilt; all plugins are built in-tree,
so the bump is just a rebuild. The video plugin sets the new tile fields to NULL/0
(= no tile source); host, video plugin and fake test plugin change in one commit.
(Relaxing the check to "same major, read only `struct_size` bytes" is possible later
but not needed for v1.)

### 5.3 Core modules (new)
- `src/map/mercator.{h,cpp}` (pb_core): lat/lon ↔ world px at a zoom, tile range
  covering a viewport, wrap, clamp.
- `src/map/map_camera.{h,cpp}` (pb_core): centre (lat/lon or world px) + fractional
  zoom; pan, zoom about a pivot, momentum (reusing `view/motion`), `flyTo` with the
  van Wijk & Nuij path, `fitBounds(box, padding, maxZoom)`.
- `src/map/tile_set.{h,cpp}` (pb_core): which tiles are wanted for a camera,
  resident/requested/failed state per tile, ancestor fallback, LRU, request
  priorities (visible first, coarse levels first).
- `src/map/map_view.{h,cpp}` (pb_core): the mode logic: open/close animation progress,
  content/panel rects, single vs grid mode, auto-fit debounce, manual-override flag,
  "no location" states, error decisions (first-batch all-offline → close + message).
  Pure, unit-tested, drives `MapCamera`.
- `src/app/map_tiles.{h,cpp}` (pb_app): request queue onto the tile worker pool,
  results into the `PagedTexture` via `TextureSink`-style upload.
- `src/gfx/map_renderer.{h,cpp}`: dots/halo/separator program.
- `PluginHost`: exposes the tile source (`std::optional<TileSource>` with
  `get(z, x, y) -> {result, Image}` plus attribution and zoom range).
- Overlay: `buildMessage(text, ...)` for the error panel; effect label positioned by
  the content rect.

### 5.4 Threading
- Tile fetches run on a **separate pool of 2 threads**, never on the photo decode
  workers, so a slow or dead network can't delay photo loading. 2 also respects the
  OSM policy's request for few parallel connections.
- Requests carry priorities and are cancelled when their tile is no longer wanted
  (existing `JobSystem` cancellation). Queue depth bounded (only wanted tiles).
- Results are handed to the main thread through a lock-free/short-lock queue; the
  main thread only uploads (budgeted).

### 5.5 The `maptiles` plugin
- Dependencies: **libcurl** (Ubuntu `libcurl4-openssl-dev`; Windows vcpkg `curl`),
  **stb_image** (vendored single header in `third_party/`, decodes PNG and JPEG). No
  link to the application. Built only when libcurl is found; otherwise CMake prints a
  status line (like the video plugin and FFmpeg).
- Config `[plugin.maptiles]` (read via `PbHost.setting`):
  ```ini
  [plugin.maptiles]
  # Tile URL; {z} {x} {y} are replaced, {key} by api_key.
  # url = https://tile.openstreetmap.org/{z}/{x}/{y}.png
  # api_key =
  # attribution = © OpenStreetMap contributors
  # max_zoom = 19
  # disk_cache = 500        (MB; least recently used tiles are deleted beyond it)
  ```
  The URL is configurable so another raster provider (with a key) is a settings
  change, but OSM is the default and the only one supported/tested in v1.
- OSM tile usage policy compliance (https://operations.osmfoundation.org/policies/tiles/):
  identifying `User-Agent: PhotoBrowse/<version> (+<project URL>)`; visible
  attribution; tiles cached on disk and HTTP caching headers honoured (at least 7
  days when the server sends none); no bulk download or prefetch beyond the visible
  area plus its ancestors; at most 2 parallel connections.
- Disk cache: `<cache_dir>/maptiles/<hash of url template>/<z>/<x>/<y>.<ext>` plus
  a small sidecar with the expiry time; written atomically; LRU trim to `disk_cache`
  on a worker at startup and every few hundred writes.
- Timeouts: connect 5 s, total 10 s per tile. curl errors for DNS, connect and
  timeout → `PB_TILE_OFFLINE`; 404 → `MISSING`; 403/429 → `REFUSED` (plugin backs
  off for 60 s); other → `ERROR`.

### 5.6 Configuration (core)
New section in `defaultConfigText()` with a test, per the project rules:
```ini
[map]
# Fraction of the window the map panel takes when shown with M.
# width = 0.4
# Map zoom when showing a single photo's location (2-19; 15 = streets).
# photo_zoom = 15
```

### 5.7 Privacy
Tile requests reveal the approximate locations of the photos being browsed to the
tile server. The README states this next to the plugin. Tiles are only requested
while the map is open.

## 6. Testing
Write the failing test first for all pure logic (project rule).
- **mercator**: known lat/lon ↔ tile x/y at several zooms (e.g. 0/0 at z0; Oslo,
  New York, Sydney), clamp at ±85.0511°, wrap of x, tile range for a viewport.
- **map_camera**: zoom about a pivot keeps the pivot's lat/lon fixed; flyTo starts and
  ends exactly at the given views, zooms out mid-way for long distances and not for
  short ones, duration clamped; fitBounds contains every point with padding and
  respects maxZoom; single point → photo zoom.
- **tile_set**: wanted tiles for a view; ancestor fallback picks the nearest loaded
  ancestor and its sub-rect; LRU evicts least recently drawn first and keeps
  ancestors; coarse levels requested first; MISSING not retried, OFFLINE retried.
- **map_view**: `M` with no tile source → message, stays closed; open/close
  animation rects; content viewport = left part; Esc order; single ↔ grid switching;
  auto-fit only after 300 ms at rest; manual override paused until the visible set
  changes; first batch all offline and nothing cached → closes with message; cached
  tiles → stays open.
- **interaction**: narrowing the viewport during the animation keeps the centre photo
  centred (existing viewport-change behaviour) and keeps the focused photo fitted.
- **plugin host**: fake plugin in `tests/plugins/` gains a tile source; host detects
  it, passes settings and cache dir, maps results; v1-sized plugin table = no tile
  source.
- **maptiles plugin**: URL template expansion; cache path; status mapping. Tests run
  **without network** using a `file://` URL template over a temp folder of PNGs
  (curl supports `file://`), plus an unreachable `http://127.0.0.1:<closed port>`
  for the offline path.
- **overlay**: message panel placement; effect label right edge = content rect edge;
  attribution inside the map panel.
- Visual check with `--snapshot` once a map start option exists for testing (e.g. a
  hidden `[map] open = on` used only by tests/snapshots), waiting for pending tiles
  with a timeout.

## 7. Implementation phases
1. **Pure core logic** (no GL, no network): mercator, map camera + fly path +
   fit-bounds, tile set, map view state machine, config section. All test-first.
2. **ABI v2 + host**: `PbHost.setting`/`cache_dir`, tile-source fields, fake plugin,
   `[plugin.<name>]` config sections, video plugin rebuilt against v2 unchanged.
3. **Rendering + app wiring**: split layout and animation, input routing, tile
   texture page + uploads, tile drawing with ancestor fallback, dots/halo renderer,
   attribution, error messages, help entry, `©`/`°` glyphs.
4. **`maptiles` plugin**: curl + stb_image, disk cache, policy compliance, status
   mapping, offline tests; README section (dependency, privacy, attribution).
5. **Polish + verification**: fly-between-locations tuning, bench at 4K with the map
   open, `docs/perf.md`, update `docs/spec.md` (keys table, decisions) and
   `docs/architecture.md` (§13 tile sources, module map).

## 8. Decisions log
Decided by the user:
- Key `M` toggles the map.
- Online mode with OpenStreetMap tiles; no API key. If the map can't load because
  there is no internet, pressing `M` gives an error message.
- In the grid the map shows **only the visible photos**.
- No vector maps. Map tiles come from a **plugin** (the network dependency stays out
  of the core).
- Fly-between-locations is in the first version.
- Other extras are deferred and documented below for later.

Calls made by Claude (open to change):
- Split view with the map on the right at 40% of the window (`[map] width`); the
  content area acts like a smaller window, reusing all existing view logic.
- Map state persists between grid and whole-photo view; not saved across launches;
  `Esc` closes the map before leaving whole-photo view.
- Single-photo zoom 15; grid auto-fit max zoom 16 with 10% padding; refit after 300 ms
  of grid rest; manual map moves pause auto-fit until the grid moves.
- Fly path = van Wijk & Nuij (ρ ≈ 1.42), 0.6–3 s.
- Dots are display-only in v1 (no hover/click actions).
- Cached tiles still show offline; the "no internet" error appears only when nothing
  needed can be shown.
- Pulse halo for 2 s, then static, so the app can idle.
- Tile URL configurable, OSM default; stb_image for decoding; libcurl for HTTP; 2
  dedicated fetch threads; 500 MB disk tile cache shared by all libraries.
- ABI extended generically (tile source + host settings/cache dir), version 2.
- Error messages as a 4-second panel near the top centre.
- Screensaver (2026-10-23): the map and the grid fly to the next photo at the same
  time and land together (before, the map waited for the photo to open).

## 9. Open questions (decide during implementation)
- Map panel colours on the app's dark background: OSM's bright standard style may
  feel loud; a "dim map" option (darken/desaturate in the tile shader) is listed
  under later extras.
- Whether the separator/panel needs rounded corners or a shadow to match
  `[view] corner_radius`.
- Exact dot colours (accent from `[background] color`? fixed orange?).

## 10. Risks
- **OSM policy**: heavy use can get the client blocked (HTTP 403/429). Mitigated by
  caching, the User-Agent, no prefetch, 2 connections, and the `REFUSED` back-off.
  A heavy user should switch `url` to a provider with a free key.
- **TLS on Windows**: libcurl via vcpkg with Schannel; certificate setup verified on
  Linux only until a Windows machine/CI exists.
- **Viewport animation** feeding `Interaction::setViewport` every frame for 350 ms:
  must not drift or snap the grid; covered by tests (§6).

## 11. Effort estimate
Roughly: phase 1 two to three sessions, phase 2 one, phase 3 two to three, phase 4
one to two, phase 5 one. The core logic is the largest part; the plugin is small.

## 12. Later extras (not selected for v1, for later consideration)
Ordered roughly by value for effort.
1. **Thumbnail pins**: show the always-resident 32 px thumbnails (L0) as map markers
   instead of dots, in the grid mode. Cheap; very visual.
2. ~~**Trip path**~~: done (§3.5, `[map] trip_path`).
3. **Clustering**: zoomed out, merge nearby dots into one bubble with a count.
4. ~~**Two-way linking**~~: done (§3.5).
5. **View direction cone**: draw a wedge for the direction the camera faced, from
   EXIF `GPSImgDirection` (tags 16/17; present in the Pixel photos). Needs the parser
   and a catalog field (format bump).
6. **Street / satellite toggle**: a key to switch between two configured tile URLs
   (satellite needs a provider such as MapTiler or Esri, usually with a key).
7. **Dim / dark map**: darken or desaturate tiles in the shader to suit the dark UI.
8. **Map selection**: drag a box on the map to filter the grid or fly to those photos
   (ties into the planned search + fly-to, spec §5).
9. **Whole-library heatmap**: a zoomed-out density view of every photo's location.
10. **Mini-map inset**: alternative layout, a small map in a corner instead of the
    split view.
11. **Screensaver map**: show the map inset while the screensaver flies between
    photos, travelling along with it.
12. ~~**Video locations**~~ — done 2026-10-23: `PbMediaInfo` has latitude/longitude/
    altitude (ABI v3); the video plugin parses the ISO 6709 `location` /
    `com.apple.quicktime.location.ISO6709` tag (`plugins/video/iso6709.h`).
13. **Offline fallback basemap**: a built-in world outline (Natural Earth, public
    domain, a few hundred KB) shown when offline, so the map works for zoomed-out
    views without a network.
14. **Place names**: reverse geocoding ("Manhattan, New York") in the info panel
    (Nominatim has its own usage policy; offline datasets are large).
15. **Other providers / keys** beyond a URL template: e.g. Google Maps (needs
    dedicated code for session tokens, a key and billing; terms restrict caching).
    Not recommended.
16. **Offline vector maps** (Protomaps PMTiles + a vector renderer): rejected for now
    as too heavy; revisit only if offline street-level maps become a must.
