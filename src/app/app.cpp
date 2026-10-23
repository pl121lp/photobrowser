#include "app/app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "app/background_presets.h"
#include "app/config.h"
#include "app/gpu_textures.h"
#include "app/library_index.h"
#include "app/map_tiles.h"
#include "app/sdl_audio.h"
#include "app/streamer.h"
#include "core/frame_history.h"
#include "core/frame_stats.h"
#include "gfx/background_renderer.h"
#include "gfx/gl.h"
#include "gfx/map_renderer.h"
#include "gfx/overlay_renderer.h"
#include "gfx/tile_renderer.h"
#include "imaging/jpeg.h"
#include "layout/content.h"
#include "layout/layout.h"
#include "library/png_handler.h"
#include "map/map_view.h"
#include "platform/paths.h"
#include "plugin/plugin_host.h"
#include "view/camera.h"
#include "view/bench_script.h"
#include "view/interaction.h"
#include "view/screensaver.h"

namespace pb {

namespace fs = std::filesystem;

namespace {

constexpr int64_t kBackgroundPriority = int64_t(1) << 40;

void* glProc(const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }

std::optional<Key> mapKey(SDL_Keycode k) {
  switch (k) {
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_EQUALS:
    case SDLK_PLUS:
    case SDLK_KP_PLUS: return Key::ZoomIn;
    case SDLK_MINUS:
    case SDLK_KP_MINUS: return Key::ZoomOut;
    case SDLK_HOME: return Key::Home;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return Key::Enter;
    case SDLK_ESCAPE: return Key::Escape;
    default: return std::nullopt;
  }
}

// Photo effect for keys 0-9 (top row or keypad); -1 for any other key.
int photoEffectKey(SDL_Keycode k) {
  if (k >= SDLK_0 && k <= SDLK_0 + TileRenderer::kPhotoEffects) return int(k - SDLK_0);
  if (k == SDLK_KP_0) return 0;
  if (k >= SDLK_KP_1 && k < SDLK_KP_1 + TileRenderer::kPhotoEffects) return int(k - SDLK_KP_1) + 1;
  return -1;
}

double seconds(uint64_t ns) { return double(ns) * 1e-9; }

// Reads the back buffer (call before swap) and writes it as a JPEG.
bool saveSnapshot(const fs::path& path, Vec2d viewport) {
  Image img{uint32_t(viewport.x), uint32_t(viewport.y), {}};
  img.rgba.resize(size_t(img.w) * img.h * 4);
  gl::PixelStorei(GL_PACK_ALIGNMENT, 4);
  gl::ReadPixels(0, 0, GLsizei(img.w), GLsizei(img.h), GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
  const size_t row = size_t(img.w) * 4;  // GL rows are bottom-up
  for (uint32_t y = 0; y < img.h / 2; ++y) {
    std::swap_ranges(img.rgba.begin() + long(y * row), img.rgba.begin() + long((y + 1) * row),
                     img.rgba.begin() + long((img.h - 1 - y) * row));
  }
  auto jpg = encodeJpeg(img, 92);
  return jpg && atomicWriteFile(path, *jpg);
}

// Sub-rect `inner` (unit square coords) of `outer`.
UvRect compose(const UvRect& outer, const UvRect& inner) {
  const float w = outer.u1 - outer.u0, h = outer.v1 - outer.v0;
  return {outer.u0 + w * inner.u0, outer.v0 + h * inner.v0, outer.u0 + w * inner.u1, outer.v0 + h * inner.v1};
}

void setRect(float (&dst)[4], const Rect& r) {
  dst[0] = float(r.x0);
  dst[1] = float(r.y0);
  dst[2] = float(r.x1);
  dst[3] = float(r.y1);
}
void setUv(float (&dst)[4], const UvRect& u) {
  dst[0] = u.u0;
  dst[1] = u.v0;
  dst[2] = u.u1;
  dst[3] = u.v1;
}

// Limits drawing to `r` (window px, top-left origin) in a window `windowHeight` px tall.
void scissor(const Rect& r, double windowHeight) {
  const long x0 = std::lround(r.x0), x1 = std::lround(r.x1), y0 = std::lround(r.y0), y1 = std::lround(r.y1);
  gl::Scissor(GLint(x0), GLint(std::lround(windowHeight) - y1), GLsizei(std::max(0L, x1 - x0)),
              GLsizei(std::max(0L, y1 - y0)));
}

constexpr uint64_t kNoticeNs = 4'000'000'000ULL;  // map notices stay this long
const Rgb8 kMapBackground{0x24, 0x24, 0x28};     // map panel where no tile is loaded yet
const Rgb8 kDotColor{0xe8, 0x45, 0x2c};

// Everything that exists once the library is loaded.
struct Session {
  LibraryState lib;
  std::unique_ptr<Streamer> streamer;
};

}  // namespace

int App::run(const Options& options) {
  // Settings: config file, then command-line overrides.
  const fs::path configPath = options.configFile ? *options.configFile : defaultConfigPath();
  ConfigResult loaded = loadConfig(configPath, !options.configFile);
  for (const auto& w : loaded.warnings) std::fprintf(stderr, "photobrowse: %s\n", w.c_str());
  Config config = loaded.config;
  applyOptions(config, options);

  std::error_code ec;
  const fs::path root = fs::absolute(config.photoFolder, ec);
  if (ec || !fs::is_directory(root, ec)) {
    std::fprintf(stderr, "photobrowse: photo folder '%s' does not exist\n", toUtf8(config.photoFolder).c_str());
    return 2;
  }
  const size_t vramBudget = size_t(config.gpuMemoryMB) << 20;
  const auto diskCacheCap = uint64_t(config.diskCacheGB * double(uint64_t(1) << 30));
  const fs::path cacheRoot = config.cacheDir.empty() ? defaultCacheRoot() : fs::absolute(config.cacheDir);
  const fs::path libDir = libraryCacheDir(cacheRoot, root);
  if (options.rebuildCache) fs::remove_all(libDir, ec);
  fs::create_directories(libDir, ec);

  // GPU choice must be in the environment before SDL loads the OpenGL driver.
  if (config.gpu != GpuPreference::Auto) {
    const GpuEnvironment env = gpuEnvironment(config.gpu, nvidiaDriverLoaded());
    for (const auto& name : env.unset) unsetEnvironment(name);
    for (const auto& [name, value] : env.set) setEnvironment(name, value);
    std::printf("gpu: %s requested\n", config.gpu == GpuPreference::Discrete ? "discrete" : "integrated");
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  // Plugins (--plugin): looked up in plugins/ next to the executable, then beside it.
  // Declared before everything that uses their formats or players.
  SdlAudio audio;
  auto pluginHost = std::make_unique<PluginHost>(&audio, config.pluginSettings, cacheRoot / "photobrowse");
  PluginHost& plugins = *pluginHost;
  static const PngHandler pngHandler;  // built in; registered first so no plugin can replace it
  plugins.addBuiltinFormat(".png", &pngHandler, kPngOrigin);
  if (!options.plugins.empty() || options.allPlugins) {
    std::vector<fs::path> pluginDirs;
    if (const char* base = SDL_GetBasePath()) pluginDirs = {fromUtf8(base) / "plugins", fromUtf8(base)};
    for (const auto& name : options.plugins) {
      if (auto err = plugins.load(name, pluginDirs)) {
        std::fprintf(stderr, "photobrowse: %s\n", err->c_str());
        SDL_Quit();
        return 2;
      }
    }
    // --all-plugins: the rest of the plugin folder; one that can't load is skipped.
    if (options.allPlugins) {
      for (const auto& name : discoverPlugins(pluginDirs)) {
        if (auto err = plugins.load(name, pluginDirs)) std::fprintf(stderr, "photobrowse: warning: %s\n", err->c_str());
      }
    }
    for (const auto& p : plugins.plugins()) {
      std::string exts;
      for (const auto& e : p.extensions) exts += " " + e;
      std::printf("plugin: %s %s (%s)%s%s\n", p.name.c_str(), p.version.c_str(), toUtf8(p.path).c_str(),
                  exts.empty() ? "" : " files:", exts.c_str());
    }
    audio.init();
  }
  const MediaFormats& formats = plugins.formats();
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);

  // Fullscreen without a set mode is borderless desktop fullscreen; --bench always uses it.
  const SDL_WindowFlags fullscreen = config.fullscreen || options.bench ? SDL_WINDOW_FULLSCREEN : 0;
  SDL_Window* window = SDL_CreateWindow("PhotoBrowse", 1280, 800,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | fullscreen);
  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context || !gl::load(glProc)) {
    std::fprintf(stderr, "OpenGL 4.1 core context unavailable: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  if (!config.vsync) {
    SDL_GL_SetSwapInterval(0);
  } else if (!SDL_GL_SetSwapInterval(-1)) {
    SDL_GL_SetSwapInterval(1);  // no adaptive vsync
  }
  std::printf("OpenGL: %s (%s)\n", reinterpret_cast<const char*>(gl::GetString(GL_RENDERER)),
              reinterpret_cast<const char*>(gl::GetString(GL_VERSION)));
  gl::Enable(GL_FRAMEBUFFER_SRGB);

  int exitCode = 0;
  {
    // Destruction order matters: streamer/indexer before the job system, the job
    // system before the cache and catalog its jobs reference.
    ThumbCache thumbs(libDir);
    Session session;
    auto stopBackground = std::make_shared<std::atomic<bool>>(false);  // set on quit
    auto jobs = std::make_unique<JobSystem>(streamingWorkerCount(unsigned(config.decodeThreads)));
    std::unique_ptr<JobSystem> indexJobs;  // all cores, only while first-run indexing
    std::unique_ptr<Indexer> indexer;
    GpuTextures textures;
    TileRenderer renderer;
    OverlayRenderer overlay;
    MapRenderer mapRenderer;
    if (!renderer.init() || !overlay.init() || !mapRenderer.init()) exitCode = 1;
    // Map (M): tiles come from a plugin's tile source, fetched on their own threads.
    const TileSource* tileSource = plugins.tileSource();
    std::unique_ptr<MapTiles> mapTiles;
    if (tileSource) {
      mapTiles = std::make_unique<MapTiles>(*tileSource, textures);
      textures.wantMapPage();
    }
    MapView map({config.mapWidth, config.mapPhotoZoom});
    if (config.mapOpen) map.toggle(seconds(SDL_GetTicksNS()), tileSource);
    std::vector<MapTileDraw> mapDraws;
    std::vector<TileInstance> mapInstances;
    std::vector<MapDot> mapDots;
    std::vector<uint32_t> visiblePositions;
    std::vector<GeoPos> visibleGps;
    std::vector<uint32_t> visibleGpsPos;     // layout positions of visibleGps
    std::optional<Vec2d> pointerPx;          // mouse in the window (hover links grid and map)
    std::vector<OverlayQuad> highlightFrames;
    SDL_Cursor* handCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    bool handShown = false;
    bool mapTilesCancelled = true;
    std::string notice;
    uint64_t noticeUntil = 0;
    renderer.setCornerRadius(float(config.cornerRadius));
    renderer.setTransparentBars(config.letterbox && config.transparentBars);
    BackgroundRenderer background;
    // The user's shader file is read once here; F6 switches without file I/O.
    const std::string userShader = toUtf8(config.backgroundShader);
    std::string userShaderSource;
    if (!userShader.empty() && !isPresetName(userShader)) {
      if (auto file = readWholeFile(config.backgroundShader)) userShaderSource.assign(file->begin(), file->end());
    }
    const std::vector<std::string> backgroundChoices = backgroundCycle(userShaderSource.empty() ? "" : userShader);
    std::string backgroundName;
    // Switches to a background by name: "solid", a built-in preset, or the shader file.
    auto setBackground = [&](const std::string& name) {
      std::string source, what = "solid color";
      bool animated = false;
      if (name == "solid") {  // the clear colour alone
      } else if (const BackgroundPreset* preset = findBackgroundPreset(name)) {
        source = preset->source;
        animated = preset->animated;
        what = "preset " + name;
      } else if (!userShaderSource.empty()) {
        source = userShaderSource;
        animated = shaderUsesTime(source);
        what = "shader file " + name;
      } else {
        std::fprintf(stderr, "photobrowse: background shader '%s' is not a preset or a readable file\n", name.c_str());
      }
      const auto [color, color2] = backgroundColors(config, name);
      if (!background.init(toLinear(color), toLinear(color2), source, animated)) {
        std::fprintf(stderr, "photobrowse: background shader failed to compile (details above); using solid color\n");
        what = "solid color";
        animated = false;
      }
      backgroundName = name;
      std::printf("background: %s%s\n", what.c_str(), animated ? " (animated)" : "");
      std::fflush(stdout);
    };
    if (config.background == BackgroundMode::Shader && config.backgroundShader.empty()) {
      std::fprintf(stderr, "photobrowse: background mode = shader needs shader = <preset or file>\n");
    }
    background.setSpeed(float(config.backgroundSpeed));
    background.setDensity(float(config.backgroundDensity));
    setBackground(backgroundSetting(config));
    const FontInfo overlayFont = OverlayRenderer::font();
    FrameHistory history;  // frame intervals for the fullscreen stats overlay
    std::vector<OverlayQuad> overlayQuads;

    std::unique_ptr<Layout> layout = makeLayout(options.layout, config.gap);
    layout->build(0);
    Camera camera;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    camera.viewport = {double(pw), double(ph)};
    Vec2d windowPx = camera.viewport;  // drawable size; the content (camera) gets its left part
    Interaction ui(camera, *layout, config.maxPhotoZoom);
    Screensaver screensaver(config.screensaverHold);
    screensaver.setSlowZoom(config.screensaverSlowZoom);
    auto setScreensaver = [&](bool on) {
      if (on == screensaver.active()) return;
      if (on) {
        screensaver.start();
        SDL_HideCursor();
      } else {
        screensaver.stop();
        SDL_ShowCursor();
      }
    };
    if (config.screensaver && !options.bench && !options.snapshot) setScreensaver(true);

    size_t prewarmPages = 0;
    auto startSession = [&](LibraryState lib) {
      session.lib = std::move(lib);
      session.lib.catalog.sortOrder(config.newestFirst);  // saved oldest first; cheap to redo
      layout->build(uint32_t(session.lib.catalog.order.size()));
      ui.layoutChanged();
      ui.setStartPosition(startPosition(config.startAt, layout->count(), layout->centerPosition(),
                                        config.newestFirst, std::random_device{}()));
      ui.setStartZoom(config.startZoom);
      ui.resetView();
      const size_t l0Pages = (session.lib.catalog.photos.size() + 16383) / 16384;
      const size_t fullReserve = size_t(Streamer::kFullSlots) * 3840 * 2880 * 4 * 4 / 3;
      const size_t mapReserve = tileSource ? kPageBytes : 0;
      const size_t budget = vramBudget - std::min(vramBudget / 2, l0Pages * kPageBytes + fullReserve + mapReserve);
      prewarmPages = l0Pages + budget / kPageBytes;
      session.streamer =
          std::make_unique<Streamer>(session.lib.catalog, root, thumbs, *jobs, textures, budget, formats);
      session.streamer->setL0(session.lib.l0);
      session.streamer->setUploadTimeBudget(3.0);
      const std::string title = session.lib.catalog.order.empty()
                                    ? "PhotoBrowse — no photos in " + toUtf8(config.photoFolder)
                                    : "PhotoBrowse — " + toUtf8(root.filename());
      SDL_SetWindowTitle(window, title.c_str());
    };

    if (auto warm = loadWarm(libDir, formats, config.recursive)) {
      startSession(std::move(*warm));
      // Pick up folder changes for the next launch; trim the disk cache.
      auto snapshot = std::make_shared<LibraryState>(session.lib);
      jobs->submit(kBackgroundPriority, [root, libDir, snapshot, stop = stopBackground, &thumbs, &formats,
                                         recursive = config.recursive, diskCacheCap] {
        refreshLibrary(root, libDir, *snapshot, thumbs, stop.get(), formats, recursive);
        if (!*stop) thumbs.sweep(diskCacheCap);
      });
    } else {
      indexJobs = std::make_unique<JobSystem>(defaultWorkerCount());
      indexer = std::make_unique<Indexer>(root, libDir, *indexJobs, thumbs, formats, config.recursive);
      indexer->start();
    }

    std::vector<TileDraw> draws;
    std::vector<std::pair<uint64_t, TileInstance>> keyed;
    std::vector<TileInstance> batch;
    uint64_t last = SDL_GetTicksNS();
    const uint64_t start = last;
    uint64_t lastTitle = 0;
    bool running = exitCode == 0, reportedGlError = false, animating = true;
    bool showStats = config.stats, showInfo = false, showHelp = false, prewarming = false, reportedFirstFrame = false;
    FrameStats intervals, work, backgroundGpu;  // ms; F3 window or the whole --bench run
    if (options.bench) background.enableTiming();
    std::optional<BenchScript> bench;
    uint64_t lastSwap = 0, benchStart = 0;
    // Live content from a plugin for the photo open in whole-photo view (e.g. a video).
    std::unique_ptr<Player> player;
    std::optional<PhotoId> playerId;
    bool autoplayPending = false;  // [view] autoplay: start once the item is fully open

    while (running) {
      // --- Input ---------------------------------------------------------------
      // With stats shown, keep rendering at the display rate so the numbers mean something.
      const bool wait = ui.idle() && !animating && !prewarming && !indexer && !options.snapshot && !options.bench &&
                        !showStats && !background.animated() && !(player && player->active()) &&
                        !screensaver.animating() && !map.animating(seconds(SDL_GetTicksNS())) &&
                        (!map.shown() || !mapTiles ||
                         (mapTiles->pending() == 0 && !mapTiles->fading(seconds(SDL_GetTicksNS())))) &&
                        (!session.streamer || session.streamer->stats().pendingRequests == 0);
      SDL_Event ev;
      bool have = wait ? SDL_WaitEventTimeout(&ev, 100) : SDL_PollEvent(&ev);
      const float density = SDL_GetWindowPixelDensity(window);
      auto px = [&](float x, float y) { return Vec2d{double(x) * density, double(y) * density}; };
      while (have) {
        const double t = seconds(ev.common.timestamp);
        switch (ev.type) {
          case SDL_EVENT_QUIT: running = false; break;
          case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            windowPx = {double(ev.window.data1), double(ev.window.data2)};  // the content follows each frame
            break;
          case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            setScreensaver(false);  // any click, scroll or key (except the F-key toggles) stops it
            // Pointer events over the map panel go to the map, the rest to the content.
            const Vec2d p = px(ev.button.x, ev.button.y);
            if (ev.button.button == SDL_BUTTON_LEFT && !map.pointerDown(p, t)) ui.onPointerDown(p, t);
            if (ev.button.button == SDL_BUTTON_RIGHT && !(map.shown() && map.panelRect(windowPx).contains(p))) {
              ui.onSecondaryClick();
            }
            break;
          }
          case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ev.button.button == SDL_BUTTON_LEFT) {
              const Vec2d p = px(ev.button.x, ev.button.y);
              if (!map.pointerUp(p, t)) ui.onPointerUp(p, t);
            }
            break;
          case SDL_EVENT_MOUSE_MOTION: {
            const Vec2d p = px(ev.motion.x, ev.motion.y);
            pointerPx = p;
            if (!map.pointerMove(p, t)) ui.onPointerMove(p, t);
            break;
          }
          case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            pointerPx.reset();
            map.pointerLeave();
            break;
          case SDL_EVENT_MOUSE_WHEEL: {
            setScreensaver(false);
            const float dy = ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
            const Vec2d p = px(ev.wheel.mouse_x, ev.wheel.mouse_y);
            if (!map.wheel(p, dy)) ui.onWheel(p, dy);
            break;
          }
          case SDL_EVENT_KEY_DOWN: {
            const SDL_Keycode k = ev.key.key;
            if (k == SDLK_Q) {
              running = false;
            } else if (k == SDLK_F5) {
              setScreensaver(!screensaver.active());
            } else if (k == SDLK_F6) {
              setBackground(nextBackground(backgroundChoices, backgroundName));
            } else if (k == SDLK_F1) {
              showHelp = !showHelp;
            } else if (k == SDLK_ESCAPE && showHelp) {
              showHelp = false;  // Esc closes the help before anything else
            } else if (k == SDLK_M) {
              map.toggle(t, tileSource);
            } else if (k == SDLK_ESCAPE && map.escape(t)) {
              // then the map
            } else if (k == SDLK_F2) {
              showInfo = !showInfo;
            } else if (k == SDLK_F3) {
              showStats = !showStats;
              intervals.reset();
              work.reset();
              history.clear();
              if (!showStats) SDL_SetWindowTitle(window, ("PhotoBrowse — " + toUtf8(root.filename())).c_str());
            } else if (k == SDLK_F11 || k == SDLK_F) {
              const bool fs = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
              SDL_SetWindowFullscreenMode(window, nullptr);  // borderless desktop
              SDL_SetWindowFullscreen(window, !fs);
            } else if (const int effect = photoEffectKey(k); effect >= 0) {
              setScreensaver(false);
              ui.setPhotoEffect(effect);  // ignored outside the whole-photo view
            } else if (auto key = mapKey(k)) {
              setScreensaver(false);
              const bool arrow = *key == Key::Left || *key == Key::Right || *key == Key::Up || *key == Key::Down;
              const int steps = arrow && (ev.key.mod & SDL_KMOD_SHIFT) ? 5 : 1;  // Shift: hop 5 photos
              if (!ui.onKey(*key, t, steps) && *key == Key::Escape) SDL_SetWindowFullscreen(window, false);
            }
            break;
          }
          default: break;
        }
        have = SDL_PollEvent(&ev);
      }

      // --- Indexing progress -----------------------------------------------------
      const uint64_t now = SDL_GetTicksNS();
      if (indexer) {
        if (indexer->finished()) {
          startSession(indexer->take());
          indexer.reset();
          indexJobs.reset();
        } else if (now - lastTitle > 250'000'000ULL) {
          lastTitle = now;
          const std::string title = indexer->scanned() ? "PhotoBrowse — indexing " + std::to_string(indexer->done()) +
                                                             "/" + std::to_string(indexer->total())
                                                       : "PhotoBrowse — scanning…";
          SDL_SetWindowTitle(window, title.c_str());
        }
      }

      // Allocate GPU textures ahead of need, one per frame while nothing is moving.
      if (session.streamer && ui.idle()) {
        Vec2d maxView = camera.viewport;
        if (const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window))) {
          maxView = {std::max(maxView.x, double(dm->w) * dm->pixel_density),
                     std::max(maxView.y, double(dm->h) * dm->pixel_density)};
        }
        prewarming = textures.prewarm(prewarmPages, maxView);
      } else {
        prewarming = false;
      }

