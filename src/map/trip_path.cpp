#include "map/trip_path.h"

#include <algorithm>
#include <cmath>

namespace pb {

std::vector<TripVertex> buildTripPath(std::vector<TripStop> stops, int64_t maxGapS) {
  std::vector<TripVertex> path;
  if (stops.size() < 2) return path;
  std::stable_sort(stops.begin(), stops.end(), [](const TripStop& a, const TripStop& b) { return a.takenAt < b.takenAt; });
  const int64_t first = stops.front().takenAt, span = stops.back().takenAt - first;
  int64_t prevAt = first;
  Vec2d prevWorld;  // as given, before unwrapping
  for (const TripStop& s : stops) {
    const bool gap = path.empty() || s.takenAt - prevAt > maxGapS;
    prevAt = s.takenAt;
    if (!gap && s.world == prevWorld) continue;
    Vec2d w = s.world;
    if (!gap) w.x -= std::round(w.x - path.back().world.x);  // the copy nearest the previous vertex
    prevWorld = s.world;
    path.push_back({w, span > 0 ? double(s.takenAt - first) / double(span) : 0.0, gap});
  }
  return path;
}

std::vector<TripSegment> tripSegments(std::span<const TripVertex> path, const std::function<Vec2d(Vec2d)>& toScreen,
                                      double minPx) {
  std::vector<TripSegment> segs;
  Vec2d last;
  double lastDate = 0;
  for (size_t i = 0; i < path.size(); ++i) {
    const Vec2d s = toScreen(path[i].world);
    const bool runEnd = i + 1 == path.size() || path[i + 1].breakBefore;
    if (!path[i].breakBefore) {
      if (!runEnd && length(s - last) < minPx) continue;
      segs.push_back({last, s, lastDate, path[i].date});
    }
    last = s;
    lastDate = path[i].date;
  }
  return segs;
}

std::array<float, 3> tripColor(double date) {
  static constexpr std::array<std::array<float, 3>, 3> kStops = {{
      {0.16f, 0.45f, 0.95f},  // blue
      {0.98f, 0.82f, 0.15f},  // yellow
      {0.90f, 0.15f, 0.12f},  // red
  }};
  const double x = std::clamp(date, 0.0, 1.0) * 2;
  const size_t i = std::min<size_t>(size_t(x), 1);
  const float f = float(x - double(i));
  std::array<float, 3> c;
  for (size_t k = 0; k < 3; ++k) c[k] = kStops[i][k] + (kStops[i + 1][k] - kStops[i][k]) * f;
  return c;
}

}  // namespace pb
