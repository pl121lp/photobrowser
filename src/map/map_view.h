#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string_view>

#include "core/geom.h"
#include "library/exif.h"
#include "map/map_camera.h"
#include "map/tile_source.h"
#include "view/motion.h"

namespace pb {

enum class MapNotice { NeedsPlugin, NoInternet, Refused };
std::string_view mapNoticeText(MapNotice n);

struct MapSettings {
  double width = 0.4;      // fraction of the window the panel takes
  double photoZoom = 15;   // zoom for a single photo's location
};

// The map panel (M): its slide in from the right, the split of the window into content
// (left) and map (right), pointer routing, what the map shows (one photo or the visible
// grid photos), and the error notices. Pure logic; times in seconds, positions in
// window px.
class MapView {
 public:
  static constexpr double kSlideS = 0.35, kRestS = 0.3, kPulseS = 2.0, kGridMaxZoom = 16, kPadding = 0.1,
                          kOpenZoom = 4;

  // What the content shows this frame.
  struct Subject {
    std::optional<uint32_t> photo;       // open photo (layout position); nullopt = grid
    std::optional<GeoPos> photoGps;      // its position, if any
    std::span<const GeoPos> visibleGps;  // grid: GPS of the visible photos (any order, duplicates ok)
    bool gridMoving = false;             // the content is moving
  };

  explicit MapView(MapSettings s = {});

  // M: opens or closes the map. Without a tile source it stays closed and posts NeedsPlugin.
  void toggle(double t, const TileSource* source);
  bool escape(double t);  // closes an open map; true if it did
  bool isOpen() const { return open_; }
  bool shown() const { return open_ || slide_ > 0; }  // open or sliding: draw it
  double slide() const { return slide_; }             // eased 0 (closed) .. 1 (open)
  Rect contentRect(Vec2d window) const;
  Rect panelRect(Vec2d window) const;

  // Pointer in window px: a press over the panel goes to the map, which keeps the
  // pointer until release. Each returns true if the map took the event.
  bool pointerDown(Vec2d px, double t);
  bool pointerMove(Vec2d px, double t);
  bool pointerUp(Vec2d px, double t);
  bool wheel(Vec2d px, double notches);

  void update(double t, double dt, Vec2d window, double uiScale, const Subject& s, size_t tilesPending);
  void tileResults(std::span<const TileStatus> results);
  std::optional<MapNotice> takeNotice();

  MapCamera& camera() { return cam_; }
  const MapCamera& camera() const { return cam_; }

 private:
  void setOpen(bool open, double t);
  double panelWidth(Vec2d window) const;
  Vec2d toPanel(Vec2d px) const;

  MapSettings settings_;
  MapCamera cam_;
  bool open_ = false;
  Tween slideTween_;
  double slide_ = 0;
  Vec2d window_;
  bool captured_ = false;

  // Errors: until the first good tile after opening, only offline results means no internet.
  bool probing_ = false;
  int probeOffline_ = 0;
  bool lostNoticed_ = false, refusedNoticed_ = false;
  std::deque<MapNotice> notices_;
};

}  // namespace pb
