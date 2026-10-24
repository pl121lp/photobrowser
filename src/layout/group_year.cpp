#include "layout/group_year.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "layout/square_grid.h"

namespace pb {

namespace {
int yearOf(int64_t seconds) {
  using namespace std::chrono;
  const auto days = floor<std::chrono::days>(sys_seconds{std::chrono::seconds{seconds}});
  return int(year_month_day{days}.year());
}
}  // namespace

GroupYearLayout::GroupYearLayout(double gap) : gap_(gap) {}

void GroupYearLayout::build(const LayoutInput& input) {
  count_ = input.count;
  groups_.clear();
  labels_.clear();
  const bool dated = input.takenAt.size() == input.count;
  // Positions are in time order (either direction), so each year is one run.
  double x = 0, height = 0;
  for (uint32_t first = 0; first < count_;) {
    uint32_t end = count_;
    int year = 0;
    if (dated) {
      year = yearOf(input.takenAt[first]);
      end = first + 1;
      while (end < count_ && yearOf(input.takenAt[end]) == year) ++end;
    }
    Group g{first, end - first, squareGridCols(end - first), 0, x};
    g.rows = (g.count + g.cols - 1) / g.cols;
    groups_.push_back(g);
    if (dated) labels_.push_back({{x, 0, x + g.cols, double(g.rows)}, std::to_string(year), kLabelBand});
    height = std::max(height, double(g.rows));
    x += g.cols + kGroupGap;
    first = end;
  }
  const Rect cells = groups_.empty() ? Rect{} : Rect{0, 0, x - kGroupGap, height};
  bounds_ = cells;
  if (!labels_.empty()) bounds_.y0 = -kLabelBand;

  center_ = 0;
  const Vec2d mid = cells.center();
  double best = std::numeric_limits<double>::max();
  for (uint32_t p = 0; p < count_; ++p) {
    const double d = length(place(p).cell.center() - mid);
    if (d < best - 1e-12) {
      best = d;
      center_ = p;
    }
  }
}

const GroupYearLayout::Group& GroupYearLayout::groupOf(uint32_t pos) const {
  auto it = std::upper_bound(groups_.begin(), groups_.end(), pos,
                             [](uint32_t p, const Group& g) { return p < g.first; });
  return *(it - 1);
}

Placement GroupYearLayout::place(uint32_t pos) const {
  if (groups_.empty()) return {};
  const Group& g = groupOf(pos);
  const uint32_t local = pos - g.first;
  const double c = g.x0 + local % g.cols, r = local / g.cols;
  const Rect cell{c, r, c + 1, r + 1};
  return {cell, cell.inset(gap_)};
}

void GroupYearLayout::visible(const Rect& world, std::vector<uint32_t>& out) const {
  // First group whose right edge is past world.x0.
  auto it = std::upper_bound(groups_.begin(), groups_.end(), world.x0,
                             [](double x, const Group& g) { return x < g.x0 + g.cols; });
  const auto clampIdx = [](double v, uint32_t hi) {
    return static_cast<uint32_t>(std::clamp(v, 0.0, double(hi)));
  };
  for (; it != groups_.end() && it->x0 < world.x1; ++it) {
    const Group& g = *it;
    const uint32_t c0 = clampIdx(std::floor(world.x0 - g.x0), g.cols);
    const uint32_t c1 = clampIdx(std::ceil(world.x1 - g.x0), g.cols);
    const uint32_t r0 = clampIdx(std::floor(world.y0), g.rows);
    const uint32_t r1 = clampIdx(std::ceil(world.y1), g.rows);
    for (uint32_t r = r0; r < r1; ++r) {
      for (uint32_t c = c0; c < c1; ++c) {
        const uint32_t local = r * g.cols + c;
        if (local >= g.count) break;
        out.push_back(g.first + local);
      }
    }
  }
}

std::optional<uint32_t> GroupYearLayout::hit(Vec2d world) const {
  if (world.y < 0) return std::nullopt;
  auto it = std::upper_bound(groups_.begin(), groups_.end(), world.x,
                             [](double x, const Group& g) { return x < g.x0; });
  if (it == groups_.begin()) return std::nullopt;
  const Group& g = *(it - 1);
  const double lx = world.x - g.x0;
  if (lx >= g.cols || world.y >= g.rows) return std::nullopt;
  const uint32_t local = uint32_t(world.y) * g.cols + uint32_t(lx);
  if (local >= g.count || !place(g.first + local).photo.contains(world)) return std::nullopt;
  return g.first + local;
}

}  // namespace pb
