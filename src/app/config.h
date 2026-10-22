#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pb {

struct Options;

struct Rgb8 {  // sRGB colour as written in the config ("#rrggbb")
  uint8_t r = 0, g = 0, b = 0;
  friend bool operator==(Rgb8, Rgb8) = default;
};

// sRGB -> linear (the framebuffer converts back), as r, g, b in 0..1.
std::array<float, 3> toLinear(Rgb8 c);

enum class BackgroundMode { Solid, Gradient, Vignette, Shader };

// Which GPU renders on hybrid laptops. Auto leaves the system's choice (and any
// launcher such as switcherooctl / prime-run) alone.
enum class GpuPreference { Auto, Integrated, Discrete };

// Environment variables that select the GPU for OpenGL (Linux; glvnd + Mesa/NVIDIA).
// Applied before SDL starts. `nvidiaDriver`: the NVIDIA driver is loaded.
struct GpuEnvironment {
  std::vector<std::pair<std::string, std::string>> set;
  std::vector<std::string> unset;
};
GpuEnvironment gpuEnvironment(GpuPreference preference, bool nvidiaDriver);

// User settings from config.ini (see defaultConfigText() for documentation).
struct Config {
  std::filesystem::path photoFolder = "photos";
  double gap = 0.03;  // per side, as a fraction of a photo; 0 = photos touch
  double maxPhotoZoom = 4.0;  // maximum enlargement in whole-photo view (1..16)
  bool stats = false;
  bool statsGraph = false;
  bool fullscreen = true;  // start in borderless desktop fullscreen
  BackgroundMode background = BackgroundMode::Solid;
  Rgb8 backgroundColor{0x17, 0x17, 0x1a};
  Rgb8 backgroundColor2{0x00, 0x00, 0x00};
  std::filesystem::path backgroundShader;
  double backgroundSpeed = 1.0;    // time scale for animated backgrounds (0..10)
  double backgroundDensity = 1.0;  // star/light count multiplier (0.1..4): warp, starfield, bokeh
  GpuPreference gpu = GpuPreference::Auto;
  bool screensaver = false;      // start in screensaver mode (F5 toggles)
  double screensaverHold = 30;   // seconds on each photo (1..3600)
  friend bool operator==(const Config&, const Config&) = default;
};

struct ConfigResult {
  Config config;
  std::vector<std::string> warnings;  // problems found; affected settings keep their defaults
};

ConfigResult parseConfig(std::string_view text, std::string_view sourceName = "config");
// Reads `path`. If it doesn't exist: writes the commented default file there when
// `createIfMissing` (the default location), otherwise warns (an explicit --config).
ConfigResult loadConfig(const std::filesystem::path& path, bool createIfMissing);
std::string defaultConfigText();
// %APPDATA%\photobrowse\config.ini, else $XDG_CONFIG_HOME or ~/.config /photobrowse/config.ini
std::filesystem::path defaultConfigPath();
// Command-line options win over the file.
void applyOptions(Config& config, const Options& options);

}  // namespace pb
