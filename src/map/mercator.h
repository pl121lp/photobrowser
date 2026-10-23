#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/geom.h"
#include "library/exif.h"

namespace pb {

// Web Mercator (EPSG:3857) in normalized world coordinates: x 0..1 west to east from
// longitude -180, y 0..1 north to south. Tiles are 256 px, XYZ scheme (y from the north).
constexpr double kMaxMercatorLat = 85.0511287798;
constexpr double kTilePx = 256;

Vec2d toWorld(double lat, double lon);          // latitude clamped to +-kMaxMercatorLat
std::optional<Vec2d> toWorld(const GeoPos& p);  // nullopt if latitude or longitude is not finite
GeoPos toLatLon(Vec2d world);                   // x taken modulo 1

struct TileKey {
  int z = 0, x = 0, y = 0;  // x wrapped into [0, 2^z), y in [0, 2^z)
  uint64_t packed() const { return uint64_t(z) << 56 | uint64_t(uint32_t(x)) << 28 | uint64_t(uint32_t(y)); }
  friend bool operator==(TileKey, TileKey) = default;
};

int wrapTileX(int x, int z);  // modulo 2^z, in [0, 2^z)

struct TileCover {
  TileKey key;
  Rect world;  // where the tile is drawn: unwrapped, may lie left of 0 or right of 1
};
// Tiles at level z intersecting `world` (x unbounded, y clipped to 0..1), row by row.
std::vector<TileCover> tilesCovering(const Rect& world, int z);

// Level whose texels are closest to device pixels, magnified at most ~1.4x, for a map
// whose world is 256 * 2^zoom * uiScale px wide; clamped to [minZ, maxZ].
int tileLevel(double zoom, double uiScale, int minZ, int maxZ);

}  // namespace pb
