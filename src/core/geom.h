#pragma once

#include <cmath>

// Coordinate convention everywhere: origin top-left, +y down (screen and world).
namespace pb {

struct Vec2d {
  double x = 0, y = 0;

  friend Vec2d operator+(Vec2d a, Vec2d b) { return {a.x + b.x, a.y + b.y}; }
  friend Vec2d operator-(Vec2d a, Vec2d b) { return {a.x - b.x, a.y - b.y}; }
  friend Vec2d operator*(Vec2d a, double s) { return {a.x * s, a.y * s}; }
  friend Vec2d operator/(Vec2d a, double s) { return {a.x / s, a.y / s}; }
  friend bool operator==(Vec2d a, Vec2d b) = default;
};

inline double length(Vec2d v) { return std::hypot(v.x, v.y); }

struct Rect {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;

  double width() const { return x1 - x0; }
  double height() const { return y1 - y0; }
  Vec2d center() const { return {(x0 + x1) * 0.5, (y0 + y1) * 0.5}; }
  bool contains(Vec2d p) const { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
  bool intersects(const Rect& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
  Rect inset(double d) const { return {x0 + d, y0 + d, x1 - d, y1 - d}; }
  friend bool operator==(const Rect&, const Rect&) = default;
};

struct UvRect {
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};

}  // namespace pb
