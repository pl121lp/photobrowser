#include <doctest/doctest.h>

#include <algorithm>

#include "core/frame_history.h"
#include "view/overlay.h"

namespace {
const pb::FontInfo kFont{13, 25, 208, 150, 16, 32, 126, 95};
const float kWhite[4] = {1, 1, 1, 1};

pb::OverlayStats stats() {
  pb::OverlayStats s;
  s.fps = 60;
  s.frameMs = 16.7;
  s.p99Ms = 17.94;
  s.jobs = 3;
  s.vramMB = 341;
  s.budgetMB = 640;
  s.photos = 10000;
  s.tiles = 412;
  s.tilePx = 128.4;
  return s;
}
}  // namespace

TEST_CASE("frame history: keeps the newest values, oldest first") {
  pb::FrameHistory h(3);
  CHECK(h.size() == 0);
  for (double v : {1.0, 2.0, 3.0, 4.0}) h.add(v);
  REQUIRE(h.size() == 3);
  CHECK(h.at(0) == 2);
  CHECK(h.at(2) == 4);
  CHECK(h.median() == 3);
}

TEST_CASE("overlay: text layout makes one quad per visible character") {
  std::vector<pb::OverlayQuad> q;
  pb::layoutText("ab c", {10, 20}, 2.0, kWhite, kFont, q);
  REQUIRE(q.size() == 3);
  CHECK(q[0].rect == pb::Rect{10, 20, 10 + 26, 20 + 50});
  CHECK(q[1].rect.x0 == doctest::Approx(10 + 26));
  CHECK(q[2].rect.x0 == doctest::Approx(10 + 3 * 26));
  // 'a' is index 65 in the atlas: column 1, row 4. UVs are inset half a texel so
  // filtering at fractional scales never picks up the neighbouring glyph.
  CHECK(q[0].uv.u0 == doctest::Approx(13.5f / 208));
  CHECK(q[0].uv.u1 == doctest::Approx(25.5f / 208));
  CHECK(q[0].uv.v0 == doctest::Approx((4 * 25.0f + 0.5f) / 150));
}

TEST_CASE("overlay: characters outside the font render as '?'") {
  std::vector<pb::OverlayQuad> q, expect;
  pb::layoutText("\x01", {0, 0}, 1.0, kWhite, kFont, q);
  pb::layoutText("?", {0, 0}, 1.0, kWhite, kFont, expect);
  REQUIRE(q.size() == 1);
  CHECK(q[0].uv.u0 == expect[0].uv.u0);
  CHECK(q[0].uv.v0 == expect[0].uv.v0);
}

TEST_CASE("overlay: stat lines") {
  auto lines = pb::overlayLines(stats());
  REQUIRE(lines.size() == 3);
  CHECK(lines[0] == "60 fps  16.7 ms  p99 17.9 ms");
  CHECK(lines[1] == "jobs 3  VRAM 341/640 MB");
  CHECK(lines[2] == "10000 photos  412 tiles  128 px");
}

TEST_CASE("overlay: panel, text and one bar per recorded frame") {
  pb::FrameHistory h(240);
  for (int i = 0; i < 100; ++i) h.add(16.7);
  h.add(40);  // a hitch
  std::vector<pb::OverlayQuad> q;
  const pb::Rect panel = pb::buildOverlay(stats(), h, kFont, 1.0, q);
  REQUIRE_FALSE(q.empty());
  CHECK(q[0].rect == panel);                    // background first
  CHECK(q[0].color[3] < 1);                     // translucent
  for (const auto& quad : q) {
    CHECK(quad.rect.x0 >= panel.x0 - 1e-9);
    CHECK(quad.rect.x1 <= panel.x1 + 1e-9);
    CHECK(quad.rect.y0 >= panel.y0 - 1e-9);
    CHECK(quad.rect.y1 <= panel.y1 + 1e-9);
  }
  const auto bars = pb::overlayBars(q);
  REQUIRE(bars.size() == 101);
  const auto& spike = bars.back();
  const auto& normal = bars.front();
  CHECK(spike.rect.height() > normal.rect.height() * 2);
  CHECK(spike.color[0] > spike.color[1]);    // red
  CHECK(normal.color[1] > normal.color[0]);  // green
}

