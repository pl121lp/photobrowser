#include <doctest/doctest.h>

#include <algorithm>
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

TEST_CASE("backgrounds: star presets honour the density setting") {
  for (auto name : {"warp", "starfield", "bokeh"}) {
    const std::string src = pb::findBackgroundPreset(name)->source;
    CHECK_MESSAGE(src.find("uniform float uDensity;") != std::string::npos, name);
  }
}

TEST_CASE("backgrounds: the F6 cycle is solid, every preset, then the user's shader file") {
  const auto plain = pb::backgroundCycle("");
  REQUIRE(plain.size() == 1 + pb::backgroundPresets().size());
  CHECK(plain[0] == "solid");
  CHECK(plain[1] == "gradient");
  CHECK(plain[2] == "vignette");
  CHECK(plain.back() == "warp");
  CHECK(pb::backgroundCycle("aurora") == plain);  // a preset is already in the cycle
  const auto withFile = pb::backgroundCycle("/home/me/my.frag");
  CHECK(withFile.size() == plain.size() + 1);
  CHECK(withFile.back() == "/home/me/my.frag");
}

TEST_CASE("backgrounds: next in the cycle wraps back to solid") {
  const auto cycle = pb::backgroundCycle("");
  CHECK(pb::nextBackground(cycle, "solid") == "gradient");
  CHECK(pb::nextBackground(cycle, "gradient") == "vignette");
  CHECK(pb::nextBackground(cycle, "warp") == "solid");
  CHECK(pb::nextBackground(cycle, "unknown") == "solid");
}

TEST_CASE("backgrounds: the starting entry follows the config") {
  pb::Config c;
  CHECK(pb::backgroundSetting(c) == "solid");
  c.background = pb::BackgroundMode::Gradient;
  CHECK(pb::backgroundSetting(c) == "gradient");
  c.background = pb::BackgroundMode::Vignette;
  CHECK(pb::backgroundSetting(c) == "vignette");
  c.background = pb::BackgroundMode::Shader;
  c.backgroundShader = "nebula";
  CHECK(pb::backgroundSetting(c) == "nebula");
  c.backgroundShader = "";
  CHECK(pb::backgroundSetting(c) == "solid");  // shader mode without a shader
}

namespace {
double brightest(pb::Rgb8 c) { return std::max({c.r, c.g, c.b}); }
}  // namespace

TEST_CASE("backgrounds: every preset has its own visible built-in colours") {
  for (const auto& p : pb::backgroundPresets()) {
    CHECK_MESSAGE(std::max(brightest(p.color), brightest(p.color2)) >= 0x28, p.name);  // not lost against black
    CHECK_MESSAGE(p.color != p.color2, p.name);
  }
  CHECK(pb::findBackgroundPreset("aurora")->color != pb::findBackgroundPreset("nebula")->color);
}

TEST_CASE("backgrounds: colours for each entry of the cycle") {
  pb::Config c;
  c.background = pb::BackgroundMode::Shader;
  c.backgroundShader = "warp";
  c.backgroundColor = {8, 8, 12};
  c.backgroundColor2 = {0, 0, 0};
  const auto* aurora = pb::findBackgroundPreset("aurora");
  // The configured background and solid use color/color2 from the config.
  CHECK(pb::backgroundColors(c, "warp") == std::pair{c.backgroundColor, c.backgroundColor2});
  CHECK(pb::backgroundColors(c, "solid").first == c.backgroundColor);
  // Other presets use their built-in colours...
  CHECK(pb::backgroundColors(c, "aurora") == std::pair{aurora->color, aurora->color2});
  // ...unless overridden per preset, one colour at a time.
  c.presetColors["aurora"].color = pb::Rgb8{1, 2, 3};
  CHECK(pb::backgroundColors(c, "aurora") == std::pair{pb::Rgb8{1, 2, 3}, aurora->color2});
  c.presetColors["warp"].color2 = pb::Rgb8{4, 5, 6};
  CHECK(pb::backgroundColors(c, "warp") == std::pair{c.backgroundColor, pb::Rgb8{4, 5, 6}});
  // A shader file uses the config colours.
  c.backgroundShader = "/home/me/my.frag";
  CHECK(pb::backgroundColors(c, "/home/me/my.frag").first == c.backgroundColor);
}

TEST_CASE("config: per-preset background colours") {
  auto r = pb::parseConfig("[background.aurora]\ncolor = #112233\n[background.Warp]\ncolor2 = #010203\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.presetColors.at("aurora").color == pb::Rgb8{0x11, 0x22, 0x33});
  CHECK_FALSE(r.config.presetColors.at("aurora").color2);
  CHECK(r.config.presetColors.at("warp").color2 == pb::Rgb8{1, 2, 3});
  auto bad = pb::parseConfig("[background.plasma]\ncolor = #112233\n[background.aurora]\ncolor = red\nspeed = 2\n");
  CHECK(bad.warnings.size() == 3);
  CHECK(bad.config.presetColors.count("plasma") == 0);
  CHECK_FALSE(bad.config.presetColors["aurora"].color);
  CHECK(pb::defaultConfigText().find("[background.aurora]") != std::string::npos);
}
