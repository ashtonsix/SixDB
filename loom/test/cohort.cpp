#include <loom/cohort.h>
#include <loom/ring.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "loom_cohort_check: " << message << '\n';
    std::abort();
  }
}
template<class T> T take(orbital::Result<T> value) {
  check(value.has_value(), "checked operation failed");
  return std::move(*value);
}

struct State {
  unsigned id{}, remaining{}, visits{}, steps{}, prepared{};
  std::uint64_t sum{};
};

void masks_and_ragged_work() {
  using Group = loom::Cohort<State, 8>;
  std::array<State, 6> states;
  const std::array<unsigned, 6> lengths{1, 8, 0, 2, 7, 3};
  for (unsigned i = 0; i < states.size(); ++i) states[i] = {i, lengths[i]};
  auto cohort = take(Group::bind(states, 0b100011));
  check(cohort.pending() == 3, "partial mask cardinality");
  unsigned rounds = 0;
  while (!cohort.empty()) {
    check(++rounds <= 8, "ragged cohort stalled");
    cohort.visit([](State& state) noexcept {
      ++state.visits;
      state.prepared = 3 * state.id + state.remaining;
    });
    cohort.advance([](State& state) noexcept {
      ++state.steps;
      state.sum += state.prepared;
      return --state.remaining == 0;
    });
  }
  for (unsigned i = 0; i < states.size(); ++i) {
    const auto selected = i == 0 || i == 1 || i == 5;
    const auto n = selected ? lengths[i] : 0;
    const auto expected = std::uint64_t(3 * i) * n + std::uint64_t(n) * (n + 1) / 2;
    check(states[i].sum == expected && states[i].visits == n && states[i].steps == n,
        "independent ragged sum or inactive-state preservation");
    check(states[i].remaining == (selected ? 0 : lengths[i]), "inactive remaining count changed");
  }
  cohort.visit([](State&) noexcept { check(false, "completed state revisited"); });
  cohort.advance([](State&) noexcept { check(false, "completed state advanced"); return false; });

  auto empty = take(Group::bind({}));
  auto zero = take(Group::bind(states, 0));
  check(empty.empty() && zero.empty() && !empty.pending() && !zero.pending(), "zero/empty binding");
  empty.visit([](State&) noexcept { check(false, "empty span dereferenced"); });
  zero.advance([](State&) noexcept { check(false, "zero mask dereferenced"); return false; });
  std::array<State, 9> oversized{};
  check(!Group::bind(oversized) && !Group::bind(oversized, 0), "oversized span accepted");
  check(!Group::bind(std::span(states).first(3), 8), "bit beyond actual span accepted");
  check(!Group::bind({}, 1), "nonempty mask accepted over empty span");

  std::array<State, 64> wide{};
  for (unsigned i = 0; i < wide.size(); ++i) wide[i].id = i;
  auto last = take(loom::Cohort<State, 64>::bind(wide, std::uint64_t{1} << 63));
  last.visit([](State& state) noexcept { check(state.id == 63, "top bit selected another state"); ++state.visits; });
  last.advance([](State&) noexcept { return true; });
  check(last.empty() && wide[63].visits == 1, "top bit completion");
  auto full = take(loom::Cohort<State, 64>::bind(wide));
  check(full.pending() == 64, "full 64-bit mask cardinality");
  full.advance([](State&) noexcept { return true; });
  check(full.empty(), "full 64-bit mask did not drain");
}

struct Fixture {
  loom::Worker worker;
  Fixture() {
    check(bool(loom::adopt(worker)), "adopt Worker");
    auto arena = take(orbital::allocation::open_arena(8 * 1024 * 1024));
    check(bool(loom::configure(worker, std::move(arena), 0)), "configure Worker");
  }
  ~Fixture() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto progress = loom::shutdown(worker);
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(progress.kind() != orbital::Progress::Kind::failed &&
          std::chrono::steady_clock::now() < deadline, "Worker drain");
      loom::poll(worker, {256, 0});
      std::this_thread::yield();
    }
    loom::relinquish(worker);
  }
};

