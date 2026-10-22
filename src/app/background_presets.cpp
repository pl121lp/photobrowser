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
        {"gradient", kShader_gradient_frag, false, {0x2c, 0x3e, 0x5c}, {0x0a, 0x0d, 0x14}},
        {"vignette", kShader_vignette_frag, false, {0x3a, 0x3a, 0x42}, {0x08, 0x08, 0x0a}},
        {"grain", kShader_grain_frag, false, {0x34, 0x2e, 0x28}, {0x0c, 0x0b, 0x0a}},
        {"dots", kShader_dots_frag, false, {0x1a, 0x1a, 0x20}, {0x5a, 0x5a, 0x70}},
        {"spotlight", kShader_spotlight_frag, false, {0x46, 0x42, 0x3c}, {0x0b, 0x0b, 0x0c}},
        {"waves", kShader_waves_frag, false, {0x22, 0x40, 0x66}, {0x0b, 0x10, 0x20}},
        {"aurora", kShader_aurora_frag, false, {0x3f, 0xd6, 0xa0}, {0x0b, 0x10, 0x20}},
        {"nebula", kShader_nebula_frag, false, {0x6a, 0x34, 0x96}, {0x07, 0x08, 0x12}},
        {"bokeh", kShader_bokeh_frag, false, {0xff, 0xcf, 0x8a}, {0x14, 0x10, 0x1c}},
        {"starfield", kShader_starfield_frag, false, {0xff, 0xff, 0xff}, {0x04, 0x05, 0x0a}},
        {"warp", kShader_warp_frag, false, {0x9f, 0xc4, 0xff}, {0x00, 0x00, 0x00}},
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

std::pair<Rgb8, Rgb8> backgroundColors(const Config& config, std::string_view name) {
  std::pair<Rgb8, Rgb8> c{config.backgroundColor, config.backgroundColor2};
  const BackgroundPreset* preset = findBackgroundPreset(name);
  if (preset && name != backgroundSetting(config)) c = {preset->color, preset->color2};
  if (auto it = config.presetColors.find(std::string(name)); it != config.presetColors.end()) {
    if (it->second.color) c.first = *it->second.color;
    if (it->second.color2) c.second = *it->second.color2;
  }
  return c;
}

const std::string& nextBackground(std::span<const std::string> cycle, std::string_view current) {
  for (size_t i = 0; i < cycle.size(); ++i) {
    if (cycle[i] == current) return cycle[(i + 1) % cycle.size()];
  }
  return cycle.front();
}

}  // namespace pb
