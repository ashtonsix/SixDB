#include "native.h"

#include <loom/ring.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <thread>
#include <time.h>
#include <vector>

namespace loom_policy_spike {
namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::uint64_t now_ns() {
  timespec value{};
  if (clock_gettime(CLOCK_MONOTONIC, &value)) throw std::runtime_error("read monotonic clock");
  return std::uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}

struct Node {
  const std::byte* next{};
  const std::byte* next_group{};
  std::uint64_t value{};
};

using HintTable = std::array<HintFunction, demand_count>;
using CodeTable = std::array<Procedure, demand_count>;
struct Job {
  Geometry geometry;
  const HintTable* hints{};
  const CodeTable* codes{};
  unsigned code{};
  std::uint64_t result{}, completed{};
};
using NativeRing = loom::Ring<const std::byte*, std::uint64_t, std::uint64_t, Job*>;

template<std::size_t... I>
[[gnu::always_inline]] inline void linear_values(const std::byte* cursor,
    std::uint64_t& sum, std::size_t stride, std::index_sequence<I...>) noexcept {
  ((sum += reinterpret_cast<const Node*>(cursor + I * stride)->value), ...);
}

template<Pattern Kind>
[[gnu::always_inline]] inline const std::byte* advance(const std::byte* cursor,
    std::uint64_t& sum, Geometry geometry) noexcept {
  const auto& node = *reinterpret_cast<const Node*>(cursor);
  if constexpr (Kind == Pattern::irregular) {
    sum += node.value;
    return node.next;
  } else {
    linear_values(cursor, sum, geometry.line_bytes, std::make_index_sequence<16>{});
    return node.next_group;
  }
}

[[gnu::always_inline]] inline void finish(NativeRing::Cursor ring, NativeRing::Slot* slot,
    std::uint64_t remaining, std::uint64_t sum, Job* job) noexcept {
  if (!remaining) {
    job->result = sum;
    ++job->completed;
    ring->complete(*slot);
  }
}

// C entry labels retain the actual native bodies for objdump/codegen inspection.
// The private tail family must not throw; see loom/ring.h for its pinned-compiler
// restriction on C++ noexcept with musttail. Ordinary hint procedures do return.
#define LOOM_POLICY_INLINE_ENTRY(NAME, PATTERN, PROCEDURE) \
extern "C" [[gnu::noinline]] LOOM_NATIVE_CC void loom_policy_##NAME( \
    NativeRing::Cursor ring, NativeRing::Slot* slot, const std::byte* cursor, \
    std::uint64_t remaining, std::uint64_t sum, Job* job) { \
  cursor = advance<Pattern::PATTERN>(cursor, sum, job->geometry); \
  issue<Procedure::PROCEDURE>(cursor, job->geometry); \
  --remaining; \
  finish(ring, slot, remaining, sum, job); \
  [[clang::musttail]] return NativeRing::yield(ring, slot, cursor, remaining, sum, job); \
}