struct Event {
  unsigned frame{}, phase{}, pressure{}, contexts{};
  bool operator==(const Event&) const = default;
};
// Event construction and vector growth use an ordinary frame, keeping O3 ASan
// fake-stack temporaries out of the native callback's calling convention.
[[gnu::noinline]] void record_event(std::vector<Event>* events, unsigned frame,
    unsigned phase, unsigned pressure, unsigned contexts) {
  events->push_back({frame, phase, pressure, contexts});
}
struct A { unsigned id{}, left{}, staged{}; std::uint64_t sum{}; };
struct B { unsigned id{}, left{}; std::uint64_t staged{}, sum{}; };
struct FrameA {
  std::array<A, 2> states;
  loom::Cohort<A, 2> cohort;
  unsigned id{}, completed{};
  std::vector<Event>* events{};
  FrameA(unsigned id, unsigned first, unsigned second, std::vector<Event>& events)
      : states{{{2 * id, first}, {2 * id + 1, second}}},
        cohort(take(loom::Cohort<A, 2>::bind(states))), id(id), events(&events) {}
};
struct FrameB {
  std::array<B, 3> states;
  loom::Cohort<B, 4> cohort;
  unsigned id{}, completed{};
  std::vector<Event>* events{};
  FrameB(unsigned id, std::array<unsigned, 3> lengths, std::vector<Event>& events)
      : states{{{3 * id, lengths[0]}, {3 * id + 1, lengths[1]}, {3 * id + 2, lengths[2]}}},
        cohort(take(loom::Cohort<B, 4>::bind(states))), id(id), events(&events) {}
};
using Ring = loom::Ring<void*>;
LOOM_NATIVE_CC void a_payload(Ring::Cursor, Ring::Slot*, void*);
LOOM_NATIVE_CC void b_payload(Ring::Cursor, Ring::Slot*, void*);

LOOM_NATIVE_CC void a_metadata(Ring::Cursor ring, Ring::Slot* slot, void* erased) {
  auto& frame = *static_cast<FrameA*>(erased);
  const auto pressure = unsigned(ring->hint_policy().pressure);
  record_event(frame.events, frame.id, 0, pressure, unsigned(frame.cohort.pending()));
  frame.cohort.visit([pressure](A& state) noexcept {
    state.staged = 7 * (state.id + 1) + 3 * state.left + 100 * pressure;
  });
  slot->function = a_payload;
  [[clang::musttail]] return Ring::yield_unchanged(ring, slot, erased);
}
LOOM_NATIVE_CC void a_payload(Ring::Cursor ring, Ring::Slot* slot, void* erased) {
  auto& frame = *static_cast<FrameA*>(erased);
  record_event(frame.events, frame.id, 1, unsigned(ring->hint_policy().pressure), unsigned(frame.cohort.pending()));
  frame.cohort.advance([](A& state) noexcept { state.sum += state.staged; return --state.left == 0; });
  if (frame.cohort.empty()) {
    ++frame.completed;
    [[clang::musttail]] return Ring::finish(ring, slot, erased);
  }
  slot->function = a_metadata;
  [[clang::musttail]] return Ring::yield_unchanged(ring, slot, erased);
}
LOOM_NATIVE_CC void b_metadata(Ring::Cursor ring, Ring::Slot* slot, void* erased) {
  auto& frame = *static_cast<FrameB*>(erased);
  const auto pressure = unsigned(ring->hint_policy().pressure);
  record_event(frame.events, frame.id, 0, pressure, unsigned(frame.cohort.pending()));
  frame.cohort.visit([pressure](B& state) noexcept {
    state.staged = ((state.id + 13) ^ state.left) + 11 * pressure;
  });
  slot->function = b_payload;
  [[clang::musttail]] return Ring::yield_unchanged(ring, slot, erased);
}
LOOM_NATIVE_CC void b_payload(Ring::Cursor ring, Ring::Slot* slot, void* erased) {
  auto& frame = *static_cast<FrameB*>(erased);
  record_event(frame.events, frame.id, 1, unsigned(ring->hint_policy().pressure), unsigned(frame.cohort.pending()));
  frame.cohort.advance([](B& state) noexcept { state.sum = 3 * state.sum + state.staged; return --state.left == 0; });
  if (frame.cohort.empty()) {
    ++frame.completed;
    [[clang::musttail]] return Ring::finish(ring, slot, erased);
  }
  slot->function = b_metadata;
  [[clang::musttail]] return Ring::yield_unchanged(ring, slot, erased);
}