      // --- Update ----------------------------------------------------------------
      const double dt = std::min(seconds(now - last), 0.1);
      last = now;
      if (options.bench && session.streamer && !bench && !prewarming && now - start > 1'000'000'000ULL) {
        bench.emplace();
        benchStart = now;
        intervals.reset();
        work.reset();
      }
      if (bench) bench->drive(ui, camera.viewport, seconds(now));
      // Map: what it shows follows the content (the open photo, else the visible photos);
      // the content gets the part of the window left of the map panel.
      const double uiScale = std::max(1.0f, SDL_GetWindowDisplayScale(window));
      {
        MapView::Subject subject;
        visibleGps.clear();
        visibleGpsPos.clear();
        if (const auto o = ui.opened(); o && session.streamer && *o < session.lib.catalog.order.size()) {
          subject.photo = *o;
          subject.photoGps = session.lib.catalog.photos[session.lib.catalog.order[*o]].gps;
        } else if (map.shown() && session.streamer && !screensaver.active()) {
          visiblePositions.clear();
          layout->visible(camera.visibleWorld(), visiblePositions);
          for (const uint32_t p : visiblePositions) {
            if (p >= session.lib.catalog.order.size()) continue;
            if (const auto& g = session.lib.catalog.photos[session.lib.catalog.order[p]].gps) {
              visibleGps.push_back(*g);
              visibleGpsPos.push_back(p);
            }
          }
          subject.visibleGps = visibleGps;
          subject.visiblePos = visibleGpsPos;
          // The grid photo under the pointer highlights its dot.
          if (pointerPx && map.contentRect(windowPx).contains(*pointerPx)) {
            subject.hoveredPhoto = layout->hit(camera.screenToWorld(*pointerPx));
          }
        }
        subject.gridMoving = !ui.idle();
        // Screensaver between photos: the map stays on the last one until the next opens.
        subject.frozen = screensaver.active() && !subject.photo;
        map.update(seconds(now), dt, windowPx, uiScale, subject, mapTiles ? mapTiles->pending() : 0);
        const Rect content = map.contentRect(windowPx);
        if (const Vec2d size{content.width(), content.height()}; size != camera.viewport) ui.setViewport(size);
        // A click on a dot opens its photo (of several taken there: the one nearest the grid's centre).
        if (const auto clicked = map.takeClickedPhotos(); !clicked.empty() && !ui.opened()) {
          const auto nearest = std::min_element(clicked.begin(), clicked.end(), [&](uint32_t a, uint32_t b) {
            return length(layout->place(a).cell.center() - camera.center) <
                   length(layout->place(b).cell.center() - camera.center);
          });
          if (*nearest < layout->count()) ui.openPhoto(*nearest, seconds(now));
        }
        // A pointing hand over a dot that can be clicked.
        if (const bool hand = !map.hoveredPhotos().empty(); hand != handShown && handCursor) {
          SDL_SetCursor(hand ? handCursor : SDL_GetDefaultCursor());
          handShown = hand;
        }
        for (auto n = map.takeNotice(); n; n = map.takeNotice()) {
          notice = std::string(mapNoticeText(*n));
          noticeUntil = now + kNoticeNs;
          std::printf("map: %s\n", notice.c_str());
          std::fflush(stdout);
        }
      }
      ui.update(seconds(now), dt);
      // After ui.update: reacts to a move ending this frame. Stays while the open item plays.
      screensaver.update(ui, layout->count(), seconds(now), player && player->active());

