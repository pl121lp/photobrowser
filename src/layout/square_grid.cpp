#include "layout/square_grid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pb {

uint32_t squareGridCols(uint32_t count) {
  uint32_t cols = count == 0 ? 0 : static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(count))));
  // Guard against floating point: smallest cols with cols*cols >= count.
  while (cols > 0 && uint64_t(cols - 1) * (cols - 1) >= count) --cols;
  while (uint64_t(cols) * cols < count) ++cols;
  return cols;
}

SquareGridLayout::SquareGridLayout(double gap) : gap_(gap) {}

void SquareGridLayout::build(const LayoutInput& input) {
  const uint32_t count = input.count;
  count_ = count;
  cols_ = squareGridCols(count);
  rows_ = cols_ == 0 ? 0 : (count + cols_ - 1) / cols_;

  center_ = 0;
  const Vec2d mid = bounds().center();
  double best = std::numeric_limits<double>::max();
  for (uint32_t p = 0; p < count_; ++p) {
    const double d = length(place(p).cell.center() - mid);
    if (d < best - 1e-12) {
      best = d;
      center_ = p;
    }
  }
}

Rect SquareGridLayout::bounds() const { return {0, 0, double(cols_), double(rows_)}; }

Placement SquareGridLayout::place(uint32_t pos) const {
  const double c = pos % std::max(cols_, 1u);
  const double r = pos / std::max(cols_, 1u);
  const Rect cell{c, r, c + 1, r + 1};
  return {cell, cell.inset(gap_)};
}

void SquareGridLayout::visible(const Rect& world, std::vector<uint32_t>& out) const {
  if (count_ == 0) return;
  const auto clampIdx = [](double v, uint32_t hi) {
    return static_cast<uint32_t>(std::clamp(v, 0.0, double(hi)));
  };
  const uint32_t c0 = clampIdx(std::floor(world.x0), cols_);
  const uint32_t c1 = clampIdx(std::ceil(world.x1), cols_);
  const uint32_t r0 = clampIdx(std::floor(world.y0), rows_);
  const uint32_t r1 = clampIdx(std::ceil(world.y1), rows_);
  for (uint32_t r = r0; r < r1; ++r) {
    for (uint32_t c = c0; c < c1; ++c) {
      const uint32_t pos = r * cols_ + c;
      if (pos >= count_) return;
      out.push_back(pos);
    }
  }
}

std::optional<uint32_t> SquareGridLayout::hit(Vec2d world) const {
  if (world.x < 0 || world.y < 0 || world.x >= cols_ || world.y >= rows_) return std::nullopt;
  const uint32_t pos = uint32_t(world.y) * cols_ + uint32_t(world.x);
  if (pos >= count_ || !place(pos).photo.contains(world)) return std::nullopt;
  return pos;
}

}  // namespace pb
