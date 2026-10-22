#pragma once

#include <cstdint>
#include <memory>
#include <optional>
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

// Places photos on the world surface. Positions are indices into Catalog::order.
class Layout {
 public:
  virtual ~Layout() = default;
  virtual void build(uint32_t count) = 0;
  virtual uint32_t count() const = 0;
  virtual Rect bounds() const = 0;
  virtual Placement place(uint32_t pos) const = 0;
  // Appends the positions whose cells intersect `world`; cost is O(visible).
  virtual void visible(const Rect& world, std::vector<uint32_t>& out) const = 0;
  // Position whose photo rect contains `world`, if any.
  virtual std::optional<uint32_t> hit(Vec2d world) const = 0;
  virtual uint32_t centerPosition() const = 0;
  virtual ContentMode contentMode() const = 0;
};

std::unique_ptr<Layout> makeLayout(std::string_view name, double gap = 0.03);

}  // namespace pb
