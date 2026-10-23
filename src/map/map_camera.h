#pragma once

#include <optional>
#include <span>
#include <utility>

#include "core/geom.h"
#include "map/fly_path.h"
#include "view/motion.h"

namespace pb {

// View onto the Web Mercator world (normalized coordinates, see map/mercator.h) and the
// pointer handling of the map panel. Pure logic: times are seconds from any monotonic
// clock, positions are px relative to the panel's top-left corner.
class MapCamera {
 public:
  static constexpr double kFlyMinS = 0.6, kFlyMaxS = 3.0;  // flyTo duration: path length, clamped
  struct View {
    Vec2d center;
    double zoom = 2;
  };

  Vec2d center{0.5, 0.5};    // x kept in [0, 1), y clamped to [0, 1]
  double zoom = 2;           // fractional; the world is 256 * 2^zoom * uiScale px wide
  Vec2d viewport{800, 600};  // panel size, device px
  double uiScale = 1;        // display scale: the same zoom shows the same area at any density
  double minZoom = 2, maxZoom = 19;

  double worldPx() const;
  Vec2d worldToScreen(Vec2d w) const;
  Vec2d screenToWorld(Vec2d s) const;
  Rect visibleWorld() const;
  // Changes the zoom (clamped) keeping the world point under `screenPivot` in place.
  void zoomAbout(Vec2d screenPivot, double newZoom);
  void jumpTo(Vec2d worldCenter, double newZoom);  // stops all motion

  // Drag pans (release while moving to glide); a double click zooms in one level about
  // the cursor; the wheel zooms half a level per notch about the cursor, smoothly.
  void onPointerDown(Vec2d px, double t);
  void onPointerMove(Vec2d px, double t);
  void onPointerUp(Vec2d px, double t);
  void onWheel(Vec2d px, double notches);
  void update(double t, double dt);
  bool idle() const;  // nothing moving
  // Eased flight to a view along the van Wijk & Nuij path (zooming out on the way as the
  // distance needs), from the current state, even mid-flight. Takes the short way around
  // the world. A drag, wheel or jumpTo ends it.
  void flyTo(Vec2d worldCenter, double newZoom, double t);
  bool flying() const { return flight_.has_value(); }
  // View showing every point (world) inside the viewport inset by `padding` (fraction per
  // side), zoomed in at most to maxZoom; one point (or all equal) gets singleZoom. Empty: nullopt.
  static std::optional<View> fitView(std::span<const Vec2d> points, Vec2d viewport, double uiScale, double padding,
                                     double maxZoom, double singleZoom, double minZoom);
  // True once after the user moved the map (drag, wheel, double click).
  bool takeUserMoved() { return std::exchange(userMoved_, false); }

 private:
  void normalize();
  void applyZoom(Vec2d pivot, double newZoom);
  void stopMotion();
  double widthAt(double z) const;  // visible world width at zoom z

  struct Flight {
    FlyPath path;
    Vec2d target;
    double targetZoom, t0, duration;
  };
  std::optional<Flight> flight_;

  bool pressed_ = false, dragging_ = false;
  Vec2d pressPos_, lastPos_;
  VelocityTracker tracker_;
  Momentum momentum_;
  SmoothValue zoomSmooth_{2, 2};
  Vec2d zoomPivot_;
  double lastClickTime_ = -1e9;
  Vec2d lastClickPos_;
  bool userMoved_ = false;
};

}  // namespace pb
