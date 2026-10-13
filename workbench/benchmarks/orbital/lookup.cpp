#include "lookup.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace orbital_bench::lookup {
namespace {
template<Stage> LOOM_NATIVE_CC void native(NativeRing::Cursor, NativeRing::Slot*, Frame*);
struct NativeRoute {
  NativeRing::Cursor ring;
  NativeRing::Slot* slot;
  template<Stage Next> [[gnu::always_inline]] void go(Frame&, State&) const noexcept {
    slot->function = native<Next>;
  }
  [[gnu::always_inline]] void finish(Frame& frame) const noexcept {
    frame.done = true;
    ring->complete(*slot);
  }
};
struct ManualRoute {
  template<Stage Next> [[gnu::always_inline]] void go(Frame& frame, State&) const noexcept {
    frame.stage = Next;
  }
  [[gnu::always_inline]] void finish(Frame& frame) const noexcept { frame.done = true; }
};
[[gnu::always_inline]] inline const Page* select(const Source& source, Key key) {
  const auto found = std::lower_bound(source.directory.begin(), source.directory.end(), key,
      [](const packed::Entry& entry, Key wanted) { return entry.maximum < wanted; });
  return found == source.directory.end() ? nullptr : found->page;
}
[[gnu::always_inline]] inline void hint_page(const Page& page) noexcept {
  loom::prefetch<loom::Hint::l1_1>(&page.bindings);
  loom::prefetch<loom::Hint::l1_1>(&page.order);
}
[[gnu::always_inline]] inline void bind_page(Frame& frame, State& state) noexcept {
  frame.binding = std::addressof(*frame.page->bindings);
  state.last = frame.page->live;
  frame.order = frame.page->order;
}
[[gnu::always_inline]] inline void hint_bindings(const packed::Bindings& binding) noexcept {
  // Fixed endpoints cover the metadata windows under either 64B or 128B lines.
  // Forming these object addresses reads no base, stride, source or plan value.
  const auto& plane = binding.series.stream(0);
  loom::prefetch<loom::Hint::l1_1>(&plane);
  loom::prefetch<loom::Hint::l1_1>(reinterpret_cast<const char*>(&plane) + sizeof(plane) - 1);
  loom::prefetch<loom::Hint::l1_1>(&binding.tuples);
  loom::prefetch<loom::Hint::l1_1>(reinterpret_cast<const char*>(&binding.tuples) + sizeof(binding.tuples) - 1);
  loom::prefetch<loom::Hint::l1_1>(&binding.keys);
  loom::prefetch<loom::Hint::l1_1>(reinterpret_cast<const char*>(&binding.tags) + sizeof(binding.tags) - 1);
}
// These fixed first-line hints use the already qualified occupied addresses.
// They do not promise the whole page/tile/tuple is resident. No per-hint counts.
[[gnu::always_inline]] inline void hint_order(const Frame& frame, std::size_t rank) noexcept {
  loom::prefetch<loom::Hint::l1_1>(frame.order + rank);
}
[[gnu::always_inline]] inline void hint_tuple(const Frame& frame, std::size_t physical) noexcept {
  loom::prefetch<loom::Hint::l1_1>(frame.binding->keys.source().row_unchecked(physical));
}
[[gnu::always_inline]] inline void hint_value(const Frame& frame, std::size_t physical) noexcept {
  const auto& plane = frame.binding->series.stream(0);
  loom::prefetch<loom::Hint::l1_1>(plane.bytes.data() + (physical / packed::Format::tile_rows) * plane.stride);
}
template<Stage Current, class Route>
[[gnu::always_inline]] inline void body(Frame& frame, State& state, const Route& route) {
  if constexpr (Current == Stage::directory) {
    frame.page = select(*frame.source, state.key);
    if (!frame.page) {
      consume_absent(*frame.output, state.key);
      route.finish(frame);
    } else {
      hint_page(*frame.page);
      route.template go<Stage::bind>(frame, state);
    }
  } else if constexpr (Current == Stage::bind) {
    bind_page(frame, state);
    state.first = 0;
    if (!state.last) {
      consume_absent(*frame.output, state.key);
      route.finish(frame);
    } else {
      state.mid = state.last / 2;
      hint_bindings(*frame.binding);
      hint_order(frame, state.mid);
      route.template go<Stage::order>(frame, state);
    }
  } else if constexpr (Current == Stage::order) {
    state.physical = frame.order[state.mid];
    hint_tuple(frame, state.physical);
    route.template go<Stage::probe>(frame, state);
  } else if constexpr (Current == Stage::probe) {
    const auto found = packed::read_key(*frame.binding, state.physical);
    if (found == state.key) {
      hint_value(frame, state.physical);
      route.template go<Stage::payload>(frame, state);
    } else {
      if (found < state.key) state.first = state.mid + 1;
      else state.last = state.mid;
      if (state.first < state.last) {
        state.mid = state.first + (state.last - state.first) / 2;
        hint_order(frame, state.mid);
        route.template go<Stage::order>(frame, state);
      } else {
        consume_absent(*frame.output, state.key);
        route.finish(frame);
      }
    }
  } else {
    const auto value = packed::record(*frame.binding, state.physical);
    consume_row(*frame.output, state.key, value);
    if (!--frame.remaining) route.finish(frame);
    else {
      state.key = value.next;
      route.template go<Stage::directory>(frame, state);
    }
  }
}
template<Stage Current> LOOM_NATIVE_CC void native(NativeRing::Cursor ring, NativeRing::Slot* slot, Frame* frame) {
  body<Current>(*frame, frame->state, NativeRoute{ring, slot});
  [[clang::musttail]] return NativeRing::yield(ring, slot, frame);
}
[[gnu::always_inline]] inline void manual(Frame& frame) {
  switch (frame.stage) {
    case Stage::directory: body<Stage::directory>(frame, frame.state, ManualRoute{}); break;
    case Stage::bind: body<Stage::bind>(frame, frame.state, ManualRoute{}); break;
    case Stage::order: body<Stage::order>(frame, frame.state, ManualRoute{}); break;
    case Stage::probe: body<Stage::probe>(frame, frame.state, ManualRoute{}); break;
    case Stage::payload: body<Stage::payload>(frame, frame.state, ManualRoute{}); break;
  }
}
void direct(Frame& frame) {
  auto& state = frame.state;
  do {
    frame.page = select(*frame.source, state.key);
    if (!frame.page) { consume_absent(*frame.output, state.key); break; }
    hint_page(*frame.page);
    bind_page(frame, state);
    state.first = 0;
    if (!state.last) { consume_absent(*frame.output, state.key); break; }
    hint_bindings(*frame.binding);
    while (state.first < state.last) {
      state.mid = state.first + (state.last - state.first) / 2;
      hint_order(frame, state.mid);
      state.physical = frame.order[state.mid];
      hint_tuple(frame, state.physical);
      const auto found = packed::read_key(*frame.binding, state.physical);
      if (found == state.key) break;
      if (found < state.key) state.first = state.mid + 1;
      else state.last = state.mid;
    }
    if (state.first == state.last) { consume_absent(*frame.output, state.key); break; }
    hint_value(frame, state.physical);
    const auto value = packed::record(*frame.binding, state.physical);
    consume_row(*frame.output, state.key, value);
    state.key = value.next;
  } while (--frame.remaining);
  frame.done = true;
}
} // namespace

