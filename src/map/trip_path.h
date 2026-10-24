#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "core/geom.h"

namespace pb {

// Trip path ([map] trip_path): the visible photos joined in the order they were taken,
// coloured by date. Pure logic; positions in normalized world coordinates (mercator.h).

constexpr int64_t kTripMaxGapS = 10 * 3600;  // default: longer between two photos, the line breaks

struct TripStop {
  Vec2d world;
  int64_t takenAt = 0;  // seconds
};

struct TripVertex {
  Vec2d world;  // unwrapped: x may leave 0..1 so the line crosses the antimeridian the short way
  double date = 0;           // 0 (earliest stop) .. 1 (latest)
  bool breakBefore = false;  // starts a new run (the first vertex, or after a long gap)
  friend bool operator==(const TripVertex&, const TripVertex&) = default;
};

// Sorted by time (ties keep their order); photos in a row at one spot are one vertex.
std::vector<TripVertex> buildTripPath(std::vector<TripStop> stops, int64_t maxGapS = kTripMaxGapS);

struct TripSegment {
  Vec2d a, b;  // screen positions
  double dateA = 0, dateB = 0;
};

// The path's segments after `toScreen`; vertices closer than `minPx` to the last one kept
// are skipped (the last vertex of a run is always kept). Runs of one vertex draw nothing.
std::vector<TripSegment> tripSegments(std::span<const TripVertex> path, const std::function<Vec2d(Vec2d)>& toScreen,
                                      double minPx);

// sRGB colour for a date 0..1 (clamped): blue, yellow, red.
std::array<float, 3> tripColor(double date);

}  // namespace pb
