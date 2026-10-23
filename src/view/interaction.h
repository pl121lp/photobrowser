#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "core/geom.h"
#include "layout/layout.h"
#include "view/camera.h"
#include "view/motion.h"

namespace pb {

enum class Key { Left, Right, Up, Down, ZoomIn, ZoomOut, Home, Enter, Escape };

// Which photo the start view (and Home) centres on.
enum class StartAt { Center, Oldest, Newest, Random };
// Start view: Focus = about three photos across, Photo = the start photo open in the
// whole-photo view (arrows step on), Full = the whole library, centred on it.
enum class StartZoom { Focus, Photo, Full };
// Layout position for `at` among `count` photos. `newestFirst`: the layout runs newest
// to oldest. `randomValue`: any random number (used by Random).
uint32_t startPosition(StartAt at, uint32_t count, uint32_t centerPos, bool newestFirst, uint32_t randomValue);

// Turns pointer/wheel/key input into camera motion and whole-photo focus.
// Pure logic: times are seconds from any monotonic clock, positions are drawable px.
class Interaction {
 public:
  Interaction(Camera& camera, const Layout& layout, double maxPhotoZoom = 4.0);

  void setViewport(Vec2d px);
  void layoutChanged();  // call after Layout::build(); recomputes zoom limits
  void resetView();      // jump to the start view (the start position, default the centre photo)
  // Photo the start view and Home centre on; nullopt or out of range = centre photo.
  void setStartPosition(std::optional<uint32_t> pos) { start_ = pos; }
  void setStartZoom(StartZoom zoom) { startZoom_ = zoom; }

  void onPointerDown(Vec2d px, double t);
  void onPointerMove(Vec2d px, double t);
  void onPointerUp(Vec2d px, double t);
  void onSecondaryClick();
  void onWheel(Vec2d px, double notches);
  // false if unhandled (Escape when not focused). Arrows move `steps` photos (Shift: 5),
  // stopping at the library's edge.
  bool onKey(Key k, double t, int steps = 1);
  // Fitted screen rect (px) for the whole photo, before focused-photo zoom/pan.
  void setFocusRect(const Rect& screen);
  Rect focusDisplayRect() const;
  void update(double t, double dt);

  // Scripted moves (screensaver). flyTo: eased journey to `pos` that pulls back to show
  // the way and zooms in to the max grid zoom on arrival; longer distances take longer.
  void flyTo(uint32_t pos, double t);
  void openPhoto(uint32_t pos, double t) { focus(pos, t); }  // whole-photo view
  // Leaves whole-photo view; a zoomed photo eases back to fit instead of jumping.
  void closePhoto() { unfocus(true); }
  // Sets the open photo's zoom (1 = fit) at once, about the photo's centre.
  void setFocusZoom(double zoom);
  bool flying() const { return tween_.active; }  // an eased camera move is running

  // True once per click on the open photo in whole-photo view (e.g. play / pause).
  bool takeActivation() { return std::exchange(activated_, false); }

  // Shader effect on the open photo (keys 0-9; 0 = none). Ignored (false) unless a photo
  // is open or opening; leaving the whole-photo view resets it to 0.
  bool setPhotoEffect(int effect);
  int photoEffect() const { return photoEffect_; }

  std::optional<uint32_t> focused() const;
  // Like focused(), but only while opening or open, not while closing.
  std::optional<uint32_t> opened() const { return focusTarget_ ? focused_ : std::nullopt; }
  double focusProgress() const { return focusProgress_; }  // 0 grid .. 1 whole photo
  double focusZoom() const;
  bool idle() const;
  double minScale() const { return minScale_; }
  double maxScale() const { return maxScale_; }
  Vec2d cameraVelocityPx() const { return velocityPx_; }

 private:
  struct CameraTween {
    bool active = false;
    Tween x, y, logScale;
    double pullback = 0;  // log-scale dip at mid-move (<= 0): zoom out, then in
  };

  double startScale() const;
  Vec2d startCenter() const;
  uint32_t startPos() const;
  void computeLimits();
  void glideTo(Vec2d center, double scale, double t);
  void stopMotion();
  void focus(uint32_t pos, double t);
  void unfocus(bool smoothZoom = false);
  void resetFocusZoom(bool immediate);
  void clampFocusPan();
  void beginDrag();
  std::optional<uint32_t> nearestPosition(Vec2d world) const;
  // The photo in the row above (dir -1) or below (+1) `pos` closest to it horizontally.
  std::optional<uint32_t> closestInRow(uint32_t pos, int dir) const;
  void showFocused(uint32_t pos, double t);  // switch the whole-photo view to `pos`
  void handleClick(Vec2d px, double t);
  void clampCenter();  // keeps the view centered over the library

  Camera& cam_;
  const Layout& layout_;
  double minScale_ = 1, maxScale_ = 1;
  std::optional<uint32_t> start_;
  StartZoom startZoom_ = StartZoom::Focus;
  double now_ = 0;

  // Pointer
  bool pressed_ = false, dragging_ = false, focusDragging_ = false;
  Vec2d pressPos_, lastPos_;
  VelocityTracker tracker_;
  Momentum momentum_;
  std::optional<uint32_t> lastClickPos_;
  double lastClickTime_ = -1e9;

  // Zoom (log scale) about a pivot
  SmoothValue logZoom_;
  Vec2d zoomPivot_;

  CameraTween tween_;

  // Whole-photo focus
  std::optional<uint32_t> focused_;
  bool focusTarget_ = false;
  Tween focusTween_;
  double focusProgress_ = 0;
  Rect focusRect_;
  bool focusRectValid_ = false;
  double maxPhotoZoom_ = 4;
  SmoothValue focusLogZoom_;
  Vec2d focusZoomPivot_;
  Vec2d focusPan_;
  bool activated_ = false;
  int photoEffect_ = 0;

  Vec2d velocityPx_;
};

}  // namespace pb
