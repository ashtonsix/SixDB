#pragma once

// Spike-private random-start, contiguous-consumed shape. Binding is cold; it
// grants neither ownership/read rights nor a cache-residency guarantee.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

#ifndef LOOM_CONTIGUOUS_IDENTITY
#define LOOM_CONTIGUOUS_IDENTITY 0
#endif

namespace loom_locality {

enum class BindError { invalid_geometry, invalid_window, insufficient_window, disjoint_window };
template<class T> using Result = std::expected<T, BindError>;

class ContiguousPlan;
[[nodiscard]] constexpr Result<ContiguousPlan> bind_contiguous(std::size_t consumed_bytes,
    std::size_t minimum_qualified_bytes, std::size_t line_bytes) noexcept;

/// Exact consumed length, selected once for every later emission address.
/// Every address must qualify requested_bytes() contiguous live readable bytes.
/// A shorter emission must bind its own plan. Nothing is inferred about earlier
/// addresses or stream training. The class contains no alignment/page decisions.
class ContiguousPlan {
  std::size_t requested_{};
  std::uint16_t tail_{};
  std::uint8_t code_{16};
  constexpr ContiguousPlan(std::size_t requested, std::size_t covered, unsigned code)
      : requested_(requested), tail_(static_cast<std::uint16_t>(covered - 1)),
        code_(static_cast<std::uint8_t>(code)) {}
  friend constexpr Result<ContiguousPlan> bind_contiguous(std::size_t,
      std::size_t, std::size_t) noexcept;
public:
  constexpr ContiguousPlan() = default;
  static constexpr unsigned maximum_lines = 8;
  static constexpr unsigned maximum_hints = maximum_lines + 1;
  constexpr std::size_t requested_bytes() const noexcept { return requested_; }
  constexpr std::size_t covered_bytes() const noexcept { return code_ == 16 ? 0 : tail_ + 1; }
  constexpr bool full_coverage() const noexcept { return requested_ == covered_bytes(); }
  constexpr std::size_t tail_offset() const noexcept { return tail_; }
  constexpr unsigned code() const noexcept { return code_; }
  constexpr std::size_t line_bytes() const noexcept { return code_ == 16 ? 0 : code_ < 8 ? 64 : 128; }
  /// Endpoint-emitter count, including duplicate physical cache lines.
  /// The optional unique emitter's count also depends on address alignment.
  constexpr unsigned hint_count() const noexcept { return code_ == 16 ? 0 : code_ % 8 + 2; }
};
static_assert(sizeof(ContiguousPlan) <= 16);

/// The entire consumed extent must be qualified, even when the hint cap lowers
/// large extents to a prefix. Prefix coverage is at most eight native lines;
/// its unaligned tail is covered, while the unhinted remainder is explicit.
[[nodiscard]] constexpr Result<ContiguousPlan> bind_contiguous(std::size_t consumed_bytes,
    std::size_t minimum_qualified_bytes, std::size_t line_bytes) noexcept {
  if (line_bytes != 64 && line_bytes != 128) return std::unexpected(BindError::invalid_geometry);
  if (consumed_bytes > minimum_qualified_bytes) return std::unexpected(BindError::insufficient_window);
  if (!consumed_bytes) return ContiguousPlan{};
  const auto cap = ContiguousPlan::maximum_lines * line_bytes;
  const auto covered = consumed_bytes < cap ? consumed_bytes : cap;
  const auto lines = (covered - 1) / line_bytes + 1;
  const auto code = (line_bytes == 128 ? 8 : 0) + lines - 1;
  return ContiguousPlan{consumed_bytes, covered, static_cast<unsigned>(code)};
}

[[nodiscard]] inline Result<ContiguousPlan> bind_contiguous(std::size_t consumed_bytes,
    std::span<const std::byte> qualified, std::size_t line_bytes) noexcept {
  if (!qualified.empty() && !qualified.data()) return std::unexpected(BindError::invalid_window);
  return bind_contiguous(consumed_bytes, qualified.size(), line_bytes);
}

/// Cold refusal check for a caller-provided contiguous union. Numerical adjacency
/// only rejects gaps/overlap/reordering; the caller still proves a common valid
/// live read window and its lifetime. No list is retained or visited by emission.
[[nodiscard]] inline Result<ContiguousPlan> bind_contiguous(std::size_t consumed_bytes,
    std::span<const std::span<const std::byte>> qualified, std::size_t line_bytes) noexcept {
  std::uintptr_t following = 0;
  std::size_t total = 0;
  bool occupied = false;
  for (const auto piece : qualified) {
    if (piece.empty()) continue;
    if (!piece.data()) return std::unexpected(BindError::invalid_window);
    const auto address = reinterpret_cast<std::uintptr_t>(piece.data());
    if (occupied && address != following) return std::unexpected(BindError::disjoint_window);
    if (piece.size() > std::numeric_limits<std::uintptr_t>::max() - address ||
        piece.size() > std::numeric_limits<std::size_t>::max() - total)
      return std::unexpected(BindError::invalid_window);
    following = address + piece.size();
    total += piece.size();
    occupied = true;
  }
  return bind_contiguous(consumed_bytes, total, line_bytes);
}

namespace detail {
template<std::size_t LineBytes, class Sink, std::size_t... I>
[[gnu::always_inline]] inline void emit_class(const char* base, std::size_t tail,
    Sink& sink, std::index_sequence<I...>) noexcept {
  (sink(base + I * LineBytes), ...);
  sink(base + tail);
#if LOOM_CONTIGUOUS_IDENTITY
  // Spike-only compiler control: distinct class identity inhibits suffix
  // merging. It emits no instructions and has no variable operands/clobbers.
  asm volatile("" : : "i"(LineBytes), "i"(sizeof...(I)));
#endif
}
template<unsigned Locality>
struct Prefetch {
  static_assert(Locality <= 3);
  [[gnu::always_inline]] void operator()(const void* address) const noexcept {
    __builtin_prefetch(address, 0, Locality);
  }
};

template<std::size_t LineBytes, unsigned Count, class Sink, std::size_t... I>
[[gnu::always_inline]] inline void emit_unique_class(const char* base,
    std::size_t following_offset, std::size_t tail, Sink& sink,
    std::index_sequence<I...>) noexcept {
  sink(base);
  (sink(base + following_offset + I * LineBytes), ...);
  if constexpr (Count == ContiguousPlan::maximum_lines) sink(base + tail);
#if LOOM_CONTIGUOUS_IDENTITY
  asm volatile("" : : "i"(LineBytes), "i"(Count));
#endif
}
} // namespace detail

/// Only the class switch remains. Within each class the fixed line-stride
/// targets and last covered byte cover every intersected native line, including
/// an unaligned tail, without hints outside the consumed/qualified window.
/// The sink seam is for address-coverage checks; production emits no counters.
template<class Sink>
[[gnu::always_inline]] inline void emit(const ContiguousPlan& plan, const void* address,
    Sink&& sink) noexcept {
  static_assert(std::is_nothrow_invocable_v<Sink&, const char*>);
  const auto* base = static_cast<const char*>(address);
  const auto tail = plan.tail_offset();
#define LOOM_CONTIGUOUS_CASE(CODE, LINE, COUNT) \
  case CODE: return detail::emit_class<LINE>(base, tail, sink, std::make_index_sequence<COUNT>{})
  switch (plan.code()) {
    LOOM_CONTIGUOUS_CASE(0, 64, 1); LOOM_CONTIGUOUS_CASE(1, 64, 2);
    LOOM_CONTIGUOUS_CASE(2, 64, 3); LOOM_CONTIGUOUS_CASE(3, 64, 4);
    LOOM_CONTIGUOUS_CASE(4, 64, 5); LOOM_CONTIGUOUS_CASE(5, 64, 6);
    LOOM_CONTIGUOUS_CASE(6, 64, 7); LOOM_CONTIGUOUS_CASE(7, 64, 8);
    LOOM_CONTIGUOUS_CASE(8, 128, 1); LOOM_CONTIGUOUS_CASE(9, 128, 2);
    LOOM_CONTIGUOUS_CASE(10, 128, 3); LOOM_CONTIGUOUS_CASE(11, 128, 4);
    LOOM_CONTIGUOUS_CASE(12, 128, 5); LOOM_CONTIGUOUS_CASE(13, 128, 6);
    LOOM_CONTIGUOUS_CASE(14, 128, 7); LOOM_CONTIGUOUS_CASE(15, 128, 8);
    case 16: return;
  }
#undef LOOM_CONTIGUOUS_CASE
  __builtin_unreachable();
}

/// Locality is selected outside the hot class body: 3 for fixed L1, 2 for L2.
/// A no-hint policy skips this primitive; it adds no pressure query here.
template<unsigned Locality>
[[gnu::always_inline]] inline void prefetch(const ContiguousPlan& plan,
    const void* address) noexcept {
  emit(plan, address, detail::Prefetch<Locality>{});
}

/// Address preparation chooses the number of intersected physical cache lines.
/// Classes one through seven issue exactly one hint per line. Class eight also
/// emits the covered tail, proving both eight/nine-line windows: it duplicates
/// a line only when the window intersects eight lines. The cap remains nine.
/// Empty plans select the inert class before any derived pointer is formed.
template<class Sink>
[[gnu::always_inline]] inline void emit_unique(const ContiguousPlan& plan,
    const void* address, Sink&& sink) noexcept {
  static_assert(std::is_nothrow_invocable_v<Sink&, const char*>);
  const auto geometry = (plan.code() >> 3) & 1;
  const auto shift = 6 + geometry;
  const auto line_bytes = std::size_t{1} << shift;
  const auto offset = reinterpret_cast<std::uintptr_t>(address) & (line_bytes - 1);
  const auto tail = plan.tail_offset();
  const auto raw_index = (offset + tail) >> shift; // Proven zero through eight.
  const auto index = raw_index - (raw_index >> 3); // Saturates eight to seven.
  const auto candidate = (geometry << 3) + index;
  const auto selected = plan.code() == 16 ? 16 : candidate;
  const auto following_offset = line_bytes - offset;
  const auto* base = static_cast<const char*>(address);
#define LOOM_CONTIGUOUS_UNIQUE_CASE(CODE, LINE, COUNT) \
  case CODE: return detail::emit_unique_class<LINE, COUNT>(base, following_offset, tail, sink, \
      std::make_index_sequence<COUNT - 1>{})
  switch (selected) {
    LOOM_CONTIGUOUS_UNIQUE_CASE(0, 64, 1); LOOM_CONTIGUOUS_UNIQUE_CASE(1, 64, 2);
    LOOM_CONTIGUOUS_UNIQUE_CASE(2, 64, 3); LOOM_CONTIGUOUS_UNIQUE_CASE(3, 64, 4);
    LOOM_CONTIGUOUS_UNIQUE_CASE(4, 64, 5); LOOM_CONTIGUOUS_UNIQUE_CASE(5, 64, 6);
    LOOM_CONTIGUOUS_UNIQUE_CASE(6, 64, 7); LOOM_CONTIGUOUS_UNIQUE_CASE(7, 64, 8);
    LOOM_CONTIGUOUS_UNIQUE_CASE(8, 128, 1); LOOM_CONTIGUOUS_UNIQUE_CASE(9, 128, 2);
    LOOM_CONTIGUOUS_UNIQUE_CASE(10, 128, 3); LOOM_CONTIGUOUS_UNIQUE_CASE(11, 128, 4);
    LOOM_CONTIGUOUS_UNIQUE_CASE(12, 128, 5); LOOM_CONTIGUOUS_UNIQUE_CASE(13, 128, 6);
    LOOM_CONTIGUOUS_UNIQUE_CASE(14, 128, 7); LOOM_CONTIGUOUS_UNIQUE_CASE(15, 128, 8);
    case 16: return;
  }
#undef LOOM_CONTIGUOUS_UNIQUE_CASE
  __builtin_unreachable();
}

template<unsigned Locality>
[[gnu::always_inline]] inline void prefetch_unique(const ContiguousPlan& plan,
    const void* address) noexcept {
  emit_unique(plan, address, detail::Prefetch<Locality>{});
}

} // namespace loom_locality
