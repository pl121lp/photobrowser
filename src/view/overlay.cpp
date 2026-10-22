#include "view/overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

std::vector<OverlayQuad> overlayBars(const std::vector<OverlayQuad>& quads) {
  std::vector<OverlayQuad> bars;
  std::copy_if(quads.begin(), quads.end(), std::back_inserter(bars),
               [](const OverlayQuad& q) { return q.kind == OverlayQuad::Kind::Bar; });
  return bars;
}

}  // namespace pb
