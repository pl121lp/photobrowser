#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pb {

struct Config;

// Background shaders built into the program (shaders/backgrounds/*.frag).
struct BackgroundPreset {
  std::string_view name;
  const char* source;
  bool animated;  // uses uTime: needs continuous redraw
};

std::span<const BackgroundPreset> backgroundPresets();
const BackgroundPreset* findBackgroundPreset(std::string_view name);
// True if `value` (a [background] shader setting) names a built-in preset rather than a file.
bool isPresetName(std::string_view value);
// True if the shader reads uTime (ignoring // comments), i.e. it animates.
bool shaderUsesTime(std::string_view source);

// The configured background as one name: "solid", a preset name, or a shader file path.
std::string backgroundSetting(const Config& config);
// What F6 steps through: "solid", every preset, then `userShader` if it is a file.
std::vector<std::string> backgroundCycle(std::string_view userShader);
// The entry after `current` (wrapping); the first entry if `current` isn't in the cycle.
const std::string& nextBackground(std::span<const std::string> cycle, std::string_view current);

}  // namespace pb