TEST_CASE("overlay: scale enlarges everything") {
  pb::FrameHistory h(10);
  h.add(16.7);
  std::vector<pb::OverlayQuad> a, b;
  const auto p1 = pb::buildOverlay(stats(), h, kFont, 1.0, a);
  const auto p2 = pb::buildOverlay(stats(), h, kFont, 2.0, b);
  CHECK(p2.width() == doctest::Approx(p1.width() * 2));
  CHECK(p2.height() == doctest::Approx(p1.height() * 2));
}

TEST_CASE("frame history: percentile") {
  pb::FrameHistory h(100);
  for (int i = 1; i <= 100; ++i) h.add(i);
  CHECK(h.percentile(99) == doctest::Approx(99).epsilon(0.02));
  CHECK(h.percentile(50) == doctest::Approx(50).epsilon(0.03));
  CHECK(pb::FrameHistory(5).percentile(99) == 0);
}

TEST_CASE("overlay: window title shows the same stats including photo count") {
  CHECK(pb::statsTitle(stats()) ==
        "PhotoBrowse — 60 fps · p99 17.9 ms · 10000 photos · 412 tiles · 128 px · jobs 3 · VRAM 341/640 MB");
}

TEST_CASE("overlay: graph can be turned off") {
  pb::FrameHistory h(240);
  for (int i = 0; i < 50; ++i) h.add(16.7);
  std::vector<pb::OverlayQuad> with, without;
  const auto big = pb::buildOverlay(stats(), h, kFont, 1.0, with, true);
  const auto small = pb::buildOverlay(stats(), h, kFont, 1.0, without, false);
  CHECK(pb::overlayBars(without).empty());
  CHECK(small.height() < big.height());
  CHECK(small.width() < big.width());
}

namespace {
pb::PhotoInfo info() {
  pb::PhotoInfo p;
  p.name = "IMG_1234.jpg";
  p.width = 4032;
  p.height = 3024;
  p.takenAt = 1715954602;  // 2024-05-17 14:03:22
  p.fileSize = 3565158;    // 3.4 MB
  return p;
}
}  // namespace

TEST_CASE("photo info: date formatting") {
  CHECK(pb::formatDateTime(0) == "1970-01-01 00:00:00");
  CHECK(pb::formatDateTime(1715954602) == "2024-05-17 14:03:22");
  CHECK(pb::formatDateTime(951782400) == "2000-02-29 00:00:00");  // leap day
  CHECK(pb::formatDateTime(-1) == "1969-12-31 23:59:59");
}

TEST_CASE("photo info: lines") {
  auto lines = pb::photoInfoLines(info());
  REQUIRE(lines.size() == 4);
  CHECK(lines[0] == "IMG_1234.jpg");
  CHECK(lines[1] == "4032 x 3024  12.2 MP");
  CHECK(lines[2] == "Taken 2024-05-17 14:03:22");
  CHECK(lines[3] == "3.4 MB");
}

TEST_CASE("photo info: file date is labelled as such, unknown size skipped") {
  auto p = info();
  p.dateFromFile = true;
  p.width = p.height = 0;
  auto lines = pb::photoInfoLines(p);
  REQUIRE(lines.size() == 3);
  CHECK(lines[1] == "Modified 2024-05-17 14:03:22");
}

TEST_CASE("photo info: name is the file name only, long names shortened") {
  auto p = info();
  p.name = "2024/holiday/IMG_1234.jpg";
  CHECK(pb::photoInfoLines(p)[0] == "IMG_1234.jpg");
  p.name = std::string(100, 'a') + ".jpg";
  const auto first = pb::photoInfoLines(p)[0];
  CHECK(first.size() == 48);
  CHECK(first.ends_with("a.jpg"));
  CHECK(first.find("...") != std::string::npos);
}

TEST_CASE("photo info: panel sits in the bottom-left corner and holds its text") {
  std::vector<pb::OverlayQuad> q;
  const pb::Vec2d viewport{1920, 1080};
  const pb::Rect panel = pb::buildPhotoInfo(info(), kFont, 1.0, viewport, q);
  REQUIRE_FALSE(q.empty());
  CHECK(q[0].rect == panel);
  CHECK(q[0].color[3] < 1);
  CHECK(panel.x0 > 0);
  CHECK(panel.x0 < 100);
  CHECK(panel.y1 < viewport.y);
  CHECK(panel.y1 > viewport.y - 100);
  for (const auto& quad : q) {
    CHECK(quad.rect.x0 >= panel.x0 - 1e-9);
    CHECK(quad.rect.x1 <= panel.x1 + 1e-9);
    CHECK(quad.rect.y0 >= panel.y0 - 1e-9);
    CHECK(quad.rect.y1 <= panel.y1 + 1e-9);
  }
}

