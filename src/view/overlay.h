#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/frame_history.h"
#include "core/geom.h"

namespace pb {

// Fixed-cell bitmap font atlas layout (see tools/gen_font.py).
struct FontInfo {
  float cellW, cellH;    // texels per character cell
  float atlasW, atlasH;  // atlas size in texels
  int cols;              // cells per atlas row
  int firstChar, lastChar;
  int solidIndex;        // a fully covered cell, for filled rectangles
};

struct OverlayQuad {
  enum class Kind { Panel, Text, Bar, Line } kind = Kind::Text;
  Rect rect;       // screen px
  UvRect uv;       // atlas coordinates
  float color[4];  // linear RGBA, multiplied by the atlas coverage
};

struct OverlayStats {
  double fps = 0, frameMs = 0, p99Ms = 0;
  size_t jobs = 0;
  double vramMB = 0, budgetMB = 0;
  size_t photos = 0, tiles = 0;
  double tilePx = 0;  // on-screen size of one photo
};

// Appends one quad per visible character (unknown characters show as '?').
void layoutText(std::string_view text, Vec2d origin, double scale, const float color[4], const FontInfo& font,
                std::vector<OverlayQuad>& out);

std::vector<std::string> overlayLines(const OverlayStats& s);

// Stats panel in the top-left corner: background, text lines, and a frame-time graph
// (one bar per recorded frame, 0..50 ms, with a line at 16.7 ms). Returns the panel
// rect. `scale` is the display's pixel density.
Rect buildOverlay(const OverlayStats& s, const FrameHistory& history, const FontInfo& font, double scale,
                  std::vector<OverlayQuad>& out);

std::vector<OverlayQuad> overlayBars(const std::vector<OverlayQuad>& quads);

}  // namespace pb
