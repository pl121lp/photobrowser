#include "app/app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include "gfx/gl.h"
#include "imaging/jpeg.h"
#include "platform/paths.h"
#include "gfx/paged_texture.h"
#include "gfx/tile_renderer.h"
#include "layout/layout.h"
#include "library/scanner.h"
#include "view/camera.h"
#include "view/interaction.h"

namespace pb {

namespace {

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
bool saveSnapshot(const std::filesystem::path& path, Vec2d viewport) {
  Image img{uint32_t(viewport.x), uint32_t(viewport.y), {}};
  img.rgba.resize(size_t(img.w) * img.h * 4);
  gl::PixelStorei(GL_PACK_ALIGNMENT, 4);
  gl::ReadPixels(0, 0, GLsizei(img.w), GLsizei(img.h), GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
  // GL rows are bottom-up.
  const size_t row = size_t(img.w) * 4;
  for (uint32_t y = 0; y < img.h / 2; ++y) {
    std::swap_ranges(img.rgba.begin() + long(y * row), img.rgba.begin() + long((y + 1) * row),
                     img.rgba.begin() + long((img.h - 1 - y) * row));
  }
  auto jpg = encodeJpeg(img, 92);
  return jpg && atomicWriteFile(path, *jpg);
}

}  // namespace

int App::run(const Options& options) {
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
    TileRenderer renderer;
    if (!renderer.init()) {
      exitCode = 1;
    } else {
      const auto scan = scanFolder(options.photoDir);
      std::unique_ptr<Layout> layout = makeLayout(options.layout);
      layout->build(uint32_t(scan.size()));

      Camera camera;
      int pw = 0, ph = 0;
      SDL_GetWindowSizeInPixels(window, &pw, &ph);
      camera.viewport = {double(pw), double(ph)};
      Interaction ui(camera, *layout);

      const Image grey{1, 1, {60, 60, 60, 255}};
      const GLuint placeholder = createImageTexture(grey, false);
      const float clear[3] = {0.008f, 0.008f, 0.009f};

      std::vector<uint32_t> visible;
      std::vector<TileInstance> instances;
      uint64_t last = SDL_GetTicksNS();
      const uint64_t start = last;
      bool running = true, reportedGlError = false;
      while (running) {
        SDL_Event ev;
        const float density = SDL_GetWindowPixelDensity(window);
        auto px = [&](float x, float y) { return Vec2d{double(x) * density, double(y) * density}; };
        const bool wait = ui.idle() && !options.snapshot;
        bool have = wait ? SDL_WaitEventTimeout(&ev, 250) : SDL_PollEvent(&ev);
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

        const uint64_t now = SDL_GetTicksNS();
        const double dt = std::min(seconds(now - last), 0.1);
        last = now;
        ui.update(seconds(now), dt);

        visible.clear();
        layout->visible(camera.visibleWorld(), visible);
        instances.clear();
        for (uint32_t pos : visible) {
          const Rect r = layout->place(pos).photo;
          const Vec2d a = camera.worldToScreen({r.x0, r.y0});
          const Vec2d b = camera.worldToScreen({r.x1, r.y1});
          TileInstance ti{{float(a.x), float(a.y), float(b.x), float(b.y)}, {0, 0, 1, 1}, {0, 0, 1, 1}, 1, 0};
          if (auto f = ui.focused(); f && *f != pos) ti.dim = float(0.85 * ui.focusProgress());
          instances.push_back(ti);
        }
        renderer.beginFrame(camera.viewport, clear);
        renderer.draw(placeholder, placeholder, instances);
        if (options.snapshot && ui.idle() && now - start > 1'500'000'000ULL) {
          if (!saveSnapshot(*options.snapshot, camera.viewport)) {
            std::fprintf(stderr, "could not write snapshot %s\n", toUtf8(*options.snapshot).c_str());
            exitCode = 1;
          }
          running = false;
        }
        if (GLenum err = gl::GetError(); err != GL_NO_ERROR && !reportedGlError) {
          std::fprintf(stderr, "OpenGL error 0x%x\n", err);
          reportedGlError = true;
        }
        SDL_GL_SwapWindow(window);
      }
      gl::DeleteTextures(1, &placeholder);
    }
  }
  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}

}  // namespace pb
