// Manual benchmark (run with --no-skip, release build): mean quickIndex time per file for
// JPEG, PNG and HEIC from photos/. Spec bar: HEIC <= ~3x JPEG per file.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "library/indexer.h"
#include "library/png_handler.h"
#include "library/scanner.h"
#include "plugin/plugin_host.h"

namespace fs = std::filesystem;

namespace {

std::string lowerExt(const std::string& p) {
  std::string e = fs::path(p).extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return e;
}

// Up to 50 files with the given extensions, evenly spaced through the sorted scan.
std::vector<pb::ScanEntry> sample(const std::vector<pb::ScanEntry>& all, std::vector<std::string> exts) {
  std::vector<pb::ScanEntry> match;
  for (const auto& e : all)
    if (std::find(exts.begin(), exts.end(), lowerExt(e.relPath)) != exts.end()) match.push_back(e);
  std::vector<pb::ScanEntry> out;
  const size_t n = std::min<size_t>(50, match.size());
  for (size_t i = 0; i < n; ++i) out.push_back(match[i * match.size() / n]);
  return out;
}

void measure(const char* name, const fs::path& root, const std::vector<pb::ScanEntry>& files,
             const pb::MediaFormats& formats) {
  if (files.empty()) {
    std::printf("[perf] %-5s no files\n", name);
    return;
  }
  for (const auto& e : files) pb::quickIndex(root, e, formats);  // untimed: warm the file cache
  std::vector<double> ms;
  for (const auto& e : files) {
    const auto t0 = std::chrono::steady_clock::now();
    pb::quickIndex(root, e, formats);
    ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  }
  double sum = 0;
  size_t fast = 0;
  for (double v : ms) {
    sum += v;
    if (v < 10.0) ++fast;
  }
  std::sort(ms.begin(), ms.end());
  for (double v : ms)
    if (v >= 10.0) std::printf("  slow %.1f ms\n", v);
  std::printf("[perf] %-5s n=%zu mean %.2f ms  median %.2f  max %.2f  (%zu files < 10 ms)\n", name, ms.size(),
              sum / double(ms.size()), ms[ms.size() / 2], ms.back(), fast);
}

}  // namespace

TEST_CASE("perf: quickIndex per file - JPEG, PNG, HEIC" * doctest::skip()) {
  const fs::path root = PB_TEST_PHOTOS_DIR;
  static const pb::PngHandler png;
  pb::PluginHost host;
  host.addBuiltinFormat(".png", &png, pb::kPngOrigin);
#ifdef PB_TEST_HEIF_PLUGIN
  REQUIRE_FALSE(host.load("heif", {fs::path(PB_TEST_PLUGIN_DIR)}));
#endif
  const auto& formats = host.formats();
  const auto all = pb::scanFolder(root, formats, true);
  measure("JPEG", root, sample(all, {".jpg", ".jpeg"}), formats);
  measure("PNG", root, sample(all, {".png"}), formats);
#ifdef PB_TEST_HEIF_PLUGIN
  measure("HEIC", root, sample(all, {".heic"}), formats);
#endif
}
