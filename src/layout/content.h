#pragma once

#include "core/geom.h"

namespace pb {

// Largest rect with aspect (w/h) `aspect`, centered inside `box`.
Rect fitRect(const Rect& box, double aspect);

// Centered sub-rect of the unit square that has aspect `targetAspect` when the
// image has aspect `imageAspect` (i.e. the visible part of a center crop).
UvRect cropUv(double imageAspect, double targetAspect);

// A photo shown whole inside a tile of another aspect, with bars on two sides.
struct Letterbox {
  UvRect content;  // where the photo sits in the tile (tile coordinates, 0..1)
  UvRect uv;       // image coordinates at the tile's edges (beyond 0..1 on the bar sides)
};
Letterbox letterbox(double imageAspect, double tileAspect);

struct FocusMorph {
  Rect rect;  // screen px
  UvRect uv;  // part of the image to show
};

// Whole-photo view transition: from the photo's square cell on screen (progress 0)
// to the whole photo fitted to the viewport with a 2% margin (progress 1).
// The shown image region always matches the rect's aspect, so nothing stretches.
FocusMorph focusMorph(const Rect& cellOnScreen, double imageAspect, Vec2d viewport, double progress);

}  // namespace pb
