#include "map/mercator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pb {

Vec2d toWorld(double lat, double lon) {
  const double phi = std::clamp(lat, -kMaxMercatorLat, kMaxMercatorLat) * std::numbers::pi / 180;
  const double y = (1 - std::log(std::tan(phi) + 1 / std::cos(phi)) / std::numbers::pi) / 2;
  return {(lon + 180) / 360, std::clamp(y, 0.0, 1.0)};
}

std::optional<Vec2d> toWorld(const GeoPos& p) {
  if (!std::isfinite(p.lat) || !std::isfinite(p.lon)) return std::nullopt;
  return toWorld(p.lat, p.lon);
}

GeoPos toLatLon(Vec2d world) {
  const double x = world.x - std::floor(world.x);
  const double n = std::numbers::pi * (1 - 2 * world.y);
  return {std::atan(std::sinh(n)) * 180 / std::numbers::pi, x * 360 - 180, std::nullopt};
}

int wrapTileX(int x, int z) {
  const int n = 1 << z;
  return ((x % n) + n) % n;
}

std::vector<TileCover> tilesCovering(const Rect& world, int z) {
  std::vector<TileCover> out;
  const int n = 1 << z;
  const double size = 1.0 / n;
  // Half-open: a view edge exactly on a tile edge does not take in the next tile.
  const int x0 = int(std::floor(world.x0 * n)), x1 = int(std::ceil(world.x1 * n)) - 1;
  const int y0 = std::max(0, int(std::floor(world.y0 * n))), y1 = std::min(n - 1, int(std::ceil(world.y1 * n)) - 1);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      out.push_back({{z, wrapTileX(x, z), y}, {x * size, y * size, (x + 1) * size, (y + 1) * size}});
    }
  }
  return out;
}

int tileLevel(double zoom, double uiScale, int minZ, int maxZ) {
  const int level = int(std::ceil(zoom + std::log2(std::max(uiScale, 1e-6)) - std::log2(1.4)));
  return std::clamp(level, minZ, maxZ);
}

}  // namespace pb
