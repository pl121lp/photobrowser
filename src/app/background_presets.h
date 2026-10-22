#pragma once

#include <span>
#include <string_view>

namespace pb {

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

}  // namespace pb
