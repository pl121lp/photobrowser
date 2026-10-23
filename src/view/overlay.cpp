#include "view/overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace pb {

namespace {
constexpr double kMargin = 16, kPadding = 10, kBarW = 2, kGraphH = 80, kGraphMs = 50, kGap = 8;
constexpr double kRefreshMs = 1000.0 / 60;

UvRect cellUv(int index, const FontInfo& f) {
  // Half-texel inset: bilinear filtering at fractional scales stays inside the cell.
  const float x = float(index % f.cols) * f.cellW, y = float(index / f.cols) * f.cellH;
  return {(x + 0.5f) / f.atlasW, (y + 0.5f) / f.atlasH, (x + f.cellW - 0.5f) / f.atlasW,
          (y + f.cellH - 0.5f) / f.atlasH};
}

UvRect solidUv(const FontInfo& f) {
  // Center of the solid cell, so filtering never reaches a neighbouring glyph.
  const UvRect c = cellUv(f.solidIndex, f);
  const float cu = (c.u0 + c.u1) / 2, cv = (c.v0 + c.v1) / 2;
  return {cu, cv, cu, cv};
}

OverlayQuad solid(OverlayQuad::Kind kind, const Rect& r, const FontInfo& f, float red, float green, float blue,
                  float alpha) {
  return {kind, r, solidUv(f), {red, green, blue, alpha}};
}
}  // namespace

void layoutText(std::string_view text, Vec2d origin, double scale, const float color[4], const FontInfo& font,
                std::vector<OverlayQuad>& out) {
  const double w = font.cellW * scale, h = font.cellH * scale;
  double x = origin.x;
  for (const char ch : text) {
    int code = static_cast<unsigned char>(ch);
    if (code != ' ') {
      if (code < font.firstChar || code > font.lastChar) code = '?';
      out.push_back({OverlayQuad::Kind::Text,
                     {x, origin.y, x + w, origin.y + h},
                     cellUv(code - font.firstChar, font),
                     {color[0], color[1], color[2], color[3]}});
    }
    x += w;
  }
}

std::vector<std::string> overlayLines(const OverlayStats& s) {
  char a[96], b[96], c[96];
  std::snprintf(a, sizeof a, "%.0f fps  %.1f ms  p99 %.1f ms", s.fps, s.frameMs, s.p99Ms);
  std::snprintf(b, sizeof b, "jobs %zu  VRAM %.0f/%.0f MB", s.jobs, s.vramMB, s.budgetMB);
  std::snprintf(c, sizeof c, "%zu photos  %zu tiles  %.0f px", s.photos, s.tiles, s.tilePx);
  return {a, b, c};
}

std::string statsTitle(const OverlayStats& s) {
  char t[256];
  std::snprintf(t, sizeof t,
                "PhotoBrowse — %.0f fps · p99 %.1f ms · %zu photos · %zu tiles · %.0f px · jobs %zu · VRAM %.0f/%.0f MB",
                s.fps, s.p99Ms, s.photos, s.tiles, s.tilePx, s.jobs, s.vramMB, s.budgetMB);
  return t;
}

Rect buildOverlay(const OverlayStats& s, const FrameHistory& history, const FontInfo& font, double scale,
                  std::vector<OverlayQuad>& out, bool graph) {
  const auto lines = overlayLines(s);
  size_t longest = 0;
  for (const auto& l : lines) longest = std::max(longest, l.size());
  const double pad = kPadding * scale, lineH = font.cellH * scale;
  const double textW = double(longest) * font.cellW * scale;
  const double graphW = graph ? double(history.capacity()) * kBarW * scale : 0;
  const double graphH = graph ? kGraphH * scale : 0, graphGap = graph ? kGap * scale : 0;
  const double x0 = kMargin * scale, y0 = kMargin * scale;
  const double innerW = std::max(textW, graphW);
  const Rect panel{x0, y0, x0 + innerW + 2 * pad, y0 + pad + double(lines.size()) * lineH + graphGap + graphH + pad};

  out.push_back(solid(OverlayQuad::Kind::Panel, panel, font, 0, 0, 0, 0.75f));

  const float white[4] = {0.95f, 0.95f, 0.95f, 1};
  for (size_t i = 0; i < lines.size(); ++i) {
    layoutText(lines[i], {x0 + pad, y0 + pad + double(i) * lineH}, scale, white, font, out);
  }

  if (!graph) return panel;

  // Frame-time graph, newest frame on the right.
  const double gx = x0 + pad, gy = panel.y1 - pad - graphH;
  const double barW = kBarW * scale;
  const size_t n = history.size(), slots = history.capacity();
  for (size_t i = 0; i < n; ++i) {
    const double ms = history.at(i);
    const double h = std::max(scale, std::min(ms, kGraphMs) / kGraphMs * graphH);
    const double x = gx + double(slots - n + i) * barW;
    float r = 0.25f, g = 0.85f, b = 0.35f;  // on time
    if (ms > 25) {
      r = 0.95f, g = 0.25f, b = 0.2f;  // dropped frame
    } else if (ms > 18) {
      r = 0.95f, g = 0.8f, b = 0.2f;  // late
    }
    out.push_back(solid(OverlayQuad::Kind::Bar, {x, gy + graphH - h, x + barW, gy + graphH}, font, r, g, b, 0.9f));
  }
  const double refY = gy + graphH - kRefreshMs / kGraphMs * graphH;
  out.push_back(solid(OverlayQuad::Kind::Line, {gx, refY, gx + graphW, refY + std::max(1.0, scale)}, font, 1, 1, 1,
                      0.35f));
  return panel;
}