#define LOOM_POLICY_INLINE_FAMILY(PREFIX, PATTERN) \
  LOOM_POLICY_INLINE_ENTRY(PREFIX##_none, PATTERN, none) \
  LOOM_POLICY_INLINE_ENTRY(PREFIX##_near_one, PATTERN, near_one) \
  LOOM_POLICY_INLINE_ENTRY(PREFIX##_distant_one, PATTERN, distant_one) \
  LOOM_POLICY_INLINE_ENTRY(PREFIX##_distant_two, PATTERN, distant_two) \
  LOOM_POLICY_INLINE_ENTRY(PREFIX##_distant_four, PATTERN, distant_four) \
  LOOM_POLICY_INLINE_ENTRY(PREFIX##_page_one, PATTERN, page_one) \
  LOOM_POLICY_INLINE_ENTRY(PREFIX##_page_two, PATTERN, page_two) \
  LOOM_POLICY_INLINE_ENTRY(PREFIX##_page_four, PATTERN, page_four) \
  LOOM_POLICY_INLINE_ENTRY(PREFIX##_page_eight, PATTERN, page_eight)

LOOM_POLICY_INLINE_FAMILY(chain, irregular)
LOOM_POLICY_INLINE_FAMILY(linear, linear)

#undef LOOM_POLICY_INLINE_FAMILY
#undef LOOM_POLICY_INLINE_ENTRY

#define LOOM_POLICY_PROCEDURE_ENTRY(NAME, PATTERN) \
extern "C" [[gnu::noinline]] LOOM_NATIVE_CC void loom_policy_##NAME##_procedure( \
    NativeRing::Cursor ring, NativeRing::Slot* slot, const std::byte* cursor, \
    std::uint64_t remaining, std::uint64_t sum, Job* job) { \
  cursor = advance<Pattern::PATTERN>(cursor, sum, job->geometry); \
  (*job->hints)[job->code](cursor, job->geometry); \
  --remaining; \
  finish(ring, slot, remaining, sum, job); \
  [[clang::musttail]] return NativeRing::yield(ring, slot, cursor, remaining, sum, job); \
}

LOOM_POLICY_PROCEDURE_ENTRY(chain, irregular)
LOOM_POLICY_PROCEDURE_ENTRY(linear, linear)
#undef LOOM_POLICY_PROCEDURE_ENTRY

#define LOOM_POLICY_SWITCH_ENTRY(NAME, PATTERN) \
extern "C" [[gnu::noinline]] LOOM_NATIVE_CC void loom_policy_##NAME##_switch( \
    NativeRing::Cursor ring, NativeRing::Slot* slot, const std::byte* cursor, \
    std::uint64_t remaining, std::uint64_t sum, Job* job) { \
  cursor = advance<Pattern::PATTERN>(cursor, sum, job->geometry); \
  issue_selected((*job->codes)[job->code], cursor, job->geometry); \
  --remaining; \
  finish(ring, slot, remaining, sum, job); \
  [[clang::musttail]] return NativeRing::yield(ring, slot, cursor, remaining, sum, job); \
}
LOOM_POLICY_SWITCH_ENTRY(chain, irregular)
LOOM_POLICY_SWITCH_ENTRY(linear, linear)
#undef LOOM_POLICY_SWITCH_ENTRY

#define LOOM_POLICY_CALLBACKS(PREFIX) \
  loom_policy_##PREFIX##_none, loom_policy_##PREFIX##_near_one, \
  loom_policy_##PREFIX##_distant_one, loom_policy_##PREFIX##_distant_two, \
  loom_policy_##PREFIX##_distant_four, loom_policy_##PREFIX##_page_one, \
  loom_policy_##PREFIX##_page_two, loom_policy_##PREFIX##_page_four, \
  loom_policy_##PREFIX##_page_eight

constexpr std::array<NativeRing::Function, 9> chain_callbacks{LOOM_POLICY_CALLBACKS(chain)};
constexpr std::array<NativeRing::Function, 9> linear_callbacks{LOOM_POLICY_CALLBACKS(linear)};
#undef LOOM_POLICY_CALLBACKS

// Fixed-inline control chooses the same policy once, before timing, without a
// hot procedure call. It deliberately shares its native body with the fused LUT.
NativeRing::Function fixed_callback(Pattern pattern, Procedure procedure) {
  if (pattern == Pattern::irregular) {
    switch (procedure) {
      case Procedure::none: return loom_policy_chain_none;
      case Procedure::near_one: return loom_policy_chain_near_one;
      case Procedure::distant_one: return loom_policy_chain_distant_one;
      case Procedure::distant_two: return loom_policy_chain_distant_two;
      case Procedure::distant_four: return loom_policy_chain_distant_four;
      case Procedure::page_one: return loom_policy_chain_page_one;
      case Procedure::page_two: return loom_policy_chain_page_two;
      case Procedure::page_four: return loom_policy_chain_page_four;
      case Procedure::page_eight: return loom_policy_chain_page_eight;
    }
  } else {
    switch (procedure) {
      case Procedure::none: return loom_policy_linear_none;
      case Procedure::near_one: return loom_policy_linear_near_one;
      case Procedure::distant_one: return loom_policy_linear_distant_one;
      case Procedure::distant_two: return loom_policy_linear_distant_two;
      case Procedure::distant_four: return loom_policy_linear_distant_four;
      case Procedure::page_one: return loom_policy_linear_page_one;
      case Procedure::page_two: return loom_policy_linear_page_two;
      case Procedure::page_four: return loom_policy_linear_page_four;
      case Procedure::page_eight: return loom_policy_linear_page_eight;
    }
  }
  throw std::runtime_error("invalid fixed procedure");
}

std::uint64_t arena_budget(Geometry geometry, std::size_t width, std::size_t nodes) {
  require(valid_geometry(geometry) && geometry.line_bytes >= sizeof(Node), "native geometry cannot hold Node");
  require(width && nodes >= 16 && std::has_single_bit(nodes), "native positive width and power-of-two nodes >=16");
  constexpr auto maximum = std::numeric_limits<std::size_t>::max();
  require(nodes <= maximum / geometry.line_bytes, "native input size overflow");
  const auto input_bytes = nodes * geometry.line_bytes;
  constexpr std::size_t extra = 32 * 1024 * 1024;
  require(input_bytes <= maximum - extra && width <= (maximum - extra - input_bytes) / 1024,
          "native arena size overflow");
  return input_bytes + width * 1024 + extra;
}

struct Fixture {
  loom::Worker worker;
  Fixture(std::uint64_t budget, int cpu) {
    require(loom::adopt(worker, cpu).has_value(), "adopt native worker");
    try {
      auto arena = orbital::allocation::open_arena(budget);
      require(arena.has_value(), "open native arena");
      require(loom::configure(worker, std::move(*arena), 0).has_value(), "configure native worker");
    } catch (...) {
      loom::relinquish(worker);
      throw;
    }
  }
  ~Fixture() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto progress = loom::shutdown(worker);
      if (progress.kind() == orbital::Progress::Kind::done) break;
      if (progress.kind() == orbital::Progress::Kind::failed || std::chrono::steady_clock::now() >= deadline)
        std::abort();
      loom::poll(worker, {256, 0});
      std::this_thread::yield();
    }
    loom::relinquish(worker);
  }
};

struct Input {
  orbital::allocation::Allocation allocation;
  orbital::allocation::Hold held;
  std::byte* base{};
  Input(loom::Worker& worker, Geometry geometry, std::size_t nodes) {
    const auto size = nodes * geometry.line_bytes;
    auto value = loom::allocate(worker, {.bytes = size,
        .alignment = std::max(std::size_t(64), geometry.line_bytes),
        .initialization = orbital::allocation::Initialization::zero});
    require(value.has_value(), "allocate native input");
    allocation = std::move(*value);
    auto use = loom::hold(allocation, {0, size});
    require(use.has_value(), "hold native input");
    held = std::move(*use);
    base = loom::bytes(held).data();
    const auto node_mask = nodes - 1, group_mask = nodes / 16 - 1;
    for (std::size_t i = 0; i < nodes; ++i) {
      const auto next = (i * 5 + 3) & node_mask;
      const auto next_group = ((i / 16 * 5 + 3) & group_mask) * 16;
      std::construct_at(reinterpret_cast<Node*>(base + i * geometry.line_bytes),
          Node{base + next * geometry.line_bytes, base + next_group * geometry.line_bytes, i * 73 + 19});
    }
  }
};

std::uint64_t expected(std::size_t start, std::size_t rounds, std::size_t mask, Pattern pattern) {
  // Independent source law, not a traversal of generated Node pointers or results.
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < rounds; ++i) {
    if (pattern == Pattern::irregular) sum += std::uint64_t(start) * 73 + 19;
    else sum += 16 * (std::uint64_t(start) * 16 * 73 + 19) + 73 * 120;
    start = (start * 5 + 3) & mask;
  }
  return sum;
}

} // namespace

struct NativeWork::Impl {
  Fixture fixture;
  Input input;
  NativeRing ring;
  Geometry geometry;
  std::size_t width, nodes;
  HintTable hints{};
  CodeTable codes{};
  std::array<NativeRing::Function, demand_count> continuations{};
  std::vector<Job> jobs;

  Impl(Geometry shape, std::size_t count, std::size_t node_count, int cpu)
      : fixture(arena_budget(shape, count, node_count), cpu), input(fixture.worker, shape, node_count),
        geometry(shape), width(count), nodes(node_count), jobs(count) {
    require(ring.configure(fixture.worker, {width, 0}, geometry.page_bytes).has_value(), "configure native Ring");
  }
  ~Impl() {
    if (ring.pending()) ring.discard();
    if (!ring.close()) std::abort();
  }
};

NativeWork::NativeWork(Geometry geometry, std::size_t width, std::size_t nodes, int cpu)
    : impl_(std::make_unique<Impl>(geometry, width, nodes, cpu)) {}
NativeWork::~NativeWork() = default;

NativeStats NativeWork::batch(const Policy& policy, Dispatch dispatch, std::size_t rounds, Pattern pattern) {
  auto& state = *impl_;
  require(!state.ring.pending() && orbital::worker::on_lane(state.fixture.worker.lane), "native returned owner boundary");
  require(policy.profile.geometry.line_bytes == state.geometry.line_bytes &&
      policy.profile.geometry.page_bytes == state.geometry.page_bytes, "native geometry update requires new input bank");
  require(unsigned(pattern) <= unsigned(Pattern::linear) && unsigned(dispatch) <= unsigned(Dispatch::continuation_lut),
      "invalid native dispatch or pattern");
  const auto grain = pattern == Pattern::linear ? 16u : 1u;
  const auto minimum = grain * state.geometry.line_bytes;
  const auto bound = bind_code(pattern, minimum, minimum, state.geometry);
  require(bound.has_value(), "bind native demand code");
  const auto code = *bound;
  const auto width = policy.profile.preferred_width;
  require(width && width <= state.width, "profile width exceeds prepared native capacity");
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  require(rounds && rounds <= maximum / width && rounds * width <= maximum / grain,
      "native positive finite load count");
  for (unsigned i = 0; i < demand_count; ++i) {
    const auto selected = policy.procedures[i];
    require(unsigned(selected) < chain_callbacks.size(), "invalid native procedure code");
    state.hints[i] = issue_function(selected);
    state.codes[i] = selected;
    state.continuations[i] = (i < 8 ? chain_callbacks : linear_callbacks)[unsigned(selected)];
  }
  const auto selected = policy.procedures[code];
  require(required_bytes(selected, state.geometry) <= minimum, "mapped native hint exceeds qualified group");
  const auto fixed = fixed_callback(pattern, selected);
  const auto procedure = pattern == Pattern::irregular ? loom_policy_chain_procedure : loom_policy_linear_procedure;
  const auto code_switch = pattern == Pattern::irregular ? loom_policy_chain_switch : loom_policy_linear_switch;
  const auto mask = (pattern == Pattern::irregular ? state.nodes : state.nodes / 16) - 1;
  for (auto& job : state.jobs) job = {state.geometry, &state.hints, &state.codes, code, 0, 0};

  const auto before_visits = state.ring.counters().visits;
  const auto begin = now_ns();
  for (std::size_t i = 0; i < width; ++i) {
    auto& job = state.jobs[i];
    const auto start = (i * 13) & mask;
    const auto* cursor = state.input.base + start * grain * state.geometry.line_bytes;
    // Admission uses the same concrete procedure for all three paths. No Loom
    // Hint descriptor is also passed to push, so there is no duplicated hint.
    issue_selected(state.codes[job.code], cursor, state.geometry);
    const auto callback = dispatch == Dispatch::fixed_inline ? fixed :
        dispatch == Dispatch::code_lut_switch ? code_switch :
        dispatch == Dispatch::procedure_lut ? procedure : state.continuations[job.code];
    require(state.ring.push(callback, {cursor, rounds, 0, &job}), "native Ring enqueue refused");
  }
  while (state.ring.pending()) state.ring.spin({128});
  const auto elapsed = now_ns() - begin;
  const auto visits = state.ring.counters().visits - before_visits;
  require(visits == rounds * width, "native dispatched visit count mismatch");
  std::uint64_t checksum = 0;
  for (std::size_t i = 0; i < width; ++i) {
    const auto& job = state.jobs[i];
    require(job.completed == 1 && job.result == expected((i * 13) & mask, rounds, mask, pattern),
        "native once-only result differs from independent source law");
    checksum += job.result;
  }
  return {visits * grain, checksum, visits, elapsed};
}

} // namespace loom_policy_spike
