#pragma once

#include "core/geom.h"

namespace pb {

// Largest rect with aspect (w/h) `aspect`, centered inside `box`.
Rect fitRect(const Rect& box, double aspect);

// Centered sub-rect of the unit square that has aspect `targetAspect` when the
// image has aspect `imageAspect` (i.e. the visible part of a center crop).
UvRect cropUv(double imageAspect, double targetAspect);

}  // namespace pb
