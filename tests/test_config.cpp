#include <doctest/doctest.h>

#include <algorithm>

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

TEST_CASE("config: focused photo zoom limit is configurable and validated") {
  CHECK(pb::Config{}.maxPhotoZoom == doctest::Approx(4.0));
  auto r = pb::parseConfig("[view]\nmax_photo_zoom = 6.5\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.maxPhotoZoom == doctest::Approx(6.5));

  auto low = pb::parseConfig("[view]\nmax_photo_zoom = 0.5\n");
  CHECK(low.config.maxPhotoZoom == doctest::Approx(1.0));
  CHECK(hasWarning(low, "line 2"));
  auto high = pb::parseConfig("[view]\nmax_photo_zoom = 50\n");
  CHECK(high.config.maxPhotoZoom == doctest::Approx(16.0));
  CHECK(hasWarning(high, "line 2"));
  auto bad = pb::parseConfig("[view]\nmax_photo_zoom = much\n");
  CHECK(bad.config.maxPhotoZoom == doctest::Approx(4.0));
  CHECK(hasWarning(bad, "line 2"));
}

TEST_CASE("config: the generated default file documents every option and parses to the defaults") {
  const std::string text = pb::defaultConfigText();
  for (auto key : {"folder", "gap", "max_photo_zoom", "enabled", "graph", "mode", "color", "color2", "shader"}) {
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

TEST_CASE("config: gpu preference") {
  CHECK(pb::Config{}.gpu == pb::GpuPreference::Auto);
  CHECK(pb::parseConfig("[display]\ngpu = discrete\n").config.gpu == pb::GpuPreference::Discrete);
  CHECK(pb::parseConfig("[display]\ngpu = integrated\n").config.gpu == pb::GpuPreference::Integrated);
  auto bad = pb::parseConfig("[display]\ngpu = fastest\n");
  CHECK(bad.config.gpu == pb::GpuPreference::Auto);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(pb::defaultConfigText().find("gpu = auto") != std::string::npos);
}

TEST_CASE("gpu: environment for each preference") {
  using pb::GpuPreference;
  auto has = [](const pb::GpuEnvironment& e, const char* name, const char* value) {
    for (const auto& [n, v] : e.set) {
      if (n == name && v == value) return true;
    }
    return false;
  };
  auto clears = [](const pb::GpuEnvironment& e, const char* name) {
    return std::find(e.unset.begin(), e.unset.end(), name) != e.unset.end();
  };
  const auto autoEnv = pb::gpuEnvironment(GpuPreference::Auto, true);
  CHECK(autoEnv.set.empty());
  CHECK(autoEnv.unset.empty());

  const auto nv = pb::gpuEnvironment(GpuPreference::Discrete, true);
  CHECK(has(nv, "__NV_PRIME_RENDER_OFFLOAD", "1"));
  CHECK(has(nv, "__GLX_VENDOR_LIBRARY_NAME", "nvidia"));
  CHECK(has(nv, "__VK_LAYER_NV_optimus", "NVIDIA_only"));

  const auto other = pb::gpuEnvironment(GpuPreference::Discrete, false);  // e.g. AMD + Intel
  CHECK(has(other, "DRI_PRIME", "1"));
  CHECK_FALSE(has(other, "__GLX_VENDOR_LIBRARY_NAME", "nvidia"));  // would break GLX without NVIDIA

  const auto igpu = pb::gpuEnvironment(GpuPreference::Integrated, true);
  CHECK(has(igpu, "DRI_PRIME", "0"));
  CHECK(clears(igpu, "__NV_PRIME_RENDER_OFFLOAD"));
  CHECK(clears(igpu, "__GLX_VENDOR_LIBRARY_NAME"));
}

TEST_CASE("config: background speed and density") {
  CHECK(pb::Config{}.backgroundSpeed == 1.0);
  CHECK(pb::Config{}.backgroundDensity == 1.0);
  auto r = pb::parseConfig("[background]\nspeed = 2.5\ndensity = 0.5\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.backgroundSpeed == doctest::Approx(2.5));
  CHECK(r.config.backgroundDensity == doctest::Approx(0.5));
  auto clamped = pb::parseConfig("[background]\nspeed = 50\ndensity = 0\n");
  CHECK(clamped.config.backgroundSpeed == doctest::Approx(10));
  CHECK(clamped.config.backgroundDensity == doctest::Approx(0.1));
  CHECK(hasWarning(clamped, "line 2"));
  CHECK(hasWarning(clamped, "line 3"));
  auto bad = pb::parseConfig("[background]\nspeed = fast\n");
  CHECK(bad.config.backgroundSpeed == 1.0);
  CHECK(hasWarning(bad, "line 2"));
  const std::string text = pb::defaultConfigText();
  CHECK(text.find("speed = 1") != std::string::npos);
  CHECK(text.find("density = 1") != std::string::npos);
}
