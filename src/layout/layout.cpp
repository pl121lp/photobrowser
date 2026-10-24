#include "layout/layout.h"

#include <algorithm>
#include <array>

#include "layout/grouped_grid.h"
#include "layout/square_grid.h"

namespace pb {

namespace {
// To add a layout: append its name here and construct it in makeLayout().
constexpr std::array<std::string_view, 3> kNames = {"square", "group_year", "group_year_month"};
}  // namespace

std::span<const std::string_view> layoutNames() { return kNames; }

std::string_view canonicalLayoutName(std::string_view name) {
  if (name == "square-crop") return "square";  // the name before layouts were switchable
  auto it = std::find(kNames.begin(), kNames.end(), name);
  return it == kNames.end() ? std::string_view{} : *it;
}

std::string_view nextLayoutName(std::string_view current) {
  auto it = std::find(kNames.begin(), kNames.end(), canonicalLayoutName(current));
  if (it == kNames.end() || ++it == kNames.end()) return kNames.front();
  return *it;
}

std::unique_ptr<Layout> makeLayout(std::string_view name, double gap) {
  name = canonicalLayoutName(name);
  if (name == "square") return std::make_unique<SquareGridLayout>(gap);
  if (name == "group_year") return std::make_unique<GroupYearLayout>(gap);
  if (name == "group_year_month") return std::make_unique<GroupYearMonthLayout>(gap);
  return nullptr;
}

}  // namespace pb