TEST_CASE("help: lists every key binding in printable ASCII") {
  const auto entries = pb::helpEntries();
  auto has = [&](std::string_view key) {
    return std::any_of(entries.begin(), entries.end(), [&](const pb::HelpEntry& e) { return e.key == key; });
  };
  for (auto key : {"F1", "F2", "F3", "F5", "F6", "F11 / F", "Arrows", "+ / -", "Home", "Enter", "Esc", "Q", "Drag",
                   "Wheel", "Double-click", "Right-click"}) {
    CHECK_MESSAGE(has(key), key);
  }
  for (const auto& e : entries) {
    for (char ch : e.key + e.action) CHECK((ch >= 32 && ch <= 126));
  }
}

TEST_CASE("help: centered panel, keys and descriptions in two aligned columns") {
  std::vector<pb::OverlayQuad> q;
  const pb::Vec2d viewport{1920, 1080};
  const pb::Rect panel = pb::buildHelp(kFont, 1.0, viewport, q);
  REQUIRE_FALSE(q.empty());
  CHECK(q[0].rect == panel);
  CHECK(q[0].color[3] < 1);
  CHECK(panel.center().x == doctest::Approx(viewport.x / 2));
  CHECK(panel.center().y == doctest::Approx(viewport.y / 2));
  for (const auto& quad : q) {
    CHECK(quad.rect.x0 >= panel.x0 - 1e-9);
    CHECK(quad.rect.x1 <= panel.x1 + 1e-9);
    CHECK(quad.rect.y0 >= panel.y0 - 1e-9);
    CHECK(quad.rect.y1 <= panel.y1 + 1e-9);
  }
  // Rows start at one of two x positions: the key column and the description column.
  std::vector<double> starts;
  double lastY = -1;
  for (const auto& quad : q) {
    if (quad.kind != pb::OverlayQuad::Kind::Text) continue;
    if (quad.rect.y0 != lastY) starts.push_back(quad.rect.x0), lastY = quad.rect.y0;
  }
  std::sort(starts.begin(), starts.end());
  starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
  CHECK(starts.size() <= 3);  // key column, description column (+ title)
}

TEST_CASE("help: shrinks to fit a small window") {
  std::vector<pb::OverlayQuad> q;
  const pb::Vec2d viewport{640, 360};
  const pb::Rect panel = pb::buildHelp(kFont, 2.0, viewport, q);
  CHECK(panel.x0 >= 0);
  CHECK(panel.y0 >= 0);
  CHECK(panel.x1 <= viewport.x);
  CHECK(panel.y1 <= viewport.y);
}

TEST_CASE("photo effect: label per effect, none for 0 or unknown") {
  CHECK(pb::photoEffectLabel(0).empty());
  CHECK(pb::photoEffectLabel(1) == "1: Sharpen");
  CHECK(pb::photoEffectLabel(2) == "2: Sobel edges");
  CHECK(pb::photoEffectLabel(3) == "3: Saturation");
  CHECK(pb::photoEffectLabel(4).empty());
  CHECK(pb::photoEffectLabel(-1).empty());
}

TEST_CASE("photo effect: label panel sits in the top-right corner and holds its text") {
  std::vector<pb::OverlayQuad> q;
  const pb::Vec2d viewport{1920, 1080};
  const pb::Rect panel = pb::buildPhotoEffectLabel("2: Sobel edges", kFont, 1.0, viewport, q);
  REQUIRE(q.size() == 1 + 12);  // panel + visible characters
  CHECK(q[0].rect == panel);
  CHECK(q[0].color[3] < 1);
  CHECK(panel.x1 < viewport.x);
  CHECK(panel.x1 > viewport.x - 100);
  CHECK(panel.y0 > 0);
  CHECK(panel.y0 < 100);
  for (const auto& quad : q) {
    CHECK(quad.rect.x0 >= panel.x0 - 1e-9);
    CHECK(quad.rect.x1 <= panel.x1 + 1e-9);
    CHECK(quad.rect.y0 >= panel.y0 - 1e-9);
    CHECK(quad.rect.y1 <= panel.y1 + 1e-9);
  }
}