std::string formatDateTime(int64_t seconds) {
  // Civil date from days since 1970 (Howard Hinnant's algorithm), no time zone involved.
  int64_t days = seconds / 86400, secs = seconds % 86400;
  if (secs < 0) secs += 86400, --days;
  const int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  const int64_t day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
  const int64_t year = yoe + era * 400 + (month <= 2);
  char t[96];
  std::snprintf(t, sizeof t, "%04lld-%02lld-%02lld %02lld:%02lld:%02lld", (long long)year, (long long)month,
                (long long)day, (long long)(secs / 3600), (long long)(secs / 60 % 60), (long long)(secs % 60));
  return t;
}

namespace {
// At most 48 characters, the middle replaced by "..." when longer.
std::string shortened(std::string s) {
  constexpr size_t kMax = 48;
  if (s.size() <= kMax) return s;
  const size_t tail = (kMax - 3) / 2;
  return s.substr(0, kMax - 3 - tail) + "..." + s.substr(s.size() - tail);
}
}  // namespace

std::vector<std::string> photoInfoLines(const PhotoInfo& p) {
  std::vector<std::string> lines;
  const size_t slash = p.name.find_last_of('/');
  lines.push_back(shortened(p.name.substr(slash + 1)));
  if (slash != std::string::npos) lines.push_back(shortened(p.name.substr(0, slash + 1)));
  char t[96];
  if (p.width && p.height) {
    std::snprintf(t, sizeof t, "%u x %u  %.1f MP", p.width, p.height, double(p.width) * p.height / 1e6);
    lines.push_back(t);
  }
  lines.push_back((p.dateFromFile ? "Modified " : "Taken ") + formatDateTime(p.takenAt));
  std::snprintf(t, sizeof t, "%.1f MB", double(p.fileSize) / (1 << 20));
  lines.push_back(t);
  if (p.gps) {
    // Five decimals is about a metre; the font has no degree sign.
    int n = std::snprintf(t, sizeof t, "GPS %.5f %c, %.5f %c", std::abs(p.gps->lat), p.gps->lat < 0 ? 'S' : 'N',
                          std::abs(p.gps->lon), p.gps->lon < 0 ? 'W' : 'E');
    if (p.gps->altitude) std::snprintf(t + n, sizeof t - size_t(n), "  %.0f m", *p.gps->altitude);
    lines.push_back(t);
  }
  return lines;
}

Rect buildPhotoInfo(const PhotoInfo& p, const FontInfo& font, double scale, Vec2d viewport,
                    std::vector<OverlayQuad>& out) {
  const auto lines = photoInfoLines(p);
  size_t longest = 0;
  for (const auto& l : lines) longest = std::max(longest, l.size());
  const double pad = kPadding * scale, lineH = font.cellH * scale;
  const double x0 = kMargin * scale, y1 = viewport.y - kMargin * scale;
  const Rect panel{x0, y1 - 2 * pad - double(lines.size()) * lineH, x0 + double(longest) * font.cellW * scale + 2 * pad,
                   y1};
  out.push_back(solid(OverlayQuad::Kind::Panel, panel, font, 0, 0, 0, 0.6f));
  const float white[4] = {0.95f, 0.95f, 0.95f, 1}, grey[4] = {0.7f, 0.7f, 0.7f, 1};
  for (size_t i = 0; i < lines.size(); ++i) {
    layoutText(lines[i], {x0 + pad, panel.y0 + pad + double(i) * lineH}, scale, i == 0 ? white : grey, font, out);
  }
  return panel;
}

std::string photoEffectLabel(int effect) {
  // Same order as the shaders in src/gfx/tile_renderer.cpp.
  constexpr const char* kNames[] = {"Sharpen",   "Sobel edges",   "Saturation", "Black & white",
                                    "Posterize", "Pencil sketch", "Oil paint",  "Tilt-shift"};
  if (effect < 1 || effect > int(std::size(kNames))) return {};
  return std::to_string(effect) + ": " + kNames[effect - 1];
}