void Driver::prepare(loom::Worker& worker, ReadExecution execution, std::size_t active_width) {
  if (execution != ReadExecution::native_direct && execution != ReadExecution::manual &&
      execution != ReadExecution::ring) throw std::invalid_argument("unknown native read execution");
  if (!active_width || active_width > frames_.size())
    throw std::invalid_argument("native active width must be 1..64");
  if (ring_.pending()) throw std::logic_error("prepare across native lookup borrowers");
  if (ring_.configured() && active_width != active_width_) {
    if (!ring_.close()) throw std::runtime_error("native lookup Ring reconfiguration failed");
  }
  active_width_ = active_width;
  if (execution == ReadExecution::ring && !ring_.configured()) {
    auto configured = ring_.configure(worker, {active_width_, frames_.size() - active_width_});
    if (!configured) throw std::runtime_error("native lookup Ring allocation failed");
  }
}
void Driver::run(loom::Worker& worker, const Source& source, std::span<const Operation> input,
    std::span<Outcome> output, ReadExecution execution) {
  if (input.size() != output.size() || input.size() > frames_.size() ||
      (execution != ReadExecution::native_direct && execution != ReadExecution::manual &&
       execution != ReadExecution::ring))
    throw std::invalid_argument("native lookup requires equal bounded read-only batch spans");
  for (const auto& operation : input)
    if (!lookup_batch_eligible(operation.kind)) throw std::invalid_argument("native lookup cannot batch writes");
  if (ring_.pending()) throw std::logic_error("nested native lookup batch");
  if (input.empty()) return;
  for (std::size_t i = 0; i < input.size(); ++i) {
    output[i] = {};
    frames_[i] = {};
    auto& frame = frames_[i];
    frame.source = &source; frame.output = &output[i]; frame.state.key = input[i].key;
    frame.remaining = input[i].kind == Kind::chain ? input[i].count : 1;
    frame.done = frame.remaining == 0;
  }
  if (execution == ReadExecution::native_direct) {
    for (std::size_t i = 0; i < input.size(); ++i) if (!frames_[i].done) direct(frames_[i]);
  } else if (execution == ReadExecution::manual) {
    std::array<Frame*, 64> active{};
    std::size_t admitted = 0, live = 0;
    auto admit = [&]() -> Frame* {
      while (admitted < input.size()) {
        auto* frame = &frames_[admitted++];
        if (!frame->done) return frame;
      }
      return nullptr;
    };
    const auto width = std::min(input.size(), active_width_);
    for (std::size_t i = 0; i < width; ++i) if ((active[i] = admit())) ++live;
    while (live) {
      for (std::size_t i = 0; i < width; ++i) if (auto* frame = active[i]) {
        manual(*frame);
        if (frame->done) {
          active[i] = admit();
          if (!active[i]) --live;
        }
      }
    }
  } else if (execution == ReadExecution::ring) {
    // One reusable worker-local bank. First use includes configuration unless
    // the enclosing measured caller has explicitly prepared it beforehand.
    prepare(worker, execution, active_width_);
    struct BorrowGuard {
      NativeRing& ring;
      ~BorrowGuard() { if (ring.pending()) ring.discard(); }
    } guard{ring_};
    for (std::size_t i = 0; i < input.size(); ++i) {
      auto& frame = frames_[i];
      if (!frame.done && !ring_.push(native<Stage::directory>, {&frame}))
        throw std::runtime_error("bounded native batch exceeded configured inventory");
    }
    while (ring_.pending()) ring_.spin({128});
  } else throw std::invalid_argument("unknown native read execution");
}
void Driver::close() noexcept {
  assert(!ring_.pending());
  const auto result = ring_.close();
  assert(result.has_value());
}
} // namespace orbital_bench::lookup
