#pragma once

// Callers describe qualified memory. Loom selects bounded hint bodies from a
// coarse owner-local hardware/pressure snapshot; hints grant no read rights.

#include <orbital/types.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace loom {

/// Fixed mechanism controls. Normal callers use MemoryDemand instead.
enum class Hint : std::uint8_t {
  none, l1_1, l2_1, l3_1, l1_2, l2_2, l1_4, l2_4,
  l1_8, l2_8, l1_l2_2, l1_l2_4, page1, page2, page4, page8
};

struct HintGeometry {
  std::size_t line_bytes{64}, page_bytes{4096};
  friend bool operator==(HintGeometry, HintGeometry) = default;
};
struct HintProfile {
  std::uint64_t revision{};
  HintGeometry geometry;
  unsigned preferred_width{8}; ///< Scheduling candidate, not a hardware resource count.
  unsigned training_accesses{}; ///< Zero means unknown.
};
enum class HintPressure : unsigned { low, busy };
enum class MemoryPattern : unsigned { irregular, linear };
struct MemoryDemand { MemoryPattern pattern; std::size_t bytes; };

inline constexpr std::array<std::size_t, 8> demand_extents{
  64, 128, 256, 512, 1024, 8192, 32768, 65536};

/// Established once against the minimum contiguous qualified window at every
/// later address. Class zero needs one occupied byte; no hint crosses a gap.
class DemandClass {
  std::uint8_t code_{16};
  explicit constexpr DemandClass(unsigned code) : code_(code) {}
  friend orbital::Result<DemandClass> bind_demand(MemoryDemand, std::size_t) noexcept;
public:
  constexpr DemandClass() = default; ///< No dependency/hints.
  constexpr unsigned code() const noexcept { return code_; }
  constexpr std::size_t minimum_bytes() const noexcept {
    return code_ == 16 ? 0 : code_ % 8 ? demand_extents[code_ % 8] : 1;
  }
};

struct MemoryDependency { const void* address{}; DemandClass demand; };
struct BoundDependency { const void* address{}; Hint hint{Hint::none}; };

[[nodiscard]] bool valid_hint_geometry(HintGeometry) noexcept;
[[nodiscard]] bool valid_hint_profile(HintProfile) noexcept;
[[nodiscard]] orbital::Result<DemandClass> bind_demand(MemoryDemand,
    std::size_t minimum_bytes) noexcept;
[[nodiscard]] orbital::Result<MemoryDependency> bind_dependency(MemoryDemand,
    std::span<const std::byte> qualified) noexcept;
/// Fixed controls retain their original 64-byte stride; match the Ring's page geometry.
[[nodiscard]] orbital::Result<BoundDependency> bind_dependency(Hint,
    std::span<const std::byte> qualified, std::size_t page_bytes = 4096) noexcept;
[[nodiscard]] orbital::Result<Hint> bind_hint_shape(Hint,
    std::size_t minimum_bytes, std::size_t page_bytes = 4096) noexcept;

struct HintPolicy {
  HintProfile profile;
  HintPressure pressure{HintPressure::low};
  std::array<Hint, 17> codes{}; ///< Entry 16 is the empty dependency.
};
/// Cold construction; every plan fits the class under every page alignment.
[[nodiscard]] HintPolicy make_hint_policy(HintProfile, HintPressure) noexcept;

namespace detail {
constexpr unsigned hint_pages(Hint kind) {
  switch (kind) {
    case Hint::page1: return 1;
    case Hint::page2: return 2;
    case Hint::page4: return 4;
    case Hint::page8: return 8;
    default: return 0;
  }
}
template<unsigned Locality, std::size_t... I>
[[gnu::always_inline]] inline void hint_lines(const char* base, std::size_t stride,
    std::index_sequence<I...>) noexcept {
  (__builtin_prefetch(base + I * stride, 0, Locality), ...);
}
template<std::size_t... I>
[[gnu::always_inline]] inline void hint_pages(const char* page, HintGeometry geometry,
    std::index_sequence<I...>) noexcept {
  ((__builtin_prefetch(page + I * geometry.page_bytes, 0, 2),
    __builtin_prefetch(page + I * geometry.page_bytes + geometry.line_bytes, 0, 2)), ...);
}
} // namespace detail

