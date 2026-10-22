#include <doctest/doctest.h>

#include "app/config.h"
#include "app/options.h"
#include "test_util.h"

using pb::BackgroundMode;

namespace {
bool hasWarning(const pb::ConfigResult& r, std::string_view part) {
  for (const auto& w : r.warnings) {
    if (w.find(part) != std::string::npos) return true;
  }
  return false;
}
}  // namespace

TEST_CASE("config: defaults") {
  pb::Config c;
  CHECK(c.photoFolder == "photos");
  CHECK(c.gap == doctest::Approx(0.03));
  CHECK_FALSE(c.stats);
  CHECK_FALSE(c.statsGraph);
  CHECK(c.background == BackgroundMode::Solid);
  CHECK(c.backgroundColor == pb::Rgb8{0x17, 0x17, 0x1a});
}

TEST_CASE("config: values, sections, comments") {
  auto r = pb::parseConfig(R"(
# a comment
[library]
folder = /data/My Photos   # trailing comment

[view]
gap = 0

[stats]
enabled = yes
graph = true

[background]
mode = gradient
color = #102030
color2 = #000000
shader = ~/bg.frag
)");
  CHECK(r.warnings.empty());
  CHECK(r.config.photoFolder == "/data/My Photos");
  CHECK(r.config.gap == 0);
  CHECK(r.config.stats);
  CHECK(r.config.statsGraph);
  CHECK(r.config.background == BackgroundMode::Gradient);
  CHECK(r.config.backgroundColor == pb::Rgb8{0x10, 0x20, 0x30});
  CHECK(r.config.backgroundColor2 == pb::Rgb8{0, 0, 0});
  CHECK(r.config.backgroundShader.filename() == "bg.frag");
  CHECK(r.config.backgroundShader.string().front() != '~');  // home expanded
}

TEST_CASE("config: mistakes warn with the line number and keep the default") {
  auto r = pb::parseConfig("[view]\ngap = lots\n[stats]\nenabled = maybe\nfoo = 1\n[background]\ncolor = red\nmode = plasma\n"
                           "this line has no equals\n");
  CHECK(r.config.gap == doctest::Approx(0.03));
  CHECK_FALSE(r.config.stats);
  CHECK(r.config.background == BackgroundMode::Solid);
  CHECK(hasWarning(r, "line 2"));
  CHECK(hasWarning(r, "line 4"));
  CHECK(hasWarning(r, "unknown key 'stats.foo'"));
  CHECK(hasWarning(r, "line 7"));
  CHECK(hasWarning(r, "line 8"));
  CHECK(hasWarning(r, "line 9"));
}

TEST_CASE("config: gap is clamped") {
  auto r = pb::parseConfig("[view]\ngap = 0.9\n");
  CHECK(r.config.gap == doctest::Approx(0.2));
  CHECK(hasWarning(r, "line 2"));
  CHECK(pb::parseConfig("[view]\ngap = -1\n").config.gap == 0);
}

TEST_CASE("config: the generated default file documents every option and parses to the defaults") {
  const std::string text = pb::defaultConfigText();
  for (auto key : {"folder", "gap", "enabled", "graph", "mode", "color", "color2", "shader"}) {
    CHECK(text.find(key) != std::string::npos);
  }
  auto r = pb::parseConfig(text);
  CHECK(r.warnings.empty());
  CHECK(r.config == pb::Config{});
}

TEST_CASE("config: load writes the default file when missing") {
  pbtest::TempDir dir;
  const auto path = dir.path / "sub" / "config.ini";
  auto r = pb::loadConfig(path, true);
  CHECK(r.warnings.empty());
  CHECK(std::filesystem::exists(path));
  pbtest::writeFile(path, "[view]\ngap = 0.1\n");
  CHECK(pb::loadConfig(path, true).config.gap == doctest::Approx(0.1));
  auto missing = pb::loadConfig(dir.path / "nope.ini", false);  // explicit --config that doesn't exist
  CHECK(hasWarning(missing, "nope.ini"));
  CHECK_FALSE(std::filesystem::exists(dir.path / "nope.ini"));
}

TEST_CASE("config: command line overrides the file") {
  pb::Config c;
  c.photoFolder = "/from/config";
  c.stats = false;
  pb::Options o;
  pb::applyOptions(c, o);
  CHECK(c.photoFolder == "/from/config");  // no folder on the command line
  o.photoDir = "/from/cli";
  o.stats = true;
  pb::applyOptions(c, o);
  CHECK(c.photoFolder == "/from/cli");
  CHECK(c.stats);
}

TEST_CASE("config: default location") {
  const auto p = pb::defaultConfigPath();
  CHECK(p.filename() == "config.ini");
  CHECK(p.parent_path().filename() == "photobrowse");
}

TEST_CASE("config: sRGB colours convert to linear for rendering") {
  CHECK(pb::toLinear({0, 0, 0})[0] == 0);
  CHECK(pb::toLinear({255, 255, 255})[1] == doctest::Approx(1));
  CHECK(pb::toLinear({128, 0, 0})[0] == doctest::Approx(0.2158).epsilon(0.01));
}
