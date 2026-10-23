#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/frame_history.h"
#include "core/geom.h"
#include "library/exif.h"

namespace pb {

// Fixed-cell bitmap font atlas layout (see tools/gen_font.py).
struct FontInfo {
  float cellW, cellH;    // texels per character cell
  float atlasW, atlasH;  // atlas size in texels
  int cols;              // cells per atlas row
  int firstChar, lastChar;
  int solidIndex;        // a fully covered cell, for filled rectangles
  const char32_t* extraChars = nullptr;  // characters beyond ASCII (e.g. © °), in cells ...
  int extraCount = 0;
  int extraFirstIndex = 0;               // ... from this one on
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

// Appends one quad per visible character of UTF-8 `text` (unknown characters show as '?').
void layoutText(std::string_view text, Vec2d origin, double scale, const float color[4], const FontInfo& font,
                std::vector<OverlayQuad>& out);

// Characters (columns) `text` takes: UTF-8 code points, a malformed byte counting as one.
size_t textColumns(std::string_view text);

std::vector<std::string> overlayLines(const OverlayStats& s);
// Window-title form of the stats, for windowed mode (UTF-8).
std::string statsTitle(const OverlayStats& s);

// Stats panel in the top-left corner: background, text lines, and a frame-time graph
// (one bar per recorded frame, 0..50 ms, with a line at 16.7 ms) when `graph`. Returns the panel
// rect. `scale` is the display's pixel density.
Rect buildOverlay(const OverlayStats& s, const FrameHistory& history, const FontInfo& font, double scale,
                  std::vector<OverlayQuad>& out, bool graph = true);

// Basic metadata of the photo in whole-photo view (F2), all from the catalog.
struct PhotoInfo {
  std::string name;  // relative path: the file name, then its folder on a line of its own
  uint32_t width = 0, height = 0;  // after EXIF orientation; 0 if unknown
  int64_t takenAt = 0;             // naive local time stored as seconds since 1970
  bool dateFromFile = false;       // no EXIF date: takenAt is the file's modification time
  uint64_t fileSize = 0;
  std::optional<GeoPos> gps;
};

// "YYYY-MM-DD HH:MM:SS" for a naive time stored as seconds since 1970.
std::string formatDateTime(int64_t seconds);
std::vector<std::string> photoInfoLines(const PhotoInfo& p);
// Photo info panel in the bottom-left corner of a `viewport`-sized screen. Returns the panel rect.
Rect buildPhotoInfo(const PhotoInfo& p, const FontInfo& font, double scale, Vec2d viewport,
                    std::vector<OverlayQuad>& out);

// Photo effect (keys 1-9 in whole-photo view): "<n>: <name>", empty for 0 or an unknown effect.
std::string photoEffectLabel(int effect);
// One-line label panel in the top-right corner of a `viewport`-sized screen. Returns the panel rect.
Rect buildPhotoEffectLabel(std::string_view label, const FontInfo& font, double scale, Vec2d viewport,
                           std::vector<OverlayQuad>& out);

// A notice (e.g. "Map unavailable: ...") centred near the top of a `viewport`-sized screen.
Rect buildMessage(std::string_view text, const FontInfo& font, double scale, Vec2d viewport,
                  std::vector<OverlayQuad>& out);
// A message centred in `area` (the map panel). Returns the panel rect.
Rect buildMapStatus(std::string_view text, const FontInfo& font, double scale, const Rect& area,
                    std::vector<OverlayQuad>& out);
// Small attribution text in the bottom-right corner of `area` (the map panel).
Rect buildAttribution(std::string_view text, const FontInfo& font, double scale, const Rect& area,
                      std::vector<OverlayQuad>& out);

// A frame of `thickness` px just outside `r` (e.g. around a highlighted photo): four bars.
void buildFrame(const Rect& r, double thickness, const float color[4], const FontInfo& font,
                std::vector<OverlayQuad>& out);

// Help (F1): every key and mouse binding. An entry with an empty key and action is a gap.
struct HelpEntry {
  std::string key, action;
};
std::vector<HelpEntry> helpEntries();
// Help panel centered in a `viewport`-sized screen, shrunk to fit if needed. Returns the panel rect.
Rect buildHelp(const FontInfo& font, double scale, Vec2d viewport, std::vector<OverlayQuad>& out);

std::vector<OverlayQuad> overlayBars(const std::vector<OverlayQuad>& quads);

}  // namespace pb
