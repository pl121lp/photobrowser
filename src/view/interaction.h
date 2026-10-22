#pragma once

#include <cstdint>
#include <optional>

#include "core/geom.h"
#include "layout/layout.h"
#include "view/camera.h"
#include "view/motion.h"

namespace pb {

enum class Key { Left, Right, Up, Down, ZoomIn, ZoomOut, Home, Enter, Escape };

// Turns pointer/wheel/key input into camera motion and whole-photo focus.
// Pure logic: times are seconds from any monotonic clock, positions are drawable px.
class Interaction {
 public:
  Interaction(Camera& camera, const Layout& layout);

  void setViewport(Vec2d px);
  void layoutChanged();  // call after Layout::build(); recomputes zoom limits
  void resetView();      // jump to the start view (center photo)

  void onPointerDown(Vec2d px, double t);
  void onPointerMove(Vec2d px, double t);
  void onPointerUp(Vec2d px, double t);
  void onWheel(Vec2d px, double notches);
  bool onKey(Key k, double t);  // false if unhandled (Escape when not focused)
  // Screen rect (px) where the whole photo is currently drawn; clicks outside it unfocus.
  void setFocusRect(const Rect& screen) { focusRect_ = screen; }
  void update(double t, double dt);

  std::optional<uint32_t> focused() const;
  double focusProgress() const { return focusProgress_; }  // 0 grid .. 1 whole photo
  bool idle() const;
  double minScale() const { return minScale_; }
  double maxScale() const { return maxScale_; }
  Vec2d cameraVelocityPx() const { return velocityPx_; }

 private:
  struct CameraTween {
    bool active = false;
    Tween x, y, logScale;
  };

  double startScale() const;
  void computeLimits();
  void glideTo(Vec2d center, double scale, double t);
  void stopMotion();
  void focus(uint32_t pos, double t);
  void unfocus();
  std::optional<uint32_t> nearestPosition(Vec2d world) const;
  void handleClick(Vec2d px, double t);

  Camera& cam_;
  const Layout& layout_;
  double minScale_ = 1, maxScale_ = 1;
  double now_ = 0;

  // Pointer
  bool pressed_ = false, dragging_ = false;
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

  Vec2d velocityPx_;
};

}  // namespace pb
