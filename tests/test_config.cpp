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
  CHECK(c.fullscreen);
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
  for (auto key : {"folder", "gap", "max_photo_zoom", "enabled", "graph", "fullscreen", "mode", "color", "color2", "shader",
                   "[map]", "width", "photo_zoom", "open", "[plugin.maptiles]"}) {
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
  CHECK(c.fullscreen);
  o.windowed = true;
  pb::applyOptions(c, o);
  CHECK_FALSE(c.fullscreen);
}

TEST_CASE("config: display fullscreen") {
  CHECK_FALSE(pb::parseConfig("[display]\nfullscreen = false\n").config.fullscreen);
  CHECK(pb::parseConfig("[display]\nfullscreen = yes\n").config.fullscreen);
  auto bad = pb::parseConfig("[display]\nfullscreen = sometimes\n");
  CHECK(bad.config.fullscreen);
  CHECK(hasWarning(bad, "fullscreen"));
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

TEST_CASE("config: screensaver") {
  CHECK_FALSE(pb::Config{}.screensaver);
  CHECK(pb::Config{}.screensaverHold == doctest::Approx(30));
  auto r = pb::parseConfig("[screensaver]\nenabled = on\nhold = 12.5\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.screensaver);
  CHECK(r.config.screensaverHold == doctest::Approx(12.5));
  auto clamped = pb::parseConfig("[screensaver]\nhold = 0\n");
  CHECK(clamped.config.screensaverHold == doctest::Approx(1));
  CHECK(hasWarning(clamped, "line 2"));
  auto bad = pb::parseConfig("[screensaver]\nenabled = perhaps\nhold = long\n");
  CHECK_FALSE(bad.config.screensaver);
  CHECK(bad.config.screensaverHold == doctest::Approx(30));
  CHECK(hasWarning(bad, "line 2"));
  CHECK(hasWarning(bad, "line 3"));
  const std::string text = pb::defaultConfigText();
  CHECK(text.find("[screensaver]") != std::string::npos);
  CHECK(text.find("hold = 30") != std::string::npos);
}

TEST_CASE("config: screensaver slow zoom and photo only") {
  CHECK(pb::Config{}.screensaverSlowZoom == doctest::Approx(1));
  CHECK_FALSE(pb::Config{}.screensaverPhotoOnly);
  auto r = pb::parseConfig("[screensaver]\nslow_zoom = 1.25\nphoto_only = on\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.screensaverSlowZoom == doctest::Approx(1.25));
  CHECK(r.config.screensaverPhotoOnly);
  auto clamped = pb::parseConfig("[screensaver]\nslow_zoom = 5\n");
  CHECK(clamped.config.screensaverSlowZoom == doctest::Approx(2));
  CHECK(hasWarning(clamped, "line 2"));
  auto bad = pb::parseConfig("[screensaver]\nslow_zoom = lots\nphoto_only = maybe\n");
  CHECK(bad.config.screensaverSlowZoom == doctest::Approx(1));
  CHECK_FALSE(bad.config.screensaverPhotoOnly);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(hasWarning(bad, "line 3"));
  const std::string text = pb::defaultConfigText();
  CHECK(text.find("# slow_zoom = 1") != std::string::npos);
  CHECK(text.find("# photo_only = off") != std::string::npos);
  CHECK(pb::parseConfig(text).warnings.empty());
}

TEST_CASE("config: corner radius") {
  CHECK(pb::Config{}.cornerRadius == 0);
  auto r = pb::parseConfig("[view]\ncorner_radius = 0.06\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.cornerRadius == doctest::Approx(0.06));
  auto clamped = pb::parseConfig("[view]\ncorner_radius = 0.9\n");
  CHECK(clamped.config.cornerRadius == doctest::Approx(0.5));
  CHECK(hasWarning(clamped, "line 2"));
  CHECK(pb::parseConfig("[view]\ncorner_radius = -1\n").config.cornerRadius == 0);
  auto bad = pb::parseConfig("[view]\ncorner_radius = round\n");
  CHECK(bad.config.cornerRadius == 0);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(pb::defaultConfigText().find("# corner_radius = 0\n") != std::string::npos);
}

TEST_CASE("config: fit or letterbox") {
  CHECK_FALSE(pb::Config{}.letterbox);
  CHECK(pb::parseConfig("[view]\nfit = letterbox\n").config.letterbox);
  auto r = pb::parseConfig("[view]\nfit = fit\n");
  CHECK(r.warnings.empty());
  CHECK_FALSE(r.config.letterbox);
  auto bad = pb::parseConfig("[view]\nfit = stretch\n");
  CHECK_FALSE(bad.config.letterbox);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(pb::defaultConfigText().find("# fit = fit\n") != std::string::npos);
}

TEST_CASE("config: sort order and start position") {
  CHECK_FALSE(pb::Config{}.newestFirst);
  CHECK(pb::Config{}.startAt == pb::StartAt::Center);
  auto r = pb::parseConfig("[library]\nsort = newest_first\n[view]\nstart_at = random\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.newestFirst);
  CHECK(r.config.startAt == pb::StartAt::Random);
  CHECK(pb::parseConfig("[view]\nstart_at = oldest\n").config.startAt == pb::StartAt::Oldest);
  CHECK(pb::parseConfig("[view]\nstart_at = newest\n").config.startAt == pb::StartAt::Newest);
  CHECK_FALSE(pb::parseConfig("[library]\nsort = oldest_first\n").config.newestFirst);
  auto bad = pb::parseConfig("[library]\nsort = by_name\n[view]\nstart_at = middle\n");
  CHECK_FALSE(bad.config.newestFirst);
  CHECK(bad.config.startAt == pb::StartAt::Center);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(hasWarning(bad, "line 4"));
  const std::string text = pb::defaultConfigText();
  CHECK(text.find("# sort = oldest_first\n") != std::string::npos);
  CHECK(text.find("# start_at = center\n") != std::string::npos);
}

TEST_CASE("config: start zoom") {
  CHECK(pb::Config{}.startZoom == pb::StartZoom::Focus);
  CHECK(pb::parseConfig("[view]\nstart_focus = photo\n").config.startZoom == pb::StartZoom::Photo);
  auto r = pb::parseConfig("[view]\nstart_focus = full\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.startZoom == pb::StartZoom::Full);
  CHECK(pb::parseConfig("[view]\nstart_focus = focus\n").config.startZoom == pb::StartZoom::Focus);
  auto bad = pb::parseConfig("[view]\nstart_focus = huge\n");
  CHECK(bad.config.startZoom == pb::StartZoom::Focus);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(pb::defaultConfigText().find("# start_focus = focus\n") != std::string::npos);
}

TEST_CASE("config: recursive library scan") {
  CHECK_FALSE(pb::Config{}.recursive);
  auto r = pb::parseConfig("[library]\nrecursive = on\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.recursive);
  auto bad = pb::parseConfig("[library]\nrecursive = deep\n");
  CHECK_FALSE(bad.config.recursive);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(pb::defaultConfigText().find("# recursive = off\n") != std::string::npos);
}

TEST_CASE("config: letterbox bars black or transparent") {
  CHECK_FALSE(pb::Config{}.transparentBars);
  auto r = pb::parseConfig("[view]\nletterbox_bars = transparent\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.transparentBars);
  CHECK_FALSE(pb::parseConfig("[view]\nletterbox_bars = black\n").config.transparentBars);
  auto bad = pb::parseConfig("[view]\nletterbox_bars = pink\n");
  CHECK_FALSE(bad.config.transparentBars);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(pb::defaultConfigText().find("# letterbox_bars = black\n") != std::string::npos);
}

TEST_CASE("config: performance settings default to the built-in values") {
  const pb::Config c;
  CHECK(c.gpuMemoryMB == 640);
  CHECK(c.diskCacheGB == doctest::Approx(4));
  CHECK(c.cacheDir.empty());
  CHECK(c.decodeThreads == 0);  // auto
  CHECK(c.vsync);
}

TEST_CASE("config: performance settings parse, clamp and report mistakes") {
  auto r = pb::parseConfig(
      "[performance]\ngpu_memory = 1024\ndisk_cache = 0.5\ncache_dir = /tmp/pbc\ndecode_threads = 6\nvsync = off\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.gpuMemoryMB == 1024);
  CHECK(r.config.diskCacheGB == doctest::Approx(0.5));
  CHECK(r.config.cacheDir == std::filesystem::path("/tmp/pbc"));
  CHECK(r.config.decodeThreads == 6);
  CHECK_FALSE(r.config.vsync);
  CHECK(pb::parseConfig("[performance]\ndecode_threads = auto\n").config.decodeThreads == 0);

  auto clamped = pb::parseConfig("[performance]\ngpu_memory = 10\ndisk_cache = 0\ndecode_threads = 500\n");
  CHECK(clamped.config.gpuMemoryMB == 256);
  CHECK(clamped.config.diskCacheGB == doctest::Approx(0.25));
  CHECK(clamped.config.decodeThreads == 32);
  CHECK(hasWarning(clamped, "line 2"));
  CHECK(hasWarning(clamped, "line 3"));
  CHECK(hasWarning(clamped, "line 4"));

  auto bad = pb::parseConfig("[performance]\ngpu_memory = lots\ndecode_threads = many\nvsync = sometimes\n");
  CHECK(bad.config.gpuMemoryMB == 640);
  CHECK(bad.config.decodeThreads == 0);
  CHECK(bad.config.vsync);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(hasWarning(bad, "line 3"));
  CHECK(hasWarning(bad, "line 4"));

  const std::string text = pb::defaultConfigText();
  for (auto line : {"# gpu_memory = 640\n", "# disk_cache = 4\n", "# cache_dir =\n", "# decode_threads = auto\n",
                    "# vsync = on\n"}) {
    CHECK(text.find(line) != std::string::npos);
  }
}

TEST_CASE("config: --cache-dir wins over cache_dir") {
  pb::Config c = pb::parseConfig("[performance]\ncache_dir = /from/config\n").config;
  pb::Options o;
  pb::applyOptions(c, o);
  CHECK(c.cacheDir == std::filesystem::path("/from/config"));
  o.cacheDir = "/from/cli";
  pb::applyOptions(c, o);
  CHECK(c.cacheDir == std::filesystem::path("/from/cli"));
}

TEST_CASE("config: autoplay") {
  CHECK_FALSE(pb::Config{}.autoplay);
  auto r = pb::parseConfig("[view]\nautoplay = on\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.autoplay);
  auto bad = pb::parseConfig("[view]\nautoplay = sometimes\n");
  CHECK_FALSE(bad.config.autoplay);
  CHECK(hasWarning(bad, "line 2"));
  CHECK(pb::defaultConfigText().find("# autoplay = off\n") != std::string::npos);
}

TEST_CASE("config: map settings") {
  pb::Config d;
  CHECK(d.mapWidth == 0.4);
  CHECK(d.mapPhotoZoom == 15);
  CHECK_FALSE(d.mapOpen);
  auto r = pb::parseConfig("[map]\nwidth = 0.5\nphoto_zoom = 12\nopen = on\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.mapWidth == 0.5);
  CHECK(r.config.mapPhotoZoom == 12);
  CHECK(r.config.mapOpen);
  auto bad = pb::parseConfig("[map]\nwidth = 0.9\nphoto_zoom = 25\nopen = maybe\n");
  CHECK(bad.config.mapWidth == 0.8);
  CHECK(bad.config.mapPhotoZoom == 19);
  CHECK_FALSE(bad.config.mapOpen);
  CHECK(hasWarning(bad, "map.width"));
  CHECK(hasWarning(bad, "map.photo_zoom"));
  CHECK(hasWarning(bad, "maybe"));
  CHECK(pb::parseConfig("[map]\nwidth = 0.1\n").config.mapWidth == 0.2);
}

TEST_CASE("config: plugin sections keep raw values without warnings") {
  auto r = pb::parseConfig(
      "[plugin.maptiles]\nurl = https://x/{z}/{x}/{y}.png\nMax_Zoom = 18  # comment\n[plugin.video]\nfoo = Bar\n");
  CHECK(r.warnings.empty());
  CHECK(r.config.pluginSettings["maptiles"]["url"] == "https://x/{z}/{x}/{y}.png");
  CHECK(r.config.pluginSettings["maptiles"]["max_zoom"] == "18");
  CHECK(r.config.pluginSettings["video"]["foo"] == "Bar");
  CHECK(pb::Config{}.pluginSettings.empty());
}
