#include "contiguous.h"
#include <array>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) noexcept {
  if (!condition) {
    std::cerr << "loom_contiguous_check: " << message << '\n';
    std::abort();
  }
}

struct Recorded {
  std::array<std::uintptr_t, loom_locality::ContiguousPlan::maximum_hints> addresses{};
  unsigned count{};
  void operator()(const char* address) noexcept {
    check(count < addresses.size(), "hint cap exceeded");
    addresses[count++] = reinterpret_cast<std::uintptr_t>(address);
  }
};

// This oracle enumerates intersected physical cache lines. It does not compute
// the emitter's class, stride targets or instruction count.
void coverage(std::span<const std::byte> window, std::size_t consumed,
    std::size_t line_bytes) {
  const auto bound = loom_locality::bind_contiguous(consumed, window, line_bytes);
  check(bound.has_value(), "valid contiguous window refused");
  const auto& plan = *bound;
  check(plan.requested_bytes() == consumed, "requested extent changed");
  const auto limit = 8 * line_bytes;
  const auto covered = consumed < limit ? consumed : limit;
  check(plan.covered_bytes() == covered && plan.full_coverage() == (consumed <= limit),
      "large-span lowering not explicit or prefix length wrong");
  Recorded recorded;
  loom_locality::emit(plan, window.data(), recorded);
  check(recorded.count == plan.hint_count(), "instruction count disagrees with emitted hints");
  if (!consumed) {
    check(!recorded.count, "empty plan emitted a target");
    return;
  }
  const auto first = reinterpret_cast<std::uintptr_t>(window.data());
  const auto last = first + covered - 1;
  bool first_seen = false, last_seen = false;
  for (unsigned i = 0; i < recorded.count; ++i) {
    const auto target = recorded.addresses[i];
    check(target >= first && target <= last, "hint outside covered consumed bytes");
    first_seen |= target == first;
    last_seen |= target == last;
  }
  check(first_seen && last_seen, "first or unaligned last consumed byte not hinted");
  for (auto line = first / line_bytes; line <= last / line_bytes; ++line) {
    bool found = false;
    for (unsigned i = 0; i < recorded.count; ++i)
      found |= recorded.addresses[i] / line_bytes == line;
    check(found, "intersected cache line not covered");
  }
}

void unique_coverage(std::span<const std::byte> window, std::size_t consumed,
    std::size_t line_bytes) {
  const auto bound = loom_locality::bind_contiguous(consumed, window, line_bytes);
  check(bound.has_value(), "unique valid contiguous window refused");
  Recorded recorded;
  loom_locality::emit_unique(*bound, window.data(), recorded);
  if (!consumed) {
    check(!recorded.count, "unique empty plan emitted a target");
    return;
  }
  const auto first = reinterpret_cast<std::uintptr_t>(window.data());
  const auto last = first + bound->covered_bytes() - 1;
  std::array<bool, 9> seen{};
  unsigned distinct = 0;
  for (unsigned i = 0; i < recorded.count; ++i) {
    const auto target = recorded.addresses[i];
    check(target >= first && target <= last, "unique hint outside consumed prefix");
    const auto line = target / line_bytes - first / line_bytes;
    check(line < seen.size(), "unique target exceeds bounded line footprint");
    distinct += !seen[line];
    seen[line] = true;
  }
  // Independent physical-line oracle: enumeration rather than the selector's
  // offset/shift/clamp arithmetic. At most one duplicate is permitted for the
  // largest class's eight-line footprint; all smaller footprints are unique.
  unsigned required = 0;
  for (auto line = first / line_bytes; line <= last / line_bytes; ++line) {
    check(seen[line - first / line_bytes], "unique intersected cache line not covered");
    ++required;
  }
  check(distinct == required, "unique line footprint disagreement");
  check(recorded.count == (required < 8 ? required : 9), "unique instruction count/duplicate bound");
  check(recorded.addresses[0] == first, "unique first byte not hinted");
}

