#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/geom.h"
#include "library/exif.h"
#include "map/map_camera.h"
#include "map/tile_source.h"
#include "map/trip_path.h"
#include "view/motion.h"

namespace pb {

enum class MapNotice { NeedsPlugin, NoInternet, Refused };
std::string_view mapNoticeText(MapNotice n);

struct MapSettings {
  double width = 0.4;      // fraction of the window the panel takes
  double photoZoom = 15;   // zoom for a single photo's location
  bool tripPath = false;   // grid: join the visible photos in time order
  int64_t tripGapS = kTripMaxGapS;  // ... breaking the line where photos are further apart
};

// The map panel (M): its slide in from the right, the split of the window into content
// (left) and map (right), pointer routing, what the map shows (one photo or the visible
// grid photos), and the error notices. Pure logic; times in seconds, positions in
// window px.
class MapView {
 public:
  static constexpr double kSlideS = 0.35, kRestS = 0.3, kPulseS = 2.0, kGridMaxZoom = 16, kPadding = 0.1,
                          kOpenZoom = 4;
  static constexpr size_t kTrailMax = 1000;  // trail points kept while stepping through photos

  // What the content shows this frame.
  struct Subject {
    std::optional<uint32_t> photo;       // open photo (layout position); nullopt = grid
    std::optional<GeoPos> photoGps;      // its position, if any
    std::span<const GeoPos> visibleGps;  // grid: GPS of the visible photos (any order, duplicates ok)
    std::span<const uint32_t> visiblePos;  // their layout positions, same order (empty: unknown)
    std::span<const int64_t> visibleTakenAt;  // their capture times, same order (empty: no trip path)
    std::optional<uint32_t> hoveredPhoto;  // grid: the photo under the pointer (highlights its dot)
    bool gridMoving = false;             // the content is moving
    bool frozen = false;                 // keep showing what was shown (screensaver between photos)
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
  void pointerLeave();  // the pointer left the window: nothing hovered

  void update(double t, double dt, Vec2d window, double uiScale, const Subject& s, size_t tilesPending);
  void tileResults(std::span<const TileStatus> results);
  std::optional<MapNotice> takeNotice();

  const std::vector<Vec2d>& dots() const { return dots_; }  // world positions, deduplicated
  // Grid: layout positions of the photos a dot stands for (photos taken at one spot share a dot).
  const std::vector<uint32_t>& dotPhotos(size_t dot) const;
  Vec2d dotScreen(size_t dot) const;  // window px, on the copy of the world nearest the view
  // Grid only (nothing while a photo is open): the dot hovered on the map, or the dot of
  // the photo hovered in the grid; the photos of the dot hovered on the map; the photos of
  // a dot clicked on the map (once).
  std::optional<size_t> highlightedDot() const { return highlightedDot_; }
  const std::vector<uint32_t>& hoveredPhotos() const;
  std::vector<uint32_t> takeClickedPhotos() { return std::exchange(clicked_, {}); }
  bool singleMode() const { return single_; }
  // Grid with [map] trip_path on: the trip path in window px, on the copy of the world
  // nearest the view.
  std::vector<TripSegment> tripSegments() const;
  // A photo open with [map] trip_path on: the locations of the photos stepped through
  // since it was opened from the grid, joined in the order shown (window px), and the
  // dots of all but the current one.
  std::vector<TripSegment> trailSegments() const;
  std::vector<Vec2d> trailDots() const;
  std::optional<double> pulse(double t) const;  // halo phase 0..1 while pulsing
  std::string_view status() const;              // "" or why there are no dots
  bool dimmed() const { return !status().empty(); }
  bool animating(double t) const;               // needs redrawing (sliding, moving, pulsing, refit due)

  MapCamera& camera() { return cam_; }
  const MapCamera& camera() const { return cam_; }

 private:
  void setOpen(bool open, double t);
  double panelWidth(Vec2d window) const;
  Vec2d toPanel(Vec2d px) const;
  void updateSingle(double t, const Subject& s);
  void updateGrid(double t, const Subject& s);
  std::optional<MapCamera::View> fit() const;
  std::optional<size_t> dotAt(Vec2d px) const;  // grid dot under window px

  MapSettings settings_;
  MapCamera cam_;
  bool open_ = false;
  Tween slideTween_;
  double slide_ = 0;
  Vec2d window_;
  bool captured_ = false;

  // What the map shows.
  bool starting_ = false;  // opened: place the camera on the next update
  bool single_ = false;
  std::optional<uint32_t> photo_;
  std::vector<Vec2d> dots_;
  std::vector<std::vector<uint32_t>> dotPhotos_;  // per dot (grid)
  std::vector<TripVertex> trip_;                  // grid, trip path on
  std::vector<TripVertex> trail_;                 // single photo, trip path on
  bool trailCurrent_ = false;                     // trail_.back() is the open photo
  std::optional<Vec2d> pointer_;                  // over the panel, not dragging
  std::optional<size_t> hoveredDot_, highlightedDot_;
  Vec2d pressAt_;
  std::vector<uint32_t> pressPhotos_, clicked_;
  double uiScale_ = 1;
  bool noDots_ = false;
  bool pulsePending_ = false;
  double pulseStart_ = -1e9;
  double lastGridMove_ = -1e9;
  bool refitDue_ = false;
  std::vector<Vec2d> fitted_;         // dots the view was last fitted to
  bool fitPaused_ = false;            // the user moved the map ...
  std::vector<Vec2d> pausedDots_;     // ... while these dots were shown

  // Errors: until the first good tile after opening, only offline results means no internet.
  bool probing_ = false;
  int probeOffline_ = 0;
  bool lostNoticed_ = false, refusedNoticed_ = false;
  std::deque<MapNotice> notices_;
};

}  // namespace pb
