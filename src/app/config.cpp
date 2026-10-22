#include "app/config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>

#include "app/options.h"
#include "platform/paths.h"

namespace pb {

namespace fs = std::filesystem;

namespace {

constexpr double kMaxGap = 0.2;

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return out;
}

std::optional<bool> parseBool(std::string_view v) {
  const std::string l = lower(v);
  if (l == "true" || l == "yes" || l == "on" || l == "1") return true;
  if (l == "false" || l == "no" || l == "off" || l == "0") return false;
  return std::nullopt;
}

std::optional<double> parseNumber(std::string_view v) {
  const std::string s(v);
  char* end = nullptr;
  const double d = std::strtod(s.c_str(), &end);
  if (s.empty() || end != s.c_str() + s.size()) return std::nullopt;
  return d;
}

std::optional<Rgb8> parseColor(std::string_view v) {
  if (v.size() != 7 || v[0] != '#') return std::nullopt;
  uint8_t c[3];
  for (int i = 0; i < 3; ++i) {
    const std::string hex(v.substr(size_t(1 + 2 * i), 2));
    if (!std::isxdigit(static_cast<unsigned char>(hex[0])) || !std::isxdigit(static_cast<unsigned char>(hex[1]))) {
      return std::nullopt;
    }
    c[i] = uint8_t(std::strtoul(hex.c_str(), nullptr, 16));
  }
  return Rgb8{c[0], c[1], c[2]};
}

fs::path expandPath(std::string_view v) {
  if (v.starts_with("~/") || v == "~") {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (home) return fromUtf8(std::string(home) + std::string(v.substr(1)));
  }
  return fromUtf8(std::string(v));
}

}  // namespace

std::array<float, 3> toLinear(Rgb8 c) {
  auto lin = [](uint8_t v) {
    const double x = v / 255.0;
    return float(x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4));
  };
  return {lin(c.r), lin(c.g), lin(c.b)};
}

ConfigResult parseConfig(std::string_view text, std::string_view sourceName) {
  ConfigResult r;
  Config& c = r.config;
  std::string section;
  size_t lineNo = 0;
  while (!text.empty()) {
    const size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    ++lineNo;
    auto warn = [&](const std::string& msg) {
      r.warnings.push_back(std::string(sourceName) + " line " + std::to_string(lineNo) + ": " + msg);
    };
    if (const size_t hash = line.find('#'); hash != std::string_view::npos) {
      // '#' starts a comment unless it begins a colour value ("= #rrggbb").
      const size_t eq = line.find('=');
      const bool colourValue = eq != std::string_view::npos && hash > eq && trim(line.substr(eq + 1, hash - eq - 1)).empty();
      if (!colourValue) {
        line = line.substr(0, hash);
      } else if (const size_t next = line.find('#', hash + 1); next != std::string_view::npos) {
        line = line.substr(0, next);
      }
    }
    line = trim(line);
    if (line.empty()) continue;
    if (line.front() == '[') {
      if (line.back() != ']') {
        warn("malformed section header");
        continue;
      }
      section = lower(trim(line.substr(1, line.size() - 2)));
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      warn("expected 'key = value'");
      continue;
    }
    const std::string key = section + "." + lower(trim(line.substr(0, eq)));
    const std::string_view value = trim(line.substr(eq + 1));
    auto bad = [&](const char* expected) { warn("'" + std::string(value) + "' is not " + expected + " for " + key); };

    if (key == "library.folder") {
      if (value.empty()) {
        bad("a folder");
      } else {
        c.photoFolder = expandPath(value);
      }
    } else if (key == "view.gap") {
      if (auto d = parseNumber(value)) {
        if (*d < 0 || *d > kMaxGap) warn(key + " must be between 0 and 0.2; clamped");
        c.gap = std::clamp(*d, 0.0, kMaxGap);
      } else {
        bad("a number");
      }
    } else if (key == "stats.enabled" || key == "stats.graph") {
      if (auto b = parseBool(value)) {
        (key == "stats.enabled" ? c.stats : c.statsGraph) = *b;
      } else {
        bad("true/false");
      }
    } else if (key == "background.mode") {
      const std::string m = lower(value);
      if (m == "solid") {
        c.background = BackgroundMode::Solid;
      } else if (m == "gradient") {
        c.background = BackgroundMode::Gradient;
      } else if (m == "vignette") {
        c.background = BackgroundMode::Vignette;
      } else if (m == "shader") {
        c.background = BackgroundMode::Shader;
      } else {
        bad("solid, gradient, vignette or shader");
      }
    } else if (key == "background.color" || key == "background.color2") {
      if (auto col = parseColor(value)) {
        (key == "background.color" ? c.backgroundColor : c.backgroundColor2) = *col;
      } else {
        bad("a colour like #1a2b3c");
      }
    } else if (key == "display.gpu") {
      const std::string g = lower(value);
      if (g == "auto") {
        c.gpu = GpuPreference::Auto;
      } else if (g == "integrated") {
        c.gpu = GpuPreference::Integrated;
      } else if (g == "discrete") {
        c.gpu = GpuPreference::Discrete;
      } else {
        bad("auto, integrated or discrete");
      }
    } else if (key == "background.shader") {
      c.backgroundShader = value.empty() ? fs::path{} : expandPath(value);
    } else {
      warn("unknown key '" + key + "'");
    }
  }
  return r;
}

