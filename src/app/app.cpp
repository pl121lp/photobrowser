#include "app/app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include "app/gpu_textures.h"
#include "app/library_index.h"
#include "app/streamer.h"
#include "core/frame_history.h"
#include "core/frame_stats.h"
#include "gfx/gl.h"
#include "gfx/overlay_renderer.h"
#include "gfx/tile_renderer.h"
#include "imaging/jpeg.h"
#include "layout/content.h"
#include "layout/layout.h"
#include "platform/paths.h"
#include "view/camera.h"
#include "view/bench_script.h"
#include "view/interaction.h"

namespace pb {

namespace fs = std::filesystem;

namespace {

constexpr size_t kVramBudget = size_t(640) << 20;
constexpr uint64_t kDiskCacheCap = uint64_t(4) << 30;
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

// Everything that exists once the library is loaded.
struct Session {
  LibraryState lib;
  std::unique_ptr<Streamer> streamer;
};

}  // namespace

int App::run(const Options& options) {
  std::error_code ec;
  const fs::path root = fs::absolute(options.photoDir, ec);
  if (ec || !fs::is_directory(root, ec)) {
    std::fprintf(stderr, "photobrowse: photo folder '%s' does not exist\n", toUtf8(options.photoDir).c_str());
    return 2;
  }
  const fs::path libDir = libraryCacheDir(options.cacheDir ? fs::absolute(*options.cacheDir) : defaultCacheRoot(), root);
  if (options.rebuildCache) fs::remove_all(libDir, ec);
  fs::create_directories(libDir, ec);

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);

