#include <doctest/doctest.h>

#include <string>

#include "app/background_presets.h"
#include "app/config.h"

TEST_CASE("backgrounds: built-in presets, static and animated") {
  for (auto name : {"gradient", "vignette", "grain", "dots", "spotlight"}) {
    const auto* p = pb::findBackgroundPreset(name);
    REQUIRE_MESSAGE(p, name);
    CHECK_FALSE_MESSAGE(p->animated, name);
  }
  for (auto name : {"waves", "aurora", "nebula", "bokeh", "starfield", "warp"}) {
    const auto* p = pb::findBackgroundPreset(name);
    REQUIRE_MESSAGE(p, name);
    CHECK_MESSAGE(p->animated, name);
  }
  CHECK_FALSE(pb::findBackgroundPreset("nope"));
}

TEST_CASE("backgrounds: every preset is a complete fragment shader") {
  for (const auto& p : pb::backgroundPresets()) {
    const std::string src = p.source;
    CHECK_MESSAGE(src.find("#version 410 core") != std::string::npos, p.name);
    CHECK_MESSAGE(src.find("out vec4 fragColor") != std::string::npos, p.name);
  }
}

TEST_CASE("backgrounds: animation is detected from the use of uTime") {
  CHECK(pb::shaderUsesTime("uniform float uTime; void main(){ x = uTime; }"));
  CHECK_FALSE(pb::shaderUsesTime("uniform vec2 uResolution; void main(){}"));
  CHECK_FALSE(pb::shaderUsesTime("// uTime is not used here\nvoid main(){}"));
}

TEST_CASE("backgrounds: the default config lists every preset") {
  const std::string text = pb::defaultConfigText();
  for (const auto& p : pb::backgroundPresets()) CHECK_MESSAGE(text.find(std::string(p.name)) != std::string::npos, p.name);
}

TEST_CASE("backgrounds: a shader setting is a preset name or a file") {
  CHECK(pb::isPresetName("aurora"));
  CHECK_FALSE(pb::isPresetName("my.frag"));
  CHECK_FALSE(pb::isPresetName("/home/me/aurora"));
  CHECK_FALSE(pb::isPresetName("unknown"));
}