void windows() {
  alignas(4096) std::array<std::byte, 16384> storage{};
  const std::span<const std::byte> bytes{storage};
  constexpr std::array<std::size_t, 4> page_positions{0, 4096 - 128, 4096, 8192 - 128};
  constexpr std::array<std::size_t, 21> notable{
    0, 1, 7, 63, 64, 65, 95, 96, 127, 128, 129, 191, 192, 193,
    319, 320, 321, 511, 512, 1024, 4096};
  for (const auto line : {std::size_t{64}, std::size_t{128}}) {
    for (const auto page : page_positions) {
      for (std::size_t alignment = 0; alignment < line; ++alignment) {
        const auto window = bytes.subspan(page + alignment);
        for (std::size_t consumed = 1; consumed <= 8 * line; ++consumed)
          coverage(window, consumed, line);
        for (std::size_t consumed = 1; consumed <= 8 * line; ++consumed)
          unique_coverage(window, consumed, line);
        for (const auto consumed : notable) {
          coverage(window.first(consumed), consumed, line);
          unique_coverage(window.first(consumed), consumed, line);
        }
      }
    }
  }
  // The same class shape may be reused at independent addresses only when the
  // exact window is qualified. Varying address alignment cannot alter safety.
  const auto plan = loom_locality::bind_contiguous(96, 96, 64);
  check(plan.has_value(), "96-byte plan");
  for (std::size_t i = 0; i < 128; ++i) {
    Recorded recorded;
    loom_locality::emit(*plan, storage.data() + i, recorded);
    const auto start = reinterpret_cast<std::uintptr_t>(storage.data() + i);
    for (unsigned j = 0; j < recorded.count; ++j)
      check(recorded.addresses[j] >= start && recorded.addresses[j] < start + 96,
          "reused class exceeded its exact independent window");
  }
}

void refusal() {
  std::array<std::byte, 512> storage{};
  const std::span<const std::byte> bytes{storage};
  using Piece = std::span<const std::byte>;
  const std::array<Piece, 3> adjacent{bytes.first(64), bytes.subspan(64, 32), bytes.subspan(96, 96)};
  check(loom_locality::bind_contiguous(192, std::span<const Piece>{adjacent}, 64).has_value(),
      "adjacent qualified pieces refused");
  const std::array<Piece, 2> gap{bytes.first(64), bytes.subspan(65, 128)};
  const std::array<Piece, 2> reverse{bytes.subspan(64, 64), bytes.first(64)};
  const std::array<Piece, 2> overlap{bytes.first(64), bytes.subspan(63, 64)};
  for (const auto* pieces : {&gap, &reverse, &overlap}) {
    const auto result = loom_locality::bind_contiguous(96, std::span<const Piece>{*pieces}, 64);
    check(!result && result.error() == loom_locality::BindError::disjoint_window,
        "gapped/reversed/overlapping pieces accepted as one window");
  }
  const auto small = loom_locality::bind_contiguous(96, bytes.first(95), 64);
  check(!small && small.error() == loom_locality::BindError::insufficient_window,
      "short qualified window accepted");
  const auto large = loom_locality::bind_contiguous(4096, 512, 64);
  check(!large && large.error() == loom_locality::BindError::insufficient_window,
      "prefix cap bypassed whole-consumed-window qualification");
  for (const auto geometry : {0U, 32U, 96U, 256U}) {
    const auto invalid = loom_locality::bind_contiguous(64, 64, geometry);
    check(!invalid && invalid.error() == loom_locality::BindError::invalid_geometry,
        "unsupported line geometry accepted");
  }
  Recorded empty;
  loom_locality::emit(loom_locality::ContiguousPlan{}, nullptr, empty);
  loom_locality::emit_unique(loom_locality::ContiguousPlan{}, nullptr, empty);
  check(!empty.count, "default plan dereferenced or hinted null");
}
} // namespace

int main() {
  windows();
  refusal();
  std::cout << "loom_contiguous_check: exhaustive endpoint/unique 64/128-byte coverage and refusal passed\n";
}
