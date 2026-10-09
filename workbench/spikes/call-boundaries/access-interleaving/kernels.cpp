#include "fixture.h"

namespace access_spike {
namespace {
struct Lane {
  Frame* frame{};
  Word row{}, remaining{}, sum{};
  Carrier accumulated{};
};

[[gnu::always_inline]] inline void finish(Lane& lane) {
  auto& frame = *lane.frame;
  frame.output = lane.accumulated;
  frame.sum = lane.sum;
  frame.final_row = lane.row;
  frame.visits = frame.steps;
  ++frame.done;
}
[[gnu::always_inline]] inline void hint(const loom::HintPolicy& policy, const Lane& lane) {
  loom::prefetch(policy, lane.frame->demand,
                 lane.frame->selected->source.row_unchecked(lane.row));
}
[[gnu::always_inline]] inline void advance(Lane& lane, tp::native::packet decoded,
                                         const loom::HintPolicy& policy) {
  const Word first = tp::native::word<0>(decoded);
  lane.accumulated = Carrier::from(tp::native::bit_or(lane.accumulated.get(), decoded));
  lane.sum = checksum(lane.sum, first, lane.row, lane.frame->identity);
  lane.row = successor(first, lane.frame->selected->mask);
  if (--lane.remaining) hint(policy, lane);
  else finish(lane);
}

template<std::size_t... I>
[[gnu::always_inline]] inline void manual(Frame* frames, std::size_t count,
    const loom::HintPolicy& policy, std::index_sequence<I...>) {
  std::array<Lane, sizeof...(I)> lanes{};
  ((I < count ? (void)(lanes[I] = {frames + I, frames[I].start, frames[I].steps},
                       hint(policy, lanes[I])) : (void)0), ...);
  // Each pass admits no replacement: manual and Ring use the same fixed group
  // and vacant lanes remain vacant. There is no hidden immediate-refill advantage.
  while ((bool(lanes[I].remaining) | ...)) {
    std::array<tp::native::packet, sizeof...(I)> decoded{};
    // Place independent native reads before their dependent cursor/hint updates.
    ((lanes[I].remaining ? (void)(decoded[I] =
        lanes[I].frame->selected->reader.get_unchecked(lanes[I].row)) : (void)0), ...);
    ((lanes[I].remaining ? (void)advance(lanes[I], decoded[I], policy) : (void)0), ...);
  }
}
} // namespace

extern "C" [[gnu::noinline]] void access_scalar(Frame* frames, std::size_t count,
    const loom::HintPolicy& policy) {
  for (std::size_t i = 0; i < count; ++i) {
    Lane lane{frames + i, frames[i].start, frames[i].steps};
    hint(policy, lane);
    do {
      const auto decoded = lane.frame->selected->reader.get_unchecked(lane.row);
      advance(lane, decoded, policy);
    } while (lane.remaining);
  }
}
#define ACCESS_MANUAL(W) \
  extern "C" [[gnu::noinline]] void access_manual_##W(Frame* frames, std::size_t count, \
      const loom::HintPolicy& policy) { manual(frames, count, policy, std::make_index_sequence<W>{}); }
ACCESS_MANUAL(1)
ACCESS_MANUAL(2)
ACCESS_MANUAL(4)
ACCESS_MANUAL(8)
#undef ACCESS_MANUAL

template<std::size_t... V>
[[gnu::noinline]] LOOM_NATIVE_CC void Family<std::index_sequence<V...>>::work(
    Ring* ring, typename Ring::Slot* slot, Frame* frame, Word row, Word remaining,
    Word sum, Part<V>... values) {
  const auto decoded = frame->selected->reader.get_unchecked(row);
  const Word first = tp::native::word<0>(decoded);
  const Carrier accumulator{{values...}};
  const auto next = Carrier::from(tp::native::bit_or(accumulator.get(), decoded));
  sum = checksum(sum, first, row, frame->identity);
  row = successor(first, frame->selected->mask);
  if (!--remaining) {
    frame->output = next;
    frame->sum = sum;
    frame->final_row = row;
    frame->visits = frame->steps;
    ++frame->done;
    ring->complete(*slot);
  } else {
    ring->prefetch(frame->demand, frame->selected->source.row_unchecked(row));
  }
  [[clang::musttail]] return Ring::yield(ring, slot, frame, row, remaining, sum, next.v[V]...);
}
template<std::size_t... V>
bool Family<std::index_sequence<V...>>::enqueue(Ring& ring, Frame& frame) {
  return ring.push(work, typename Ring::Arguments{
      &frame, frame.start, frame.steps, 0, Part<V>{}...},
      loom::MemoryDependency{frame.selected->source.row_unchecked(frame.start), frame.demand});
}
template struct Family<std::make_index_sequence<Carrier::parts>>;
} // namespace access_spike
