#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/geom.h"

namespace pb {

// How a photo fills its cell: Crop fills it (center crop), Fit letterboxes it.
enum class ContentMode { Crop, Fit };

struct Placement {
  Rect cell;   // full cell in world units
  Rect photo;  // cell inset by the gap: where the photo is drawn
};

// What a layout may arrange photos by, per position (Catalog::order). Fields other
// than `count` may be empty (unknown); layouts must cope. Future layouts (e.g. a
// cluster layout) add fields here, such as GPS positions.
struct LayoutInput {
  uint32_t count = 0;
  std::span<const int64_t> takenAt;  // PhotoRecord::takenAt per position
};

// A heading over a group of photos (e.g. a year), drawn above its top-left corner.
struct LayoutLabel {
  Rect group;  // the group's cells, world units
  std::string text;
  double space = 0;  // world units kept free above the group for the heading
};

// Places photos on the world surface. Positions are indices into Catalog::order, so
// they stay valid when switching layouts; only their world rects change.
class Layout {
 public:
  virtual ~Layout() = default;
  virtual void build(const LayoutInput& input) = 0;
  void build(uint32_t count) { build(LayoutInput{count, {}}); }
  virtual uint32_t count() const = 0;
  virtual Rect bounds() const = 0;
  virtual Placement place(uint32_t pos) const = 0;
  // Appends the positions whose cells intersect `world`; cost is O(visible).
  virtual void visible(const Rect& world, std::vector<uint32_t>& out) const = 0;
  // Position whose photo rect contains `world`, if any.
  virtual std::optional<uint32_t> hit(Vec2d world) const = 0;
  virtual uint32_t centerPosition() const = 0;
  virtual ContentMode contentMode() const = 0;
  // Group headings, in priority order (where two would overlap on screen, the earlier one
  // wins; e.g. a year before its months); none for an ungrouped layout.
  virtual std::span<const LayoutLabel> labels() const { return {}; }
};

// Layout modes by name, in F4 cycling order; the first is the default.
std::span<const std::string_view> layoutNames();
// The name `name` stands for (old aliases resolved), or empty if unknown.
std::string_view canonicalLayoutName(std::string_view name);
// The mode after `current` in cycling order (wrapping); the default for an unknown one.
std::string_view nextLayoutName(std::string_view current);
// nullptr for an unknown name.
std::unique_ptr<Layout> makeLayout(std::string_view name, double gap = 0.03);

}  // namespace pb
