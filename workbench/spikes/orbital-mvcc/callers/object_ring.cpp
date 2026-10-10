#include "object_ring.h"
#include <algorithm>
#include <cassert>

namespace loom_object_ring {
namespace sr = orbital_mvcc::metadata_consumer::shape_read;
namespace {
constexpr auto encoding = law::ReadLaw::canonical();

[[gnu::always_inline]] inline const law::tp::byte* address(const Frame& frame, Word row) noexcept {
  return frame.bytes + row * law::v::tuple_bytes;
}
template<bool Hints>
[[gnu::always_inline]] inline void hint(const loom::HintPolicy& policy, const Frame& frame,
                                      Word row, unsigned offset = 0) noexcept {
  if constexpr (Hints) loom::prefetch(policy, frame.demand, address(frame, row) + offset);
}
[[gnu::always_inline]] inline void finish(Frame& frame, const State& state) noexcept {
  frame.result = {state.digest, frame.request.steps - state.remaining, 0};
  frame.done = true;
}
[[gnu::always_inline]] inline void absent(Frame& frame, State& state) noexcept {
  orbital_bench::detail::mix(state.digest, state.key);
  orbital_bench::detail::mix(state.digest, 0);
  finish(frame, state);
}
[[gnu::always_inline]] inline void consume(State& state, const law::tp::byte* source) noexcept {
  const auto next = encoding.read_unchecked<1>(source);
  orbital_bench::detail::mix(state.digest, state.key);
  orbital_bench::detail::mix(state.digest, next);
  orbital_bench::detail::mix(state.digest, encoding.read_unchecked<2>(source));
  orbital_bench::detail::mix(state.digest, encoding.read_unchecked<3>(source));
  state.key = next;
  --state.remaining;
}

template<Stage, bool> LOOM_NATIVE_CC void native(Ring*, Ring::Slot*, Frame*, Word, Word, Word, Word, Word);
struct NativeRoute {
  Ring* ring;
  Ring::Slot* slot;
  template<Stage Next, bool Hints> [[gnu::always_inline]] void go(Frame&) const noexcept {
    slot->function = native<Next, Hints>;
  }
  [[gnu::always_inline]] void complete() const noexcept { ring->complete(*slot); }
};
struct ManualRoute {
  template<Stage Next, bool> [[gnu::always_inline]] void go(Frame& frame) const noexcept {
    frame.stage = Next;
  }
  [[gnu::always_inline]] void complete() const noexcept {}
};

// Same lower_bound decision/load/digest bodies in manual and native controls.
// Each callback consumes one dependent key probe or one established record.
template<Stage Current, bool Hints, class Route>
[[gnu::always_inline]] inline void body(Frame& frame, State& state,
    const loom::HintPolicy& policy, const Route& route) noexcept {
  if constexpr (Current == Stage::probe) {
    const auto middle = state.first + (state.last - state.first) / 2;
    const auto found = encoding.read_unchecked<0>(address(frame, middle));
    // Equality continues left, preserving the complete consumer's first match.
    if (found < state.key) state.first = middle + 1;
    else state.last = middle;
    if (state.first < state.last) {
      hint<Hints>(policy, frame, state.first + (state.last - state.first) / 2);
    } else if (state.first == frame.records) {
      absent(frame, state); route.complete();
    } else {
      hint<Hints>(policy, frame, state.first);
      route.template go<Stage::candidate, Hints>(frame);
    }
  } else if constexpr (Current == Stage::candidate) {
    if (encoding.read_unchecked<0>(address(frame, state.first)) != state.key) {
      absent(frame, state); route.complete();
    } else {
      hint<Hints>(policy, frame, state.first, 8);
      route.template go<Stage::payload, Hints>(frame);
    }
  } else {
    consume(state, address(frame, state.first));
    if (!state.remaining) {
      finish(frame, state); route.complete();
    } else {
      state.first = 0; state.last = frame.records;
      hint<Hints>(policy, frame, state.last / 2);
      route.template go<Stage::probe, Hints>(frame);
    }
  }
}
template<Stage Current, bool Hints>
LOOM_NATIVE_CC void native(Ring* ring, Ring::Slot* slot, Frame* frame,
    Word key, Word first, Word last, Word remaining, Word digest) {
  State state{key, first, last, remaining, digest};
  body<Current, Hints>(*frame, state, ring->hint_policy(), NativeRoute{ring, slot});
  [[clang::musttail]] return Ring::yield(ring, slot, frame, state.key, state.first,
                                       state.last, state.remaining, state.digest);
}
template<bool Hints> [[gnu::always_inline]] inline void manual(Frame& frame,
    const loom::HintPolicy& policy) noexcept {
  switch (frame.stage) {
  case Stage::probe: body<Stage::probe, Hints>(frame, frame.state, policy, ManualRoute{}); break;
  case Stage::candidate: body<Stage::candidate, Hints>(frame, frame.state, policy, ManualRoute{}); break;
  case Stage::payload: body<Stage::payload, Hints>(frame, frame.state, policy, ManualRoute{}); break;
  }
}

// Ordinary serial lower_bound/chain; the same hint opportunities have no other
// request's work between hint and dependent consumption.
template<bool Hints> void serial(Frame& frame, const loom::HintPolicy& policy) noexcept {
  auto& state = frame.state;
  for (;;) {
    while (state.first < state.last) {
      const auto middle = state.first + (state.last - state.first) / 2;
      const auto found = encoding.read_unchecked<0>(address(frame, middle));
      if (found < state.key) state.first = middle + 1;
      else state.last = middle;
      if (state.first < state.last)
        hint<Hints>(policy, frame, state.first + (state.last - state.first) / 2);
    }
    if (state.first == frame.records) { absent(frame, state); return; }
    hint<Hints>(policy, frame, state.first);
    if (encoding.read_unchecked<0>(address(frame, state.first)) != state.key) {
      absent(frame, state); return;
    }
    hint<Hints>(policy, frame, state.first, 8);
    consume(state, address(frame, state.first));
    if (!state.remaining) { finish(frame, state); return; }
    state.first = 0; state.last = frame.records;
    hint<Hints>(policy, frame, state.last / 2);
  }
}
} // namespace

orbital::Result<void> Frame::open(const Catalog& store, Proof proof, Request input, bool hints) noexcept {
  assert(!occupied && !scope);
  scope.emplace(store); request = input; result = {}; state = {input.key, 0, 0, input.steps, 0};
  stage = Stage::probe; done = input.steps == 0; occupied = true;
  const auto selected = scope->bind(input.object);
  if (!selected) return std::unexpected(orbital::Error::absent);
  if (selected.shape() != proof.shape()) return std::unexpected(orbital::Error::invalid_argument);
  records = selected.records(); state.last = records;
  const auto part = selected.part(0);
  if (!part) return std::unexpected(part.error());
  bytes = reinterpret_cast<const law::tp::byte*>(part->address());
  demand = {};
  if (done) return {}; // selection is still charged/validated, no payload access
  if (!records) { absent(*this, state); return {}; }
  // The captured Shape establishes exact geometry and initialized Tuple bytes.
  // Every probe target is a full row. Payload hints use offset8, leaving15B;
  // the admitted class-zero envelope therefore stays valid for every target.
  if (hints) {
    const auto bound = loom::bind_demand({loom::MemoryPattern::irregular, 64}, 15);
    if (!bound) return std::unexpected(bound.error());
    demand = *bound;
  }
  return {};
}
void Frame::clear() noexcept {
  scope.reset(); bytes = nullptr; records = 0; occupied = done = false;
}
bool enqueue(Ring& ring, Frame& frame, bool hints) noexcept {
  assert(frame.occupied && !frame.done);
  const auto& state = frame.state;
  const auto function = hints ? native<Stage::probe, true> : native<Stage::probe, false>;
  return ring.push(function, {&frame, state.key, state.first, state.last, state.remaining, state.digest},
      loom::MemoryDependency{address(frame, state.last / 2), frame.demand});
}

Driver::~Driver() {
  const auto result = close(); assert(result);
}
orbital::Result<void> Driver::configure(std::size_t width) noexcept {
  if (!width || width > 64) return std::unexpected(orbital::Error::invalid_argument);
  if (ring_.pending()) return std::unexpected(orbital::Error::conflict);
  if (width_ == width) return {};
  const auto closed = ring_.close(); if (!closed) return closed;
  width_ = 0;
  const auto configured = ring_.configure(worker_, {width, 0});
  if (!configured) return configured;
  width_ = width; return {};
}
orbital::Result<void> Driver::close() noexcept {
  const auto result = ring_.close(); if (result) width_ = 0;
  return result;
}
orbital::Result<Statistics> Driver::run(const Catalog& store, std::span<const Request> input,
    std::span<Outcome> output, Execution execution, bool hints, std::uint64_t visits) noexcept {
  if (input.size() != output.size() || !width_ || !visits || ring_.pending() ||
      (execution != Execution::serial && execution != Execution::manual && execution != Execution::ring))
    return std::unexpected(orbital::Error::invalid_argument);
  assert(orbital::worker::on_lane(worker_.lane));
  Statistics statistics;
  const auto baseline = ring_.counters().visits;
  struct Cleanup {
    Ring& ring; std::array<Frame,64>& frames;
    ~Cleanup() { if (ring.pending()) ring.discard(); for (auto& frame : frames) if (frame.occupied) frame.clear(); }
  } cleanup{ring_, frames_};
  const auto policy = ring_.hint_policy();
  if (execution == Execution::serial) {
    auto& frame = frames_[0];
    for (std::size_t i = 0; i < input.size(); ++i) {
      const auto opened = frame.open(store, proof_, input[i], hints);
      if (!opened) return std::unexpected(opened.error());
      if (!frame.done) {
        if (hints) { hint<true>(policy, frame, frame.records / 2); serial<true>(frame, policy); }
        else serial<false>(frame, policy);
      }
      output[i] = frame.result; frame.clear(); ++statistics.completed;
    }
    return statistics;
  }
  std::array<Frame*,64> active{};
  std::size_t live = 0, cursor = 0, admitted = 0;
  bool initial = true;
  while (admitted < input.size() || live || ring_.pending()) {
    // Retire outputs/Scopes and refill only after a returned quantum, in both
    // staged controls. Manual's compact circular frontier preserves Ring order.
    for (auto& frame : frames_) if (frame.occupied && frame.done) {
      output[frame.output_index] = frame.result; frame.clear(); ++statistics.completed;
    }
    if (execution == Execution::manual && cursor) {
      std::rotate(active.begin(), active.begin() + cursor, active.begin() + live);
      cursor = 0;
    }
    for (std::size_t i = 0; i < width_ && admitted < input.size(); ++i) {
      auto& frame = frames_[i]; if (frame.occupied) continue;
      do {
        frame.output_index = admitted;
        const auto opened = frame.open(store, proof_, input[admitted++], hints);
        if (!opened) return std::unexpected(opened.error());
        if (!initial) ++statistics.replacement_admissions;
        if (frame.done) { output[frame.output_index] = frame.result; frame.clear(); ++statistics.completed; }
      } while (!frame.occupied && admitted < input.size());
      if (!frame.occupied) continue;
      if (execution == Execution::ring) {
        if (!enqueue(ring_, frame, hints)) return std::unexpected(orbital::Error::conflict);
      } else {
        if (hints) hint<true>(policy, frame, frame.records / 2);
        active[live++] = &frame;
      }
    }
    initial = false;
    if (execution == Execution::ring) {
      if (!ring_.pending()) continue;
      ring_.spin({visits}); ++statistics.returned_quanta;
    } else {
      std::uint64_t dispatched = 0;
      while (live && dispatched < visits) {
        ++dispatched;
        auto* frame = active[cursor];
        if (hints) manual<true>(*frame, policy); else manual<false>(*frame, policy);
        if (frame->done) {
          std::move(active.begin() + cursor + 1, active.begin() + live, active.begin() + cursor);
          if (cursor == --live) cursor = 0;
        } else if (++cursor == live) cursor = 0;
      }
      if (dispatched) ++statistics.returned_quanta;
    }
  }
  // Final Ring completion can make the loop condition false before collection.
  for (auto& frame : frames_) if (frame.occupied && frame.done) {
    output[frame.output_index] = frame.result; frame.clear(); ++statistics.completed;
  }
  statistics.ring_visits = ring_.counters().visits - baseline;
  return statistics;
}
} // namespace loom_object_ring
