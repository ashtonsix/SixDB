#include "policy.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <limits>

namespace loom_policy_spike {

bool valid_geometry(Geometry geometry) noexcept {
  return geometry.line_bytes >= 16 && std::has_single_bit(geometry.line_bytes) &&
         geometry.page_bytes >= std::max(std::size_t(64), geometry.line_bytes) &&
         geometry.page_bytes <= (std::size_t(1) << 30) && std::has_single_bit(geometry.page_bytes);
}

std::optional<unsigned> bind_code(Pattern pattern, std::size_t requested,
    std::size_t minimum, Geometry geometry) noexcept {
  if (unsigned(pattern) > 1 || !requested || !minimum || !valid_geometry(geometry)) return {};
  const auto extent = std::min(requested, minimum);
  unsigned index = 0;
  while (index + 1 < extent_classes.size() && extent >= extent_classes[index + 1]) ++index;
  return unsigned(pattern) * 8 + index;
}

std::size_t envelope_bytes(unsigned code) noexcept {
  assert(code < demand_count);
  return code % 8 ? extent_classes[code % 8] : 1;
}

std::size_t required_bytes(Procedure procedure, Geometry geometry) noexcept {
  switch (procedure) {
    case Procedure::none: return 0;
    case Procedure::near_one: case Procedure::distant_one: return 1;
    case Procedure::distant_two: return geometry.line_bytes + 1;
    case Procedure::distant_four: return 3 * geometry.line_bytes + 1;
    case Procedure::page_one: return geometry.page_bytes + geometry.line_bytes + 1;
    case Procedure::page_two: return 2 * geometry.page_bytes + geometry.line_bytes + 1;
    case Procedure::page_four: return 4 * geometry.page_bytes + geometry.line_bytes + 1;
    case Procedure::page_eight: return 8 * geometry.page_bytes + geometry.line_bytes + 1;
  }
  return std::numeric_limits<std::size_t>::max();
}

namespace {
Procedure smaller(Procedure procedure) noexcept {
  switch (procedure) {
    case Procedure::page_eight: return Procedure::page_four;
    case Procedure::page_four: return Procedure::page_two;
    case Procedure::page_two: return Procedure::page_one;
    case Procedure::page_one: return Procedure::distant_four;
    case Procedure::distant_four: return Procedure::distant_two;
    case Procedure::distant_two: return Procedure::distant_one;
    default: return Procedure::none;
  }
}
template<Procedure Kind>
[[gnu::always_inline]] inline void entry(const void* address, Geometry geometry) noexcept {
  issue<Kind>(address, geometry);
}
constexpr std::array<HintFunction, 9> entries{
  entry<Procedure::none>, entry<Procedure::near_one>, entry<Procedure::distant_one>,
  entry<Procedure::distant_two>, entry<Procedure::distant_four>, entry<Procedure::page_one>,
  entry<Procedure::page_two>, entry<Procedure::page_four>, entry<Procedure::page_eight>};
}

Policy make_policy(Profile profile, Pressure pressure) noexcept {
  assert(valid_geometry(profile.geometry));
  Policy result{profile, pressure, {}};
  for (unsigned code = 0; code < demand_count; ++code) {
    Procedure selected;
    if (code < 8) selected = pressure == Pressure::busy ? Procedure::distant_one : Procedure::near_one;
    else if (pressure == Pressure::busy) selected = Procedure::none;
    else if (!profile.training_accesses) selected = Procedure::distant_one;
    else if (code % 8 < 5) selected = profile.training_accesses <= 2 ? Procedure::distant_two : Procedure::distant_four;
    else selected = code % 8 == 5 ? Procedure::page_one : code % 8 == 6 ? Procedure::page_four : Procedure::page_eight;
    while (required_bytes(selected, profile.geometry) > envelope_bytes(code)) selected = smaller(selected);
    result.procedures[code] = selected;
  }
  return result;
}

const char* procedure_name(Procedure procedure) noexcept {
  constexpr std::array names{"none", "near-one", "distant-one", "distant-two", "distant-four",
    "page-one", "page-two", "page-four", "page-eight"};
  assert(unsigned(procedure) < names.size());
  return names[unsigned(procedure)];
}

HintFunction issue_function(Procedure procedure) noexcept {
  assert(unsigned(procedure) < entries.size());
  return entries[unsigned(procedure)];
}

} // namespace loom_policy_spike