std::string defaultConfigText() {
  return R"(# PhotoBrowse settings. Lines starting with # are comments.
# Remove the # in front of a setting to change it. Command-line options win over
# this file. Problems are reported on the terminal and the default is used.

[library]
# Folder with your photos (searched including subfolders). ~ means your home folder.
# folder = photos

[view]
# Space around each photo, as a fraction of a photo's size (0 to 0.2). 0 = no gaps.
# gap = 0.03

[stats]
# Show the stats display at startup (F3 toggles it). In fullscreen it is an on-screen
# panel, in a window it is shown in the title bar.
# enabled = false
# Add a frame-time graph (last 4 seconds) to the on-screen panel.
# graph = false

[display]
# Which graphics chip draws on laptops with two (e.g. Intel + NVIDIA):
#   auto       : the system's default (usually the power-saving integrated one);
#                launchers such as switcherooctl or prime-run still work
#   integrated : always the integrated GPU (best battery life)
#   discrete   : the dedicated GPU (more headroom: high-refresh monitors, heavy shaders)
# Linux only; on Windows use Settings > System > Display > Graphics.
# gpu = auto

[background]
# mode = solid     : just color
# mode = gradient  : color at the top, color2 at the bottom (same as shader = gradient)
# mode = vignette  : color in the middle, color2 at the edges (same as shader = vignette)
# mode = shader    : a built-in preset or your own shader file, chosen with shader = ...
# mode = solid
# color = #17171a
# color2 = #000000
#
# Built-in presets (all use color and color2):
#   still:    gradient, vignette, grain, dots, spotlight
#   animated: waves, aurora, nebula, bokeh, starfield
# shader = aurora
#
# Or a file: a complete "#version 410 core" fragment shader writing
# "out vec4 fragColor;". It may declare
#   uniform vec2 uResolution;  // drawable size in pixels
#   uniform float uTime;       // seconds since start; shaders using it redraw continuously
#   uniform vec3 uColor;       // color
#   uniform vec3 uColor2;      // color2
# The presets in the source tree's shaders/backgrounds/ folder are good starting points.
# shader = ~/.config/photobrowse/my-background.frag
)";
}

ConfigResult loadConfig(const fs::path& path, bool createIfMissing) {
  if (auto data = readWholeFile(path)) {
    return parseConfig(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()),
                       toUtf8(path.filename()));
  }
  ConfigResult r;
  std::error_code ec;
  if (fs::exists(path, ec)) {
    r.warnings.push_back("could not read " + toUtf8(path) + "; using defaults");
  } else if (createIfMissing) {
    const std::string text = defaultConfigText();
    if (!atomicWriteFile(path, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()))) {
      r.warnings.push_back("could not create " + toUtf8(path));
    }
  } else {
    r.warnings.push_back("config file " + toUtf8(path) + " not found; using defaults");
  }
  return r;
}

fs::path defaultConfigPath() {
#ifdef _WIN32
  if (const char* v = std::getenv("APPDATA"); v && *v) return fromUtf8(v) / "photobrowse" / "config.ini";
#else
  if (const char* v = std::getenv("XDG_CONFIG_HOME"); v && *v) return fs::path(v) / "photobrowse" / "config.ini";
  if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".config" / "photobrowse" / "config.ini";
#endif
  return fs::path("photobrowse") / "config.ini";
}

GpuEnvironment gpuEnvironment(GpuPreference preference, bool nvidiaDriver) {
  GpuEnvironment e;
  switch (preference) {
    case GpuPreference::Auto:
      break;
    case GpuPreference::Discrete:
      if (nvidiaDriver) {
        // NVIDIA PRIME render offload (glvnd picks NVIDIA's GLX/EGL implementation).
        e.set = {{"__NV_PRIME_RENDER_OFFLOAD", "1"},
                 {"__GLX_VENDOR_LIBRARY_NAME", "nvidia"},
                 {"__VK_LAYER_NV_optimus", "NVIDIA_only"}};
      } else {
        e.set = {{"DRI_PRIME", "1"}};  // Mesa: the non-default (discrete) GPU
      }
      break;
    case GpuPreference::Integrated:
      e.set = {{"DRI_PRIME", "0"}};
      e.unset = {"__NV_PRIME_RENDER_OFFLOAD", "__GLX_VENDOR_LIBRARY_NAME", "__VK_LAYER_NV_optimus"};
      break;
  }
  return e;
}

void applyOptions(Config& config, const Options& options) {
  if (options.photoDir) config.photoFolder = *options.photoDir;
  if (options.stats) config.stats = true;
}

}  // namespace pb
