#include <loom/hints.h>
#include <algorithm>
#include <bit>
#include <cassert>

namespace loom {
bool valid_hint_geometry(HintGeometry geometry) noexcept {
  return geometry.line_bytes >= 16 && std::has_single_bit(geometry.line_bytes) &&
      geometry.page_bytes >= std::max(std::size_t(64), geometry.line_bytes) &&
      geometry.page_bytes <= (std::size_t(1) << 30) && std::has_single_bit(geometry.page_bytes);
}
bool valid_hint_profile(HintProfile profile) noexcept {
  return valid_hint_geometry(profile.geometry) && profile.preferred_width &&
      profile.preferred_width <= 64 && (!profile.training_accesses ||
      (profile.training_accesses >= 2 && profile.training_accesses <= 64));
}
orbital::Result<DemandClass> bind_demand(MemoryDemand demand, std::size_t minimum) noexcept {
  if (unsigned(demand.pattern) > 1 || !demand.bytes || !minimum)
    return std::unexpected(orbital::Error::invalid_argument);
  const auto extent = std::min(demand.bytes, minimum);
  unsigned index = 0;
  while (index + 1 < demand_extents.size() && extent >= demand_extents[index + 1]) ++index;
  return DemandClass(unsigned(demand.pattern) * 8 + index);
}
orbital::Result<MemoryDependency> bind_dependency(MemoryDemand demand,
    std::span<const std::byte> qualified) noexcept {
  if (!qualified.data()) return std::unexpected(orbital::Error::invalid_argument);
  auto code = bind_demand(demand, qualified.size());
  if (!code) return std::unexpected(code.error());
  return MemoryDependency{qualified.data(), *code};
}
namespace {
std::size_t lines(Hint hint) {
  switch (hint) {
    case Hint::l1_8: case Hint::l2_8: return 8;
    case Hint::l1_l2_4: return 5;
    case Hint::l1_4: case Hint::l2_4: return 4;
    case Hint::l1_l2_2: return 3;
    case Hint::l1_2: case Hint::l2_2: return 2;
    default: return 1;
  }
}
// One maximum-target proof for precise fixed bindings and worst-alignment
// abstract classes. Geometry is validated before bounded arithmetic here.
bool fits(Hint hint, std::size_t bytes, HintGeometry geometry, std::size_t next_page) {
  if (hint == Hint::none) return true;
  if (!bytes) return false;
  const auto pages = detail::hint_pages(hint);
  const auto last = pages ? next_page + (pages - 1) * geometry.page_bytes + geometry.line_bytes :
      (lines(hint) - 1) * geometry.line_bytes;
  return last < bytes;
}
Hint smaller(Hint hint) {
  switch (hint) {
    case Hint::l1_8: return Hint::l1_4;
    case Hint::l2_8: return Hint::l2_4;
    case Hint::l1_4: return Hint::l1_2;
    case Hint::l1_2: return Hint::l1_1;
    case Hint::l2_2: return Hint::l2_1;
    case Hint::l1_l2_4: return Hint::l1_l2_2;
    case Hint::l1_l2_2: return Hint::l1_1;
    case Hint::page8: return Hint::page4;
    case Hint::page4: return Hint::page2;
    case Hint::page2: return Hint::page1;
    case Hint::page1: return Hint::l2_4;
    case Hint::l2_4: return Hint::l2_2;
    default: return Hint::none;
  }
}
}
orbital::Result<BoundDependency> bind_dependency(Hint requested,
    std::span<const std::byte> bytes, std::size_t page_bytes) noexcept {
  const HintGeometry geometry{64, page_bytes};
  if (unsigned(requested) >= 16 || !valid_hint_geometry(geometry) || (!bytes.data() && !bytes.empty()))
    return std::unexpected(orbital::Error::invalid_argument);
  const auto next_page = page_bytes - (reinterpret_cast<std::uintptr_t>(bytes.data()) & (page_bytes - 1));
  // Fixed page controls originally downgrade to first-line; abstract policy
  // can use a line bootstrap, still proved by this same footprint calculation.
  while (!fits(requested, bytes.size(), geometry, next_page))
    requested = requested == Hint::page1 ? Hint::l1_1 : smaller(requested);
  return BoundDependency{bytes.data(), requested};
}
orbital::Result<Hint> bind_hint_shape(Hint requested,
    std::size_t minimum_bytes, std::size_t page_bytes) noexcept {
  const HintGeometry geometry{64, page_bytes};
  if (unsigned(requested) >= 16 || !valid_hint_geometry(geometry))
    return std::unexpected(orbital::Error::invalid_argument);
  while (!fits(requested, minimum_bytes, geometry, page_bytes))
    requested = requested == Hint::page1 ? Hint::l1_1 : smaller(requested);
  return requested;
}
HintPolicy make_hint_policy(HintProfile profile, HintPressure pressure) noexcept {
  assert(valid_hint_profile(profile) && unsigned(pressure) <= 1);
  HintPolicy result{profile, pressure, {}};
  for (unsigned code = 0; code < 16; ++code) {
    Hint selected;
    if (code < 8) selected = pressure == HintPressure::busy ? Hint::l2_1 : Hint::l1_1;
    else if (pressure == HintPressure::busy) selected = Hint::none;
    else if (!profile.training_accesses) selected = Hint::l2_1;
    else if (code % 8 < 5) selected = profile.training_accesses <= 2 ? Hint::l2_2 : Hint::l2_4;
    else selected = code % 8 == 5 ? Hint::page1 : code % 8 == 6 ? Hint::page4 : Hint::page8;
    const auto bytes = code % 8 ? demand_extents[code % 8] : 1;
    while (!fits(selected, bytes, profile.geometry, profile.geometry.page_bytes)) selected = smaller(selected);
    result.codes[code] = selected;
  }
  return result;
}
} // namespace loom