template<Hint Kind>
[[gnu::always_inline]] inline void prefetch(const void* address,
    std::size_t page_bytes = 4096, std::size_t line_bytes = 64) noexcept {
  const auto* base = static_cast<const char*>(address);
  if constexpr (Kind == Hint::l1_1) detail::hint_lines<3>(base, line_bytes, std::make_index_sequence<1>{});
  else if constexpr (Kind == Hint::l2_1) detail::hint_lines<2>(base, line_bytes, std::make_index_sequence<1>{});
  else if constexpr (Kind == Hint::l3_1) detail::hint_lines<1>(base, line_bytes, std::make_index_sequence<1>{});
  else if constexpr (Kind == Hint::l1_2) detail::hint_lines<3>(base, line_bytes, std::make_index_sequence<2>{});
  else if constexpr (Kind == Hint::l2_2) detail::hint_lines<2>(base, line_bytes, std::make_index_sequence<2>{});
  else if constexpr (Kind == Hint::l1_4) detail::hint_lines<3>(base, line_bytes, std::make_index_sequence<4>{});
  else if constexpr (Kind == Hint::l2_4) detail::hint_lines<2>(base, line_bytes, std::make_index_sequence<4>{});
  else if constexpr (Kind == Hint::l1_8) detail::hint_lines<3>(base, line_bytes, std::make_index_sequence<8>{});
  else if constexpr (Kind == Hint::l2_8) detail::hint_lines<2>(base, line_bytes, std::make_index_sequence<8>{});
  else if constexpr (Kind == Hint::l1_l2_2) {
    __builtin_prefetch(base, 0, 3);
    detail::hint_lines<2>(base + line_bytes, line_bytes, std::make_index_sequence<2>{});
  } else if constexpr (Kind == Hint::l1_l2_4) {
    __builtin_prefetch(base, 0, 3);
    detail::hint_lines<2>(base + line_bytes, line_bytes, std::make_index_sequence<4>{});
  } else if constexpr (detail::hint_pages(Kind)) {
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    const auto following = (value & ~(page_bytes - 1)) + page_bytes;
    __builtin_prefetch(base, 0, 3);
    detail::hint_pages(reinterpret_cast<const char*>(following), {line_bytes, page_bytes},
                       std::make_index_sequence<detail::hint_pages(Kind)>{});
  }
}

/// The admitted class and immutable snapshot prove every target. Only code
/// selection remains: no size/null checks, pressure queries, clocks or helper calls.
[[gnu::always_inline]] inline void prefetch_selected(Hint hint, const void* address,
    HintGeometry geometry) noexcept {
#define LOOM_HINT_CASE(KIND) case Hint::KIND: return prefetch<Hint::KIND>(address, geometry.page_bytes, geometry.line_bytes)
  switch (hint) {
    LOOM_HINT_CASE(none); LOOM_HINT_CASE(l1_1); LOOM_HINT_CASE(l2_1); LOOM_HINT_CASE(l3_1);
    LOOM_HINT_CASE(l1_2); LOOM_HINT_CASE(l2_2); LOOM_HINT_CASE(l1_4); LOOM_HINT_CASE(l2_4);
    LOOM_HINT_CASE(l1_8); LOOM_HINT_CASE(l2_8); LOOM_HINT_CASE(l1_l2_2); LOOM_HINT_CASE(l1_l2_4);
    LOOM_HINT_CASE(page1); LOOM_HINT_CASE(page2); LOOM_HINT_CASE(page4); LOOM_HINT_CASE(page8);
  }
#undef LOOM_HINT_CASE
  __builtin_unreachable();
}
[[gnu::always_inline]] inline void prefetch(const HintPolicy& policy, DemandClass demand,
    const void* address) noexcept {
  prefetch_selected(policy.codes[demand.code()], address, policy.profile.geometry);
}

} // namespace loom