Rect buildPhotoEffectLabel(std::string_view label, const FontInfo& font, double scale, Vec2d viewport,
                           std::vector<OverlayQuad>& out) {
  const double pad = kPadding * scale;
  const double x1 = viewport.x - kMargin * scale, y0 = kMargin * scale;
  const Rect panel{x1 - double(label.size()) * font.cellW * scale - 2 * pad, y0, x1, y0 + font.cellH * scale + 2 * pad};
  out.push_back(solid(OverlayQuad::Kind::Panel, panel, font, 0, 0, 0, 0.6f));
  const float white[4] = {0.95f, 0.95f, 0.95f, 1};
  layoutText(label, {panel.x0 + pad, y0 + pad}, scale, white, font, out);
  return panel;
}

std::vector<HelpEntry> helpEntries() {
  return {
      {"F1", "show / hide this help"},
      {"F2", "photo info (in whole-photo view)"},
      {"F3", "performance stats"},
      {"F5", "screensaver on / off"},
      {"F6", "next background"},
      {"F11 / F", "fullscreen on / off"},
      {"", ""},
      {"Arrows", "move one photo"},
      {"Left / Right", "whole-photo view: previous / next photo"},
      {"Up / Down", "whole-photo view: closest photo one row up / down"},
      {"Shift + arrows", "move five photos (rows) at a time"},
      {"+ / -", "zoom in / out"},
      {"Home", "back to the start view"},
      {"Enter", "open the centered photo whole"},
      {"1 - 8 / 0", "whole-photo view: photo effect (name top right) / none"},
      {"Esc", "close help, leave whole-photo view, then fullscreen"},
      {"Q", "quit"},
      {"", ""},
      {"Drag", "pan (release while moving to glide); pan a zoomed photo"},
      {"Click", "glide a photo to the center"},
      {"Double-click", "open a photo whole"},
      {"Wheel", "zoom about the cursor (also inside a whole photo)"},
      {"Right-click", "reset a zoomed photo to fit"},
      {"Click open photo", "plugin action, e.g. play / pause a video"},
  };
}

Rect buildHelp(const FontInfo& font, double scale, Vec2d viewport, std::vector<OverlayQuad>& out) {
  const std::string title = "PhotoBrowse keys";
  const auto entries = helpEntries();
  size_t keyChars = 0, actionChars = 0;
  for (const auto& e : entries) {
    keyChars = std::max(keyChars, e.key.size());
    actionChars = std::max(actionChars, e.action.size());
  }
  keyChars += 3;  // gap between the columns
  const double cols = double(std::max(title.size(), keyChars + actionChars));
  const double rows = double(entries.size() + 2);  // title and a gap
  // Shrink so the panel fits inside the margins.
  auto size = [&](double s) {
    return Vec2d{cols * font.cellW * s + 2 * kPadding * s, rows * font.cellH * s + 2 * kPadding * s};
  };
  const Vec2d full = size(scale);
  const double fit = std::min({1.0, (viewport.x - 2 * kMargin) / full.x, (viewport.y - 2 * kMargin) / full.y});
  scale *= fit;
  const Vec2d sz = size(scale);
  const Vec2d c = viewport * 0.5;
  const Rect panel{c.x - sz.x / 2, c.y - sz.y / 2, c.x + sz.x / 2, c.y + sz.y / 2};
  // Nearly opaque: blending is in linear light, so even 0.9 looks quite see-through.
  out.push_back(solid(OverlayQuad::Kind::Panel, panel, font, 0, 0, 0, 0.98f));

  const float white[4] = {0.95f, 0.95f, 0.95f, 1}, grey[4] = {0.72f, 0.72f, 0.72f, 1};
  const float accent[4] = {0.45f, 0.7f, 1.0f, 1};
  const double pad = kPadding * scale, lineH = font.cellH * scale, charW = font.cellW * scale;
  layoutText(title, {panel.x0 + pad, panel.y0 + pad}, scale, white, font, out);
  for (size_t i = 0; i < entries.size(); ++i) {
    const double y = panel.y0 + pad + double(i + 2) * lineH;
    layoutText(entries[i].key, {panel.x0 + pad, y}, scale, accent, font, out);
    layoutText(entries[i].action, {panel.x0 + pad + double(keyChars) * charW, y}, scale, grey, font, out);
  }
  return panel;
}

std::vector<OverlayQuad> overlayBars(const std::vector<OverlayQuad>& quads) {
  std::vector<OverlayQuad> bars;
  std::copy_if(quads.begin(), quads.end(), std::back_inserter(bars),
               [](const OverlayQuad& q) { return q.kind == OverlayQuad::Kind::Bar; });
  return bars;
}

}  // namespace pb
