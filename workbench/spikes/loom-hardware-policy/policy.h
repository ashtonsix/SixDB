#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace loom_policy_spike {

struct Geometry { std::size_t line_bytes{64}, page_bytes{4096}; };
enum class Pattern : unsigned { irregular, linear };
enum class Procedure : std::uint8_t {
  none, near_one, distant_one, distant_two, distant_four,
  page_one, page_two, page_four, page_eight
};
enum class Pressure : unsigned { low, busy };
inline constexpr unsigned demand_count = 16;
inline constexpr std::array<std::size_t, 8> extent_classes{
  64, 128, 256, 512, 1024, 8192, 32768, 65536};

struct Profile {
  std::uint64_t revision{};
  Geometry geometry;
  unsigned preferred_width{8};
  /// Zero preserves an unknown training observation, rather than asserting zero cost.
  unsigned training_accesses{};
};

struct Policy {
  Profile profile;
  Pressure pressure{};
  std::array<Procedure, demand_count> procedures{};
};

bool valid_geometry(Geometry) noexcept;
/// Cold binding. Every later dependency must retain the admitted minimum span.
/// Class zero admits one occupied address; other classes retain their byte floor.
std::optional<unsigned> bind_code(Pattern, std::size_t requested_bytes,
    std::size_t minimum_bytes, Geometry) noexcept;
std::size_t envelope_bytes(unsigned code) noexcept;
std::size_t required_bytes(Procedure, Geometry) noexcept;
Policy make_policy(Profile, Pressure) noexcept;
const char* procedure_name(Procedure) noexcept;

namespace detail {
template<unsigned Locality, std::size_t... I>
[[gnu::always_inline]] inline void lines(const char* base, std::size_t stride,
    std::index_sequence<I...>) noexcept {
  (__builtin_prefetch(base + I * stride, 0, Locality), ...);
}
template<std::size_t... I>
[[gnu::always_inline]] inline void pages(const char* page, Geometry geometry,
    std::index_sequence<I...>) noexcept {
  ((__builtin_prefetch(page + I * geometry.page_bytes, 0, 2),
    __builtin_prefetch(page + I * geometry.page_bytes + geometry.line_bytes, 0, 2)), ...);
}
}

/// Targets are admitted before entry. Each instantiated body is fixed and unrolled.
template<Procedure Kind>
[[gnu::always_inline]] inline void issue(const void* address, Geometry geometry) noexcept {
  const auto* base = static_cast<const char*>(address);
  if constexpr (Kind == Procedure::near_one) detail::lines<3>(base, geometry.line_bytes, std::make_index_sequence<1>{});
  else if constexpr (Kind == Procedure::distant_one) detail::lines<2>(base, geometry.line_bytes, std::make_index_sequence<1>{});
  else if constexpr (Kind == Procedure::distant_two) detail::lines<2>(base, geometry.line_bytes, std::make_index_sequence<2>{});
  else if constexpr (Kind == Procedure::distant_four) detail::lines<2>(base, geometry.line_bytes, std::make_index_sequence<4>{});
  else if constexpr (Kind >= Procedure::page_one) {
    constexpr auto count = Kind == Procedure::page_one ? 1 : Kind == Procedure::page_two ? 2 :
                           Kind == Procedure::page_four ? 4 : 8;
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    const auto next = (value & ~(geometry.page_bytes - 1)) + geometry.page_bytes;
    __builtin_prefetch(base, 0, 3);
    detail::pages(reinterpret_cast<const char*>(next), geometry, std::make_index_sequence<count>{});
  }
}

using HintFunction = void (*)(const void*, Geometry) noexcept;
HintFunction issue_function(Procedure) noexcept;

/// The LUT selects a code, not a separately called procedure. Admission proves
/// the code and every possible target before this inline switch is entered.
[[gnu::always_inline]] inline void issue_selected(Procedure selected,
    const void* address, Geometry geometry) noexcept {
  switch (selected) {
    case Procedure::none: return issue<Procedure::none>(address, geometry);
    case Procedure::near_one: return issue<Procedure::near_one>(address, geometry);
    case Procedure::distant_one: return issue<Procedure::distant_one>(address, geometry);
    case Procedure::distant_two: return issue<Procedure::distant_two>(address, geometry);
    case Procedure::distant_four: return issue<Procedure::distant_four>(address, geometry);
    case Procedure::page_one: return issue<Procedure::page_one>(address, geometry);
    case Procedure::page_two: return issue<Procedure::page_two>(address, geometry);
    case Procedure::page_four: return issue<Procedure::page_four>(address, geometry);
    case Procedure::page_eight: return issue<Procedure::page_eight>(address, geometry);
  }
  __builtin_unreachable();
}

} // namespace loom_policy_spike