      // A player exists while its photo is opening or open; a click on it goes to the player.
      std::optional<PhotoId> openId;
      if (const auto o = ui.opened(); o && session.streamer && *o < session.lib.catalog.order.size()) {
        openId = session.lib.catalog.order[*o];
      }
      if (openId != playerId) {
        if (player) textures.releaseLive();
        player.reset();
        playerId = openId;
        if (openId) player = plugins.openPlayer(root / fromUtf8(session.lib.catalog.photos[*openId].relPath));
        autoplayPending = player && config.autoplay;
      }
      // The screensaver moves on when the item ends, so it never loops there.
      if (player) player->setPlayOnce(screensaver.active());
      if (ui.takeActivation() && player) {
        player->activate();
        autoplayPending = false;  // the user took over
      }
      if (autoplayPending && ui.focusProgress() >= 1) {
        autoplayPending = false;
        if (!player->active()) player->activate();
      }
      // Screensaver: live content (e.g. a video) starts by itself once its item is open.
      if (screensaver.takeArrived() && player && !player->active()) player->activate();
      // --- Draw ------------------------------------------------------------------
      const uint64_t tPre = SDL_GetTicksNS();
      const float clear[3] = {background.clearColor()[0], background.clearColor()[1], background.clearColor()[2]};
      renderer.beginFrame(windowPx, clear);
      if (auto gpuMs = background.takeGpuMs(); gpuMs && bench) backgroundGpu.add(*gpuMs);
      background.draw(windowPx, seconds(now - start));
      // Map tiles: requested and uploaded only while the map is shown.
      mapDraws.clear();
      if (mapTiles && map.shown()) {
        mapTiles->frame(seconds(now), map.camera(), mapDraws);
        map.tileResults(mapTiles->takeResults());
        mapTilesCancelled = false;
      } else if (mapTiles && !mapTilesCancelled) {
        mapTiles->cancelAll();
        mapTilesCancelled = true;
      }
      if (!session.streamer) textures.endFrame();
      if (map.shown()) {
        gl::Enable(GL_SCISSOR_TEST);
        scissor(map.contentRect(windowPx), windowPx.y);
      }
      const uint64_t tBegin = SDL_GetTicksNS();
      uint64_t tStream = tBegin;
      animating = false;
      if (session.streamer) {
        session.streamer->frame(seconds(now), camera, *layout, ui.focused(), config.maxPhotoZoom, draws);
        std::optional<std::pair<GLuint, UvRect>> live;
        if (player) {
          if (auto f = player->frame()) live = textures.liveFrame(f->w, f->h, f->stride, f->rgba, f->serial);
        }
        textures.endFrame();
        tStream = SDL_GetTicksNS();
        keyed.clear();
        highlightFrames.clear();
        // Photos of the dot hovered on the map: the others dim, these get a subtle frame.
        const std::vector<uint32_t>& highlighted = ui.focused() ? std::vector<uint32_t>{} : map.hoveredPhotos();
        std::optional<std::pair<uint64_t, TileInstance>> focusedTile;
        for (const TileDraw& d : draws) {
          const PhotoRecord& rec = session.lib.catalog.photos[d.id];
          const Rect r = layout->place(d.pos).photo;
          // Fit fills the square with a centre crop; letterbox shows the whole photo
          // inside it, the image coordinates extended over the bars.
          const Letterbox lb = letterbox(rec.aspect(), r.width() / r.height());
          const UvRect crop = config.letterbox ? lb.uv : cropUv(rec.aspect(), r.width() / r.height());
          const Vec2d a = camera.worldToScreen({r.x0, r.y0});
          const Vec2d b = camera.worldToScreen({r.x1, r.y1});
          TileInstance ti{};
          setRect(ti.rect, {a.x, a.y, b.x, b.y});
          if (config.letterbox) setUv(ti.content, lb.content);
          setUv(ti.uv, compose(d.cur.uv, crop));
          setUv(ti.uvPrev, compose(d.prev.uv, crop));
          ti.fade = d.fade;
          animating |= d.fade < 1;
          const uint64_t key = uint64_t(textures.texture(d.cur)) << 32 | textures.texture(d.prev);
          if (auto f = ui.focused(); f && *f == d.pos) {
            // Whole-photo view: morph from the square crop to the full photo, drawn on top.
            // Morphs from the square crop, or in letterbox mode from the letterboxed photo.
            const Rect start = config.letterbox ? fitRect({a.x, a.y, b.x, b.y}, rec.aspect()) : Rect{a.x, a.y, b.x, b.y};
            const FocusMorph m = focusMorph(start, rec.aspect(), camera.viewport, ui.focusProgress());
            setUv(ti.content, UvRect{0, 0, 1, 1});
            ui.setFocusRect(m.rect);
            setRect(ti.rect, ui.focusDisplayRect());
            setUv(ti.uv, compose(d.cur.uv, m.uv));
            setUv(ti.uvPrev, compose(d.prev.uv, m.uv));
            if (live && playerId == d.id) {  // the player's frame replaces the still
              setUv(ti.uv, compose(live->second, m.uv));
              setUv(ti.uvPrev, compose(live->second, m.uv));
              ti.fade = 1;
              focusedTile.emplace(uint64_t(live->first) << 32 | live->first, ti);
              continue;
            }
            focusedTile.emplace(key, ti);
            continue;
          }
          if (ui.focused() && config.screensaverPhotoOnly && screensaver.active()) {
            // Screensaver "photo only": the other photos fade out to the background.
            ti.alpha = float(1 - ui.focusProgress());
            if (ti.alpha <= 0) continue;
          } else if (ui.focused()) {
            ti.dim = float(0.85 * ui.focusProgress());
          } else if (!highlighted.empty()) {
            if (std::find(highlighted.begin(), highlighted.end(), d.pos) == highlighted.end()) {
              ti.dim = 0.5f;
            } else {
              const Rect shown = config.letterbox ? fitRect({a.x, a.y, b.x, b.y}, rec.aspect()) : Rect{a.x, a.y, b.x, b.y};
              const float frame[4] = {1, 1, 1, 0.7f};
              buildFrame(shown, 2 * uiScale, frame, overlayFont, highlightFrames);
            }
          }
          keyed.emplace_back(key, ti);
        }
        std::sort(keyed.begin(), keyed.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        for (size_t i = 0; i < keyed.size();) {
          size_t j = i;
          batch.clear();
          while (j < keyed.size() && keyed[j].first == keyed[i].first) batch.push_back(keyed[j++].second);
          renderer.draw(GLuint(keyed[i].first >> 32), GLuint(keyed[i].first & 0xffffffffu), batch);
          i = j;
        }
        overlay.draw(windowPx, highlightFrames);
        if (focusedTile) {
          const uint64_t key = focusedTile->first;
          renderer.draw(GLuint(key >> 32), GLuint(key & 0xffffffffu), std::span(&focusedTile->second, 1),
                        ui.photoEffect());
        }
      }

      // Map panel: background, tiles (blurry ancestors until loaded), dots, separator.
      if (map.shown()) {
        const Rect panel = map.panelRect(windowPx);
        scissor(panel, windowPx.y);
        const auto bg = toLinear(kMapBackground);
        gl::ClearColor(bg[0], bg[1], bg[2], 1);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        mapInstances.clear();
        for (const MapTileDraw& d : mapDraws) {
          TileInstance ti{};
          setRect(ti.rect, {d.screen.x0 + panel.x0, d.screen.y0 + panel.y0, d.screen.x1 + panel.x0, d.screen.y1 + panel.y0});
          setUv(ti.uv, d.uv);
          setUv(ti.uvPrev, d.uvPrev);
          ti.fade = d.fade;
          ti.dim = map.dimmed() ? 0.6f : 0.0f;
          // Image area past the tile's edges: no letterbox darkening of the edge pixels (seams).
          setUv(ti.content, UvRect{-0.01f, -0.01f, 1.01f, 1.01f});
          mapInstances.push_back(ti);
        }
        renderer.setCornerRadius(0);
        renderer.setTransparentBars(false);
        renderer.draw(textures.mapTexture(), textures.mapTexture(), mapInstances);
        renderer.setCornerRadius(float(config.cornerRadius));
        renderer.setTransparentBars(config.letterbox && config.transparentBars);
        mapDots.clear();
        const auto dotColor = toLinear(kDotColor);
        const bool single = map.singleMode();
        const std::optional<double> pulse = map.pulse(seconds(now));
        const std::optional<size_t> highlightedDot = map.highlightedDot();
        for (size_t i = 0; i < map.dots().size(); ++i) {
          if (i == highlightedDot) continue;  // drawn last, on top
          const Vec2d s = map.dotScreen(i);
          mapDots.push_back({{float(s.x), float(s.y)},
                             float((single ? 5 : 4.5) * uiScale),
                             float(1.5 * uiScale),
                             {dotColor[0], dotColor[1], dotColor[2], 1},
                             single && pulse ? float(std::max(*pulse, 1e-3)) : 0.0f});
        }
        if (highlightedDot && *highlightedDot < map.dots().size()) {  // larger, with a thicker ring
          const Vec2d s = map.dotScreen(*highlightedDot);
          mapDots.push_back({{float(s.x), float(s.y)}, float(7 * uiScale), float(2.5 * uiScale),
                             {dotColor[0], dotColor[1], dotColor[2], 1}, 0.0f});
        }
        mapRenderer.draw(windowPx, mapDots);
        // Separator: one dark device pixel along the panel's left edge.
        scissor({panel.x0, 0, panel.x0 + 1, windowPx.y}, windowPx.y);
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        gl::Disable(GL_SCISSOR_TEST);
        overlayQuads.clear();
        if (map.dimmed()) buildMapStatus(map.status(), overlayFont, uiScale, panel, overlayQuads);
        if (tileSource) buildAttribution(tileSource->attribution(), overlayFont, uiScale, panel, overlayQuads);
        overlay.draw(windowPx, overlayQuads);
        animating |= std::any_of(mapDraws.begin(), mapDraws.end(), [](const MapTileDraw& d) { return d.fade < 1; });
      }

      // Stats: drawn as an overlay in fullscreen (no title bar there), else in the title.
      const bool fullscreen = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
      OverlayStats os;
      if (showStats && session.streamer) {
        const auto st = session.streamer->stats();
        os.frameMs = history.median();
        os.fps = os.frameMs > 0 ? 1000.0 / os.frameMs : 0;
        os.p99Ms = history.percentile(99);
        os.jobs = st.pendingJobs;
        os.vramMB = double(st.vramBytes) / (1 << 20);
        os.budgetMB = double(vramBudget >> 20);
        os.photos = session.lib.catalog.order.size();
        os.tiles = draws.size();
        os.tilePx = layout->count() ? layout->place(0).photo.width() * camera.scale : 0;
      }
      if (showStats && fullscreen && session.streamer) {
        overlayQuads.clear();
        buildOverlay(os, history, overlayFont, uiScale, overlayQuads, config.statsGraph);
        overlay.draw(windowPx, overlayQuads);
      }

      // Photo info (F2): only once a photo is fully open in whole-photo view.
      if (const auto f = ui.focused(); showInfo && f && ui.focusProgress() >= 1 && session.streamer &&
                                       *f < session.lib.catalog.order.size()) {
        const PhotoRecord& rec = session.lib.catalog.photos[session.lib.catalog.order[*f]];
        const PhotoInfo info{rec.relPath, rec.width, rec.height, rec.takenAt, rec.takenAt == rec.mtime,
                             rec.fileSize, rec.gps};
        overlayQuads.clear();
        buildPhotoInfo(info, overlayFont, uiScale, camera.viewport, overlayQuads);
        overlay.draw(windowPx, overlayQuads);
      }

      // Photo effect (number keys): its name in the top-right corner while one is on.
      if (const std::string label = photoEffectLabel(ui.photoEffect()); !label.empty()) {
        overlayQuads.clear();
        buildPhotoEffectLabel(label, overlayFont, uiScale, camera.viewport, overlayQuads);  // top right of the content
        overlay.draw(windowPx, overlayQuads);
      }

      // Playback position of the open item's live content (e.g. a video): a bar with a dot,
      // at the bottom of the window whatever the item's size or the map panel.
      if (const auto p = player ? player->progress() : std::nullopt) {
        overlayQuads.clear();
        const ProgressBar bar =
            buildProgressBar(p->position / p->duration, uiScale, windowPx, overlayFont, overlayQuads);
        overlay.draw(windowPx, overlayQuads);
        const MapDot dot{{float(bar.dot.x), float(bar.dot.y)}, float(bar.dotRadius), 0.0f, {1, 1, 1, 1}};
        mapRenderer.draw(windowPx, std::span(&dot, 1));
      }

      // Map notices (e.g. no internet): a few seconds near the top centre.
      if (now < noticeUntil) {
        overlayQuads.clear();
        buildMessage(notice, overlayFont, uiScale, windowPx, overlayQuads);
        overlay.draw(windowPx, overlayQuads);
      }

      // Help (F1): every key binding, centered on top of everything.
      if (showHelp) {
        overlayQuads.clear();
        buildHelp(overlayFont, uiScale, windowPx, overlayQuads);
        overlay.draw(windowPx, overlayQuads);
      }

      if (GLenum err = gl::GetError(); err != GL_NO_ERROR && !reportedGlError) {
        std::fprintf(stderr, "OpenGL error 0x%x\n", err);
        reportedGlError = true;
      }
      const uint64_t beforeSwap = SDL_GetTicksNS();
      if (bench && bench->done(seconds(now))) {
        std::printf("bench: %zu frames in %.1f s, %dx%d px\n", intervals.count(), seconds(now - benchStart),
                    int(windowPx.x), int(windowPx.y));
        std::printf("bench: frame interval ms p50=%.2f p95=%.2f p99=%.2f max=%.2f, >20ms: %zu\n",
                    intervals.percentile(50), intervals.percentile(95), intervals.percentile(99), intervals.max(),
                    intervals.countAbove(20));
        std::printf("bench: cpu work ms p50=%.2f p99=%.2f max=%.2f | VRAM %.0f MB, jobs pending %zu\n",
                    work.percentile(50), work.percentile(99), work.max(),
                    double(session.streamer->stats().vramBytes) / (1 << 20), session.streamer->stats().pendingJobs);
        if (backgroundGpu.count()) {
          std::printf("bench: background shader GPU ms p50=%.2f p99=%.2f max=%.2f\n", backgroundGpu.percentile(50),
                      backgroundGpu.percentile(99), backgroundGpu.max());
        }
        if (options.snapshot && !saveSnapshot(*options.snapshot, windowPx)) exitCode = 1;
        running = false;
      }
      // With the map shown, also wait for its tiles (but no more than 10 s).
      const bool mapSettled = !map.shown() || now - start > 10'000'000'000ULL ||
                              (!map.animating(seconds(now)) && (!mapTiles || mapTiles->pending() == 0));
      if (!options.bench && options.snapshot && session.streamer && ui.idle() && !animating && mapSettled &&
          session.streamer->stats().pendingRequests == 0 && now - start > 1'500'000'000ULL) {
        if (!saveSnapshot(*options.snapshot, windowPx)) {
          std::fprintf(stderr, "could not write snapshot %s\n", toUtf8(*options.snapshot).c_str());
          exitCode = 1;
        }
        running = false;
      }
      SDL_GL_SwapWindow(window);
      const uint64_t afterSwap = SDL_GetTicksNS();
      if (session.streamer && !reportedFirstFrame) {
        reportedFirstFrame = true;
        std::printf("first full frame after %.0f ms (%zu photos)\n", double(afterSwap - start) * 1e-6,
                    session.lib.catalog.order.size());
      }
      if (lastSwap && (bench || showStats)) {
        intervals.add(double(afterSwap - lastSwap) * 1e-6);
        history.add(double(afterSwap - lastSwap) * 1e-6);
        work.add(double(beforeSwap - now) * 1e-6);
      }
      lastSwap = afterSwap;
      if (bench && double(beforeSwap - now) * 1e-6 <= 20) textures.takeUploadMs();
      if (bench && double(beforeSwap - now) * 1e-6 > 20) {
        const auto st = session.streamer->stats();
        std::printf("slow frame: gl upload %.1f ms, work %.1f ms [pre %.1f clear %.1f stream %.1f draw %.1f], swap %.1f ms, uploads %zu (%.1f MB), pages+ %zu, full %zu\n",
                    textures.takeUploadMs(), double(beforeSwap - now) * 1e-6, double(tPre - now) * 1e-6, double(tBegin - tPre) * 1e-6,
                    double(tStream - tBegin) * 1e-6, double(beforeSwap - tStream) * 1e-6,
                    double(afterSwap - beforeSwap) * 1e-6, st.uploadsThisFrame,
                    double(st.uploadBytesThisFrame) / (1 << 20), st.pagesAddedThisFrame, st.fullUploadsThisFrame);
      }
      if (showStats && !fullscreen && session.streamer && afterSwap - lastTitle > 500'000'000ULL) {
        lastTitle = afterSwap;
        // The title updates twice a second, so it summarizes that half second.
        os.fps = intervals.count() ? 1000.0 / intervals.percentile(50) : 0.0;
        os.p99Ms = intervals.percentile(99);
        SDL_SetWindowTitle(window, statsTitle(os).c_str());
        intervals.reset();
        work.reset();
      }
    }

    *stopBackground = true;
    // Map tile fetches can take a few seconds to give up on a dead network: the
    // window goes away first so quitting never looks frozen.
    SDL_HideWindow(window);
    mapTiles.reset();
    SDL_SetCursor(SDL_GetDefaultCursor());
    if (handCursor) SDL_DestroyCursor(handCursor);
    player.reset();
    session.streamer.reset();
    indexer.reset();
    indexJobs.reset();
    jobs.reset();
  }
  pluginHost.reset();  // stops plugin threads while SDL (audio) is still up
  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}

}  // namespace pb
