#pragma once

#include <vector>

#include "layout/layout.h"

namespace pb {

// Photos split into groups of consecutive positions, each group a square grid (as
// SquareGridLayout) placed somewhere on the surface. Subclasses decide the groups and
// where they go (build()); placement, visibility and hit testing are shared. Visibility
// and hit testing scan the groups: O(groups + visible).
class GroupedGridLayout : public Layout {
 public:
  static constexpr double kGroupGap = 1.0;   // world units (photos) between groups side by side
  static constexpr double kLabelBand = 1.0;  // world units kept above a group for its heading

  explicit GroupedGridLayout(double gap) : gap_(gap) {}

  uint32_t count() const override { return count_; }
  Rect bounds() const override { return bounds_; }
  Placement place(uint32_t pos) const override;
  void visible(const Rect& world, std::vector<uint32_t>& out) const override;
  std::optional<uint32_t> hit(Vec2d world) const override;
  uint32_t centerPosition() const override { return center_; }
  ContentMode contentMode() const override { return ContentMode::Crop; }
  std::span<const LayoutLabel> labels() const override { return labels_; }

 protected:
  struct Group {
    uint32_t first = 0, count = 0, cols = 0, rows = 0;
    double x0 = 0, y0 = 0;  // top-left corner, world units
    Rect rect() const { return {x0, y0, x0 + cols, y0 + rows}; }
  };
  // A group of `count` positions from `first` with its grid size; place it, then addGroup().
  static Group makeGroup(uint32_t first, uint32_t count);
  void reset(uint32_t count);
  void addGroup(const Group& g) { groups_.push_back(g); }
  void addLabel(const Rect& group, std::string text) { labels_.push_back({group, std::move(text), kLabelBand}); }
  // Call at the end of build(): bounds = all groups plus the heading bands; centre position.
  void finish();

 private:
  const Group& groupOf(uint32_t pos) const;

  double gap_;
  uint32_t count_ = 0, center_ = 0;
  Rect bounds_;
  std::vector<Group> groups_;  // ascending `first`
  std::vector<LayoutLabel> labels_;
};

// One grid per year, the years side by side left to right in layout order, top-aligned at
// y = 0, kGroupGap apart, each labelled with its year in a band above. Without dates
// (empty LayoutInput::takenAt) everything is one unlabelled group.
class GroupYearLayout final : public GroupedGridLayout {
 public:
  explicit GroupYearLayout(double gap = 0.03) : GroupedGridLayout(gap) {}
  using Layout::build;
  void build(const LayoutInput& input) override;
};

// A column per year, side by side as in GroupYearLayout (a year band on top), each column
// a stack of one grid per month, left-aligned, each under its own month-name band.
// Without dates, one unlabelled group.
class GroupYearMonthLayout final : public GroupedGridLayout {
 public:
  explicit GroupYearMonthLayout(double gap = 0.03) : GroupedGridLayout(gap) {}
  using Layout::build;
  void build(const LayoutInput& input) override;
};

}  // namespace pb