  SDL_Window* window = SDL_CreateWindow("PhotoBrowse", 1280, 800,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
  if (!SDL_GL_SetSwapInterval(-1)) SDL_GL_SetSwapInterval(1);
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
    auto jobs = std::make_unique<JobSystem>(streamingWorkerCount());
    std::unique_ptr<JobSystem> indexJobs;  // all cores, only while first-run indexing
    std::unique_ptr<Indexer> indexer;
    GpuTextures textures;
    TileRenderer renderer;
    OverlayRenderer overlay;
    if (!renderer.init() || !overlay.init()) exitCode = 1;
    const FontInfo overlayFont = OverlayRenderer::font();
    FrameHistory history;  // frame intervals for the fullscreen stats overlay
    std::vector<OverlayQuad> overlayQuads;

    std::unique_ptr<Layout> layout = makeLayout(options.layout);
    layout->build(0);
    Camera camera;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    camera.viewport = {double(pw), double(ph)};
    Interaction ui(camera, *layout);

    size_t prewarmPages = 0;
    auto startSession = [&](LibraryState lib) {
      session.lib = std::move(lib);
      layout->build(uint32_t(session.lib.catalog.order.size()));
      ui.layoutChanged();
      ui.resetView();
      const size_t l0Pages = (session.lib.catalog.photos.size() + 16383) / 16384;
      const size_t fullReserve = size_t(Streamer::kFullSlots) * 3840 * 2880 * 4 * 4 / 3;
      const size_t budget = kVramBudget - std::min(kVramBudget / 2, l0Pages * kPageBytes + fullReserve);
      prewarmPages = l0Pages + budget / kPageBytes;
      session.streamer =
          std::make_unique<Streamer>(session.lib.catalog, root, thumbs, *jobs, textures, budget);
      session.streamer->setL0(session.lib.l0);
      session.streamer->setUploadTimeBudget(3.0);
      const std::string title = session.lib.catalog.order.empty()
                                    ? "PhotoBrowse — no photos in " + toUtf8(options.photoDir)
                                    : "PhotoBrowse — " + toUtf8(root.filename());
      SDL_SetWindowTitle(window, title.c_str());
    };

    if (auto warm = loadWarm(libDir)) {
      startSession(std::move(*warm));
      // Pick up folder changes for the next launch; trim the disk cache.
      auto snapshot = std::make_shared<LibraryState>(session.lib);
      jobs->submit(kBackgroundPriority, [root, libDir, snapshot, stop = stopBackground, &thumbs] {
        refreshLibrary(root, libDir, *snapshot, thumbs, stop.get());
        if (!*stop) thumbs.sweep(kDiskCacheCap);
      });
    } else {
      indexJobs = std::make_unique<JobSystem>(defaultWorkerCount());
      indexer = std::make_unique<Indexer>(root, libDir, *indexJobs, thumbs);
      indexer->start();
    }

    const float clear[3] = {0.008f, 0.008f, 0.009f};
    std::vector<TileDraw> draws;
    std::vector<std::pair<uint64_t, TileInstance>> keyed;
    std::vector<TileInstance> batch;
    uint64_t last = SDL_GetTicksNS();
    const uint64_t start = last;
    uint64_t lastTitle = 0;
    bool running = exitCode == 0, reportedGlError = false, animating = true;
    bool showStats = options.stats, prewarming = false, reportedFirstFrame = false;
    FrameStats intervals, work;  // ms; F3 window or the whole --bench run
    std::optional<BenchScript> bench;
    uint64_t lastSwap = 0, benchStart = 0;
    if (options.bench) {
      SDL_SetWindowFullscreenMode(window, nullptr);
      SDL_SetWindowFullscreen(window, true);
    }

    while (running) {
      // --- Input ---------------------------------------------------------------
      // With stats shown, keep rendering at the display rate so the numbers mean something.
      const bool wait = ui.idle() && !animating && !prewarming && !indexer && !options.snapshot && !options.bench &&
                        !showStats &&
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
            ui.setViewport({double(ev.window.data1), double(ev.window.data2)});
            break;
          case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) ui.onPointerDown(px(ev.button.x, ev.button.y), t);
            break;
          case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ev.button.button == SDL_BUTTON_LEFT) ui.onPointerUp(px(ev.button.x, ev.button.y), t);
            break;
          case SDL_EVENT_MOUSE_MOTION: ui.onPointerMove(px(ev.motion.x, ev.motion.y), t); break;
          case SDL_EVENT_MOUSE_WHEEL: {
            const float dy = ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
            ui.onWheel(px(ev.wheel.mouse_x, ev.wheel.mouse_y), dy);
            break;
          }
          case SDL_EVENT_KEY_DOWN: {
            const SDL_Keycode k = ev.key.key;
            if (k == SDLK_Q) {
              running = false;
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
            } else if (auto key = mapKey(k)) {
              if (!ui.onKey(*key, t) && *key == Key::Escape) SDL_SetWindowFullscreen(window, false);
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
      ui.update(seconds(now), dt);

      // --- Draw ------------------------------------------------------------------
      const uint64_t tPre = SDL_GetTicksNS();
      renderer.beginFrame(camera.viewport, clear);
      const uint64_t tBegin = SDL_GetTicksNS();
      uint64_t tStream = tBegin;
      animating = false;
      if (session.streamer) {
        session.streamer->frame(seconds(now), camera, *layout, ui.focused(), draws);
        textures.endFrame();
        tStream = SDL_GetTicksNS();
        keyed.clear();
        std::optional<std::pair<uint64_t, TileInstance>> focusedTile;
        for (const TileDraw& d : draws) {
          const PhotoRecord& rec = session.lib.catalog.photos[d.id];
          const UvRect crop = cropUv(rec.aspect(), 1.0);
          const Rect r = layout->place(d.pos).photo;
          const Vec2d a = camera.worldToScreen({r.x0, r.y0});
          const Vec2d b = camera.worldToScreen({r.x1, r.y1});
          TileInstance ti{};
          setRect(ti.rect, {a.x, a.y, b.x, b.y});
          setUv(ti.uv, compose(d.cur.uv, crop));
          setUv(ti.uvPrev, compose(d.prev.uv, crop));
          ti.fade = d.fade;
          animating |= d.fade < 1;
          const uint64_t key = uint64_t(textures.texture(d.cur)) << 32 | textures.texture(d.prev);
          if (auto f = ui.focused(); f && *f == d.pos) {
            // Whole-photo view: morph from the square crop to the full photo, drawn on top.
            const FocusMorph m = focusMorph({a.x, a.y, b.x, b.y}, rec.aspect(), camera.viewport, ui.focusProgress());
            setRect(ti.rect, m.rect);
            setUv(ti.uv, compose(d.cur.uv, m.uv));
            setUv(ti.uvPrev, compose(d.prev.uv, m.uv));
            ui.setFocusRect(m.rect);
            focusedTile.emplace(key, ti);
            continue;
          }
          if (ui.focused()) ti.dim = float(0.85 * ui.focusProgress());
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
        if (focusedTile) {
          const uint64_t key = focusedTile->first;
          renderer.draw(GLuint(key >> 32), GLuint(key & 0xffffffffu), std::span(&focusedTile->second, 1));
        }
      }

      // Stats: drawn as an overlay in fullscreen (no title bar there), else in the title.
      const bool fullscreen = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
      if (showStats && fullscreen && session.streamer) {
        const auto st = session.streamer->stats();
        OverlayStats os;
        os.frameMs = history.median();
        os.fps = os.frameMs > 0 ? 1000.0 / os.frameMs : 0;
        os.p99Ms = history.percentile(99);
        os.jobs = st.pendingJobs;
        os.vramMB = double(st.vramBytes) / (1 << 20);
        os.budgetMB = double(kVramBudget >> 20);
        os.photos = session.lib.catalog.order.size();
        os.tiles = draws.size();
        os.tilePx = layout->count() ? layout->place(0).photo.width() * camera.scale : 0;
        overlayQuads.clear();
        buildOverlay(os, history, overlayFont, std::max(1.0f, SDL_GetWindowDisplayScale(window)), overlayQuads);
        overlay.draw(camera.viewport, overlayQuads);
      }

      if (GLenum err = gl::GetError(); err != GL_NO_ERROR && !reportedGlError) {
        std::fprintf(stderr, "OpenGL error 0x%x\n", err);
        reportedGlError = true;
      }
      const uint64_t beforeSwap = SDL_GetTicksNS();
      if (bench && bench->done(seconds(now))) {
        std::printf("bench: %zu frames in %.1f s, %dx%d px\n", intervals.count(), seconds(now - benchStart),
                    int(camera.viewport.x), int(camera.viewport.y));
        std::printf("bench: frame interval ms p50=%.2f p95=%.2f p99=%.2f max=%.2f, >20ms: %zu\n",
                    intervals.percentile(50), intervals.percentile(95), intervals.percentile(99), intervals.max(),
                    intervals.countAbove(20));
        std::printf("bench: cpu work ms p50=%.2f p99=%.2f max=%.2f | VRAM %.0f MB, jobs pending %zu\n",
                    work.percentile(50), work.percentile(99), work.max(),
                    double(session.streamer->stats().vramBytes) / (1 << 20), session.streamer->stats().pendingJobs);
        if (options.snapshot && !saveSnapshot(*options.snapshot, camera.viewport)) exitCode = 1;
        running = false;
      }
      if (!options.bench && options.snapshot && session.streamer && ui.idle() && !animating &&
          session.streamer->stats().pendingRequests == 0 && now - start > 1'500'000'000ULL) {
        if (!saveSnapshot(*options.snapshot, camera.viewport)) {
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
        const auto st = session.streamer->stats();
        char title[256];
        std::snprintf(title, sizeof title, "PhotoBrowse — %.0f fps · p99 %.1f ms · jobs %zu · VRAM %.0f/%zu MB",
                      intervals.count() ? 1000.0 / intervals.percentile(50) : 0.0, intervals.percentile(99),
                      st.pendingJobs, double(st.vramBytes) / (1 << 20), kVramBudget >> 20);
        SDL_SetWindowTitle(window, title);
        intervals.reset();
        work.reset();
      }
    }

    *stopBackground = true;
    session.streamer.reset();
    indexer.reset();
    indexJobs.reset();
    jobs.reset();
  }
  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}

}  // namespace pb
