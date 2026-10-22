#include "app/background_presets.h"

#include <array>

#include "app/config.h"
#include "background_shaders.h"
#include "platform/paths.h"

namespace pb {

bool shaderUsesTime(std::string_view source) {
  while (!source.empty()) {
    const size_t nl = source.find('\n');
    std::string_view line = source.substr(0, nl);
    if (const size_t c = line.find("//"); c != std::string_view::npos) line = line.substr(0, c);
    if (line.find("uTime") != std::string_view::npos) return true;
    source = nl == std::string_view::npos ? std::string_view{} : source.substr(nl + 1);
  }
  return false;
}

std::span<const BackgroundPreset> backgroundPresets() {
  static const std::array presets = [] {
    std::array<BackgroundPreset, 11> p = {{
        {"gradient", kShader_gradient_frag, false},
        {"vignette", kShader_vignette_frag, false},
        {"grain", kShader_grain_frag, false},
        {"dots", kShader_dots_frag, false},
        {"spotlight", kShader_spotlight_frag, false},
        {"waves", kShader_waves_frag, false},
        {"aurora", kShader_aurora_frag, false},
        {"nebula", kShader_nebula_frag, false},
        {"bokeh", kShader_bokeh_frag, false},
        {"starfield", kShader_starfield_frag, false},
        {"warp", kShader_warp_frag, false},
    }};
    for (auto& preset : p) preset.animated = shaderUsesTime(preset.source);
    return p;
  }();
  return presets;
}

const BackgroundPreset* findBackgroundPreset(std::string_view name) {
  for (const auto& p : backgroundPresets()) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

bool isPresetName(std::string_view value) { return findBackgroundPreset(value) != nullptr; }

std::string backgroundSetting(const Config& config) {
  switch (config.background) {
    case BackgroundMode::Solid: return "solid";
    case BackgroundMode::Gradient: return "gradient";
    case BackgroundMode::Vignette: return "vignette";
    case BackgroundMode::Shader: break;
  }
  return config.backgroundShader.empty() ? "solid" : toUtf8(config.backgroundShader);
}

std::vector<std::string> backgroundCycle(std::string_view userShader) {
  std::vector<std::string> cycle{"solid"};
  for (const auto& p : backgroundPresets()) cycle.emplace_back(p.name);
  if (!userShader.empty() && !isPresetName(userShader)) cycle.emplace_back(userShader);
  return cycle;
}

const std::string& nextBackground(std::span<const std::string> cycle, std::string_view current) {
  for (size_t i = 0; i < cycle.size(); ++i) {
    if (cycle[i] == current) return cycle[(i + 1) % cycle.size()];
  }
  return cycle.front();
}

}  // namespace pb