void shared_native_family(Fixture& fixture) {
  Ring ring;
  check(bool(ring.configure(fixture.worker, {2, 2})), "configure shared native family");
  std::vector<Event> actual_events;
  FrameA a(0, 1, 4, actual_events);
  FrameB b(1, {2, 5, 1}, actual_events);
  auto cancelled = std::make_unique<FrameA>(2, 9, 8, actual_events);
  FrameB later(3, {3, 2, 4}, actual_events);
  check(ring.push(a_metadata, {cancelled.get()}) && ring.push(b_metadata, {&b}) &&
      ring.push(a_metadata, {&a}) && ring.push(b_metadata, {&later}), "heterogeneous admission");
  check(!ring.push(b_metadata, {&later}), "bounded family admitted extra work");

  struct Expected {
    bool kind_a{};
    unsigned phase{}, completed{};
    std::vector<unsigned> ids, left;
    std::vector<std::uint64_t> staged, sums;
  };
  std::array<Expected, 4> expected;
  const auto initialise = [&]<class Frame>(const Frame& frame, bool kind_a) {
    auto& state = expected[frame.id];
    state.kind_a = kind_a;
    for (const auto& member : frame.states) {
      state.ids.push_back(member.id);
      state.left.push_back(member.left);
      state.staged.push_back(0);
      state.sums.push_back(0);
    }
  };
  initialise(a, true); initialise(b, false); initialise(*cancelled, true); initialise(later, false);
  std::vector<unsigned> active{2, 1}, overflow{0, 3};
  std::uint64_t visits = 0;
  const auto verify_frame = [&]<class Frame>(const Frame& frame) {
    const auto& state = expected[frame.id];
    unsigned contexts = 0;
    for (unsigned i = 0; i < frame.states.size(); ++i) {
      contexts += state.left[i] != 0;
      check(frame.states[i].left == state.left[i] && frame.states[i].sum == state.sums[i],
          "heterogeneous state result differs from independent execution");
    }
    check(frame.completed == state.completed && frame.cohort.pending() == contexts,
        "cohort cardinality/completion differs from remaining work");
  };
  const auto spin = [&](unsigned budget, unsigned pressure) {
    std::vector<Event> events;
    for (unsigned visit = 0; visit < budget && !active.empty(); ++visit) {
      const auto id = active.front();
      std::rotate(active.begin(), active.begin() + 1, active.end());
      auto& state = expected[id];
      const auto contexts = std::count_if(state.left.begin(), state.left.end(), [](unsigned n) { return n != 0; });
      events.push_back({id, state.phase, pressure, unsigned(contexts)});
      for (unsigned i = 0; i < state.left.size(); ++i) {
        if (!state.left[i]) continue;
        if (!state.phase) {
          state.staged[i] = state.kind_a ? 7 * (state.ids[i] + 1) + 3 * state.left[i] + 100 * pressure
              : ((state.ids[i] + 13) ^ state.left[i]) + 11 * pressure;
        } else {
          state.sums[i] = state.kind_a ? state.sums[i] + state.staged[i]
              : 3 * state.sums[i] + state.staged[i];
          --state.left[i];
        }
      }
      if (state.phase && std::all_of(state.left.begin(), state.left.end(), [](unsigned n) { return n == 0; })) {
        ++state.completed;
        if (overflow.empty()) active.pop_back();
        else { active.back() = overflow.front(); overflow.erase(overflow.begin()); }
      } else state.phase ^= 1;
    }
    actual_events.clear();
    check(ring.spin({budget}) == events.size(), "shared cohort exact visit budget");
    check(actual_events == events, "mixed cohort stage/pressure/rotation trace differs");
    visits += events.size();
    check(ring.pending() == active.size() + overflow.size(), "shared cohort pending inventory");
    verify_frame(a); verify_frame(b); verify_frame(later);
    if (cancelled) verify_frame(*cancelled);
  };

  check(bool(loom::set_hint_pressure(fixture.worker, loom::HintPressure::low)) &&
      bool(ring.refresh_hints()), "initial coarse policy");
  spin(0, 0); spin(3, 0);
  check(bool(loom::set_hint_pressure(fixture.worker, loom::HintPressure::busy)) &&
      bool(ring.refresh_hints()), "pending shared policy refresh");
  spin(2, 1);
  const auto* removed = cancelled.get();
  check(ring.release_if([removed](const Ring::Arguments& arguments) noexcept {
    return std::get<0>(arguments) == removed;
  }) == 1, "selective cohort cleanup");
  std::erase(active, 2u);
  active.push_back(overflow.front()); overflow.erase(overflow.begin());
  cancelled.reset(); // The removed cohort's span and mutable states really end.
  check(!ring.close(), "live mixed family storage closed");
  spin(1, 1);
  check(bool(loom::set_hint_pressure(fixture.worker, loom::HintPressure::low)) &&
      bool(ring.refresh_hints()), "second pending shared policy refresh");
  unsigned iteration = 0;
  const std::array<unsigned, 5> budgets{1, 4, 0, 2, 7};
  while (ring.pending()) {
    check(iteration < 100, "mixed cohort drain stalled");
    spin(budgets[iteration++ % budgets.size()], 0);
  }
  const auto& counters = ring.counters();
  check(counters.accepted == 4 && counters.refused == 1 && counters.completed == 3 &&
      counters.abandoned == 1 && counters.visits == visits, "mixed cohort finite work accounting");
  check(bool(ring.close()), "close shared native family");
}
} // namespace

int main() {
  masks_and_ragged_work();
  Fixture fixture;
  shared_native_family(fixture);
  std::cout << "loom_cohort_check: passed\n";
}
