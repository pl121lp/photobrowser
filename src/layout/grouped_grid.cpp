#include "layout/grouped_grid.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>

#include "layout/square_grid.h"

namespace pb {

namespace {
std::chrono::year_month_day dateOf(int64_t seconds) {
  using namespace std::chrono;
  return year_month_day{floor<days>(sys_seconds{std::chrono::seconds{seconds}})};
}
int yearOf(int64_t seconds) { return int(dateOf(seconds).year()); }
unsigned monthOf(int64_t seconds) { return unsigned(dateOf(seconds).month()); }

// End of the run of positions from `first` whose `key` matches; positions are in time
// order (either direction), so each year or month is one run.
template <class Key>
uint32_t runEnd(std::span<const int64_t> taken, uint32_t first, uint32_t end, Key key) {
  const auto k = key(taken[first]);
  uint32_t e = first + 1;
  while (e < end && key(taken[e]) == k) ++e;
  return e;
}

constexpr std::array<const char*, 12> kMonths = {"January", "February", "March",     "April",   "May",      "June",
                                                 "July",    "August",   "September", "October", "November", "December"};
}  // namespace

GroupedGridLayout::Group GroupedGridLayout::makeGroup(uint32_t first, uint32_t count) {
  Group g{first, count, squareGridCols(count)};
  g.rows = g.cols ? (count + g.cols - 1) / g.cols : 0;
  return g;
}

void GroupedGridLayout::reset(uint32_t count) {
  count_ = count;
  groups_.clear();
  labels_.clear();
}

void GroupedGridLayout::finish() {
  Rect cells{};
  for (size_t i = 0; i < groups_.size(); ++i) {
    const Rect r = groups_[i].rect();
    cells = i == 0 ? r : Rect{std::min(cells.x0, r.x0), std::min(cells.y0, r.y0), std::max(cells.x1, r.x1),
                              std::max(cells.y1, r.y1)};
  }
  bounds_ = cells;
  for (const LayoutLabel& l : labels_) bounds_.y0 = std::min(bounds_.y0, l.group.y0 - l.space);

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

const GroupedGridLayout::Group& GroupedGridLayout::groupOf(uint32_t pos) const {
  auto it = std::upper_bound(groups_.begin(), groups_.end(), pos,
                             [](uint32_t p, const Group& g) { return p < g.first; });
  return *(it - 1);
}

Placement GroupedGridLayout::place(uint32_t pos) const {
  if (groups_.empty()) return {};
  const Group& g = groupOf(pos);
  const uint32_t local = pos - g.first;
  const double c = g.x0 + local % g.cols, r = g.y0 + local / g.cols;
  const Rect cell{c, r, c + 1, r + 1};
  return {cell, cell.inset(gap_)};
}

void GroupedGridLayout::visible(const Rect& world, std::vector<uint32_t>& out) const {
  const auto clampIdx = [](double v, uint32_t hi) {
    return static_cast<uint32_t>(std::clamp(v, 0.0, double(hi)));
  };
  for (const Group& g : groups_) {
    if (!g.rect().intersects(world)) continue;
    const uint32_t c0 = clampIdx(std::floor(world.x0 - g.x0), g.cols);
    const uint32_t c1 = clampIdx(std::ceil(world.x1 - g.x0), g.cols);
    const uint32_t r0 = clampIdx(std::floor(world.y0 - g.y0), g.rows);
    const uint32_t r1 = clampIdx(std::ceil(world.y1 - g.y0), g.rows);
    for (uint32_t r = r0; r < r1; ++r) {
      for (uint32_t c = c0; c < c1; ++c) {
        const uint32_t local = r * g.cols + c;
        if (local >= g.count) break;
        out.push_back(g.first + local);
      }
    }
  }
}

std::optional<uint32_t> GroupedGridLayout::hit(Vec2d world) const {
  for (const Group& g : groups_) {
    const double lx = world.x - g.x0, ly = world.y - g.y0;
    if (lx < 0 || ly < 0 || lx >= g.cols || ly >= g.rows) continue;
    const uint32_t local = uint32_t(ly) * g.cols + uint32_t(lx);
    if (local >= g.count || !place(g.first + local).photo.contains(world)) return std::nullopt;
    return g.first + local;
  }
  return std::nullopt;
}

void GroupYearLayout::build(const LayoutInput& input) {
  reset(input.count);
  const bool dated = input.takenAt.size() == input.count;
  double x = 0;
  for (uint32_t first = 0; first < input.count;) {
    const uint32_t end = dated ? runEnd(input.takenAt, first, input.count, yearOf) : input.count;
    Group g = makeGroup(first, end - first);
    g.x0 = x;
    addGroup(g);
    if (dated) addLabel(g.rect(), std::to_string(yearOf(input.takenAt[first])));
    x += g.cols + kGroupGap;
    first = end;
  }
  finish();
}

void GroupYearMonthLayout::build(const LayoutInput& input) {
  reset(input.count);
  if (input.takenAt.size() != input.count) {
    if (input.count) addGroup(makeGroup(0, input.count));
    finish();
    return;
  }
  double x = 0;
  for (uint32_t first = 0; first < input.count;) {
    const uint32_t end = runEnd(input.takenAt, first, input.count, yearOf);
    // The months first, to know the column's size; the year label goes before them.
    std::vector<Group> months;
    std::vector<unsigned> names;
    double y = 0, width = 0;
    for (uint32_t m = first; m < end;) {
      const uint32_t mEnd = runEnd(input.takenAt, m, end, monthOf);
      Group g = makeGroup(m, mEnd - m);
      g.x0 = x;
      g.y0 = y;
      months.push_back(g);
      names.push_back(monthOf(input.takenAt[m]));
      width = std::max(width, double(g.cols));
      y += g.rows + kLabelBand;
      m = mEnd;
    }
    addLabel({x, -kLabelBand, x + width, y - kLabelBand}, std::to_string(yearOf(input.takenAt[first])));
    for (size_t i = 0; i < months.size(); ++i) {
      addGroup(months[i]);
      addLabel(months[i].rect(), kMonths[names[i] - 1]);
    }
    x += width + kGroupGap;
    first = end;
  }
  finish();
}

}  // namespace pb
