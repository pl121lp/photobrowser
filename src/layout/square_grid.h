#pragma once

#include "layout/layout.h"

namespace pb {

// Square grid, cols = ceil(sqrt(N)), filled left to right, row by row.
// Each cell is 1x1 world units; the photo is inset by `gap` on every side.
class SquareGridLayout final : public Layout {
 public:
  explicit SquareGridLayout(double gap = 0.03);

  void build(uint32_t count) override;
  uint32_t count() const override { return count_; }
  Rect bounds() const override;
  Placement place(uint32_t pos) const override;
  void visible(const Rect& world, std::vector<uint32_t>& out) const override;
  std::optional<uint32_t> hit(Vec2d world) const override;
  uint32_t centerPosition() const override { return center_; }
  ContentMode contentMode() const override { return ContentMode::Crop; }

  uint32_t cols() const { return cols_; }
  uint32_t rows() const { return rows_; }

 private:
  double gap_;
  uint32_t count_ = 0, cols_ = 0, rows_ = 0, center_ = 0;
};

}  // namespace pb
