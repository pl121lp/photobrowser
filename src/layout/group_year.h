#pragma once

#include <vector>

#include "layout/layout.h"

namespace pb {

// One square grid per year (as SquareGridLayout), the years side by side left to right
// in layout order, top-aligned at y = 0, kGroupGap apart. Each year is labelled in a band
// above (bounds() includes it, so the whole-library view leaves room). Without dates
// (empty LayoutInput::takenAt) everything is one unlabelled group.
class GroupYearLayout final : public Layout {
 public:
  static constexpr double kGroupGap = 1.0;   // world units (photos) between year groups
  static constexpr double kLabelBand = 1.0;  // world units above the groups for the year labels

  explicit GroupYearLayout(double gap = 0.03);

  using Layout::build;
  void build(const LayoutInput& input) override;
  uint32_t count() const override { return count_; }
  Rect bounds() const override { return bounds_; }
  Placement place(uint32_t pos) const override;
  void visible(const Rect& world, std::vector<uint32_t>& out) const override;
  std::optional<uint32_t> hit(Vec2d world) const override;
  uint32_t centerPosition() const override { return center_; }
  ContentMode contentMode() const override { return ContentMode::Crop; }
  std::span<const LayoutLabel> labels() const override { return labels_; }

 private:
  struct Group {
    uint32_t first = 0, count = 0, cols = 0, rows = 0;
    double x0 = 0;  // left edge, world units
  };
  const Group& groupOf(uint32_t pos) const;

  double gap_;
  uint32_t count_ = 0, center_ = 0;
  Rect bounds_;
  std::vector<Group> groups_;  // left to right, ascending first and x0
  std::vector<LayoutLabel> labels_;
};

}  // namespace pb
