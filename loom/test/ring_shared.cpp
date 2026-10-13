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
    std::cerr << "loom_ring_shared_check: " << message << '\n';
    std::abort();
  }
}
template<class T> void check(const orbital::Result<T>& value, const char* message) {
  check(value.has_value(), message);
}

struct Fixture {
  loom::Worker worker;
  Fixture() {
    check(loom::adopt(worker), "adopt Worker");
    auto arena = orbital::allocation::open_arena(8 * 1024 * 1024);
    check(arena, "open arena");
    check(loom::configure(worker, std::move(*arena), 0), "configure Worker");
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

using Vector = std::uint64_t __attribute__((vector_size(16)));
struct Context { std::uint64_t epoch{}, factor{}; loom::HintPressure pressure{}; };
struct Event {
  unsigned id{}, phase{};
  std::uint64_t remaining{}, sum{}, seed{};
  const Context* context{};
  std::uint64_t epoch{}, factor{}, common0{}, common1{}, local0{}, local1{};
  unsigned pressure{};
  bool operator==(const Event&) const = default;
};
struct Job {
  unsigned id{}, cycles{}, done{};
  bool checked_boundary{};
  std::uint64_t output{}, vector0{}, vector1{};
  std::vector<Event>* events{};
};
using Base = loom::Ring<Job*, std::uint64_t, std::uint64_t, Vector>;
using Ring = Base::WithShared<std::uint64_t, const Context*, Vector>;

// Test observation uses ordinary frames: no temporary Event or shared tuple
// needs an O3 ASan fake-stack allocation in the native calling convention.
[[gnu::noinline]] void record(std::vector<Event>* events, unsigned id, unsigned phase,
    std::uint64_t remaining, std::uint64_t sum, std::uint64_t seed, const Context* context,
    std::uint64_t epoch, std::uint64_t factor, std::uint64_t common0, std::uint64_t common1,
    std::uint64_t local0, std::uint64_t local1, unsigned pressure) {
  events->push_back({id, phase, remaining, sum, seed, context, epoch, factor,
      common0, common1, local0, local1, pressure});
}
[[gnu::noinline]] void reject_native_change(Ring* ring) {
  check(!ring->set_shared({999, nullptr, Vector{99, 101}}), "shared prefix changed within a spin");
}

template<bool Other> LOOM_NATIVE_CC void metadata(Ring::Cursor, Ring::Slot*,
    std::uint64_t, const Context*, Vector, Job*, std::uint64_t, std::uint64_t, Vector);
template<bool Other> LOOM_NATIVE_CC void payload(Ring::Cursor, Ring::Slot*,
    std::uint64_t, const Context*, Vector, Job*, std::uint64_t, std::uint64_t, Vector);

template<bool Other>
LOOM_NATIVE_CC void initial(Ring::Cursor ring, Ring::Slot* slot, std::uint64_t seed,
    const Context* context, Vector common, Job* job, std::uint64_t remaining,
    std::uint64_t sum, Vector local) {
  record(job->events, job->id, 2, remaining, sum, seed, context, context->epoch,
      context->factor, common[0], common[1], local[0], local[1], unsigned(context->pressure));
  reject_native_change(ring.operator->());
  job->checked_boundary = true;
  slot->function = metadata<Other>;
  [[clang::musttail]] return Ring::yield_unchanged(ring, slot, seed, context, common,
      job, remaining, sum, local);
}
template<bool Other>
LOOM_NATIVE_CC void metadata(Ring::Cursor ring, Ring::Slot* slot, std::uint64_t seed,
    const Context* context, Vector common, Job* job, std::uint64_t remaining,
    std::uint64_t sum, Vector local) {
  record(job->events, job->id, 0, remaining, sum, seed, context, context->epoch,
      context->factor, common[0], common[1], local[0], local[1], unsigned(context->pressure));
  const auto value = context->factor * (job->id + 1) + 3 * remaining + seed + context->epoch;
  const auto delta = Other ? value ^ 0x33 : value;
  sum += delta;
  local += common + Vector{delta, delta ^ 0x5a};
  slot->function = payload<Other>;
  [[clang::musttail]] return Ring::yield(ring, slot, seed, context, common, job, remaining, sum, local);
}
template<bool Other>
LOOM_NATIVE_CC void payload(Ring::Cursor ring, Ring::Slot* slot, std::uint64_t seed,
    const Context* context, Vector common, Job* job, std::uint64_t remaining,
    std::uint64_t sum, Vector local) {
  record(job->events, job->id, 1, remaining, sum, seed, context, context->epoch,
      context->factor, common[0], common[1], local[0], local[1], unsigned(context->pressure));
  const auto delta = (job->id ^ 0x31) + remaining + 2 * context->factor + seed +
      (Other ? context->epoch : 3 * context->epoch);
  local ^= common + Vector{delta + 7, 3 * delta};
  sum += local[0] ^ local[1] ^ delta;
  if (!--remaining) {
    job->output = sum + local[0] + 3 * local[1];
    job->vector0 = local[0]; job->vector1 = local[1];
    ++job->done;
    if constexpr (Other) {
      [[clang::musttail]] return Ring::finish(ring, slot, seed, context, common,
          job, remaining, sum, local);
    } else ring->complete(*slot);
  } else slot->function = metadata<Other>;
  [[clang::musttail]] return Ring::yield(ring, slot, seed, context, common, job, remaining, sum, local);
}

void shared_prefix(Fixture& fixture, unsigned width, unsigned overflow_size) {
  constexpr std::array<unsigned, 8> lengths{1, 4, 2, 7, 3, 5, 1, 6};
  std::vector<Event> actual_events;
  std::array<std::unique_ptr<Job>, 8> jobs;
  for (unsigned id = 0; id < jobs.size(); ++id) {
    jobs[id] = std::make_unique<Job>();
    jobs[id]->id = id; jobs[id]->cycles = lengths[id]; jobs[id]->events = &actual_events;
  }
  auto context = std::make_unique<Context>(Context{3, 5, loom::HintPressure::low});
  std::uint64_t seed = 17, common0 = 41, common1 = 83;
  Ring ring;
  check(!ring.set_shared({seed, context.get(), Vector{common0, common1}}), "unconfigured prefix accepted");
  check(ring.configure(fixture.worker, {width, overflow_size},
      {seed, context.get(), Vector{common0, common1}}), "configure shared Ring");
  const auto count = width + overflow_size;
  check(count <= jobs.size(), "fixture exceeds fixed identity inventory");

  struct State {
    unsigned phase{2}, remaining{}, visits{}, done{};
    std::uint64_t sum{}, x0{}, x1{};
  };
  std::array<State, 8> expected;
  std::vector<unsigned> active, overflow;
  std::uint64_t visits = 0, budget_stops = 0, promoted = 0;
  unsigned abandoned = 0;
  for (unsigned id = 0; id < count; ++id) {
    expected[id] = {2, lengths[id], 0, 0, id * 11, id + 1, 2 * id + 3};
    const auto function = id & 1 ? initial<true> : initial<false>;
    check(ring.push(function, {jobs[id].get(), lengths[id], id * 11, Vector{id + 1, 2 * id + 3}}),
        "local argument admission");
    if (id < width) active.push_back(id); else overflow.push_back(id);
  }
  check(!ring.push(initial<false>, {jobs[7].get(), 1, 0, Vector{}}), "finite shared inventory accepted extra work");

  const auto verify = [&] {
    check(ring.pending() == active.size() + overflow.size(), "shared pending differs from logical inventory");
    check(!ring.release_if([&](const Ring::Arguments& arguments) noexcept {
      const auto* job = std::get<0>(arguments);
      check(job && jobs[job->id].get() == job, "local identity changed or cancelled borrower retained");
      const auto& state = expected[job->id];
      const auto value = std::get<3>(arguments);
      check(std::get<1>(arguments) == state.remaining && std::get<2>(arguments) == state.sum &&
          value[0] == state.x0 && value[1] == state.x1, "local scalar/vector persistence differs");
      return false;
    }), "argument inspection removed a borrower");
    for (unsigned id = 0; id < count; ++id) {
      if (!jobs[id]) continue;
      const auto& state = expected[id];
      check(jobs[id]->done == state.done && jobs[id]->checked_boundary == bool(state.visits),
          "shared family initial/completion count differs");
      if (state.done)
        check(jobs[id]->output == state.sum + state.x0 + 3 * state.x1 &&
            jobs[id]->vector0 == state.x0 && jobs[id]->vector1 == state.x1,
            "independent scalar-lane result differs");
    }
    const auto& counters = ring.counters();
    unsigned completed = 0;
    for (const auto& state : expected) completed += state.done;
    check(counters.accepted == count && counters.refused == 1 && counters.completed == completed &&
        counters.abandoned == abandoned && counters.visits == visits &&
        counters.promoted == promoted && counters.budget_stops == budget_stops,
        "shared finite-work accounting differs");
  };
  const auto spin = [&](std::uint64_t budget) {
    std::vector<Event> events;
    for (std::uint64_t visit = 0; visit < budget && !active.empty(); ++visit) {
      const auto id = active.front();
      std::rotate(active.begin(), active.begin() + 1, active.end());
      auto& state = expected[id];
      events.push_back({id, state.phase, state.remaining, state.sum, seed, context.get(),
          context->epoch, context->factor, common0, common1, state.x0, state.x1, unsigned(context->pressure)});
      ++state.visits;
      if (state.phase == 2) state.phase = 0;
      else if (state.phase == 0) {
        auto delta = context->factor * (id + 1) + 3 * state.remaining + seed + context->epoch;
        if (id & 1) delta ^= 0x33;
        state.sum += delta;
        state.x0 += common0 + delta;
        state.x1 += common1 + (delta ^ 0x5a);
        state.phase = 1;
      } else {
        const auto delta = (id ^ 0x31) + state.remaining + 2 * context->factor + seed +
            (id & 1 ? context->epoch : 3 * context->epoch);
        state.x0 ^= common0 + delta + 7;
        state.x1 ^= common1 + 3 * delta;
        state.sum += state.x0 ^ state.x1 ^ delta;
        if (!--state.remaining) {
          ++state.done;
          if (overflow.empty()) active.pop_back();
          else { active.back() = overflow.front(); overflow.erase(overflow.begin()); ++promoted; }
        } else state.phase = 0;
      }
    }
    actual_events.clear();
    check(ring.spin({budget}) == events.size(), "shared exact spin budget differs");
    check(actual_events == events, "shared prefix/local state/phase trace differs");
    visits += events.size();
    if (budget && events.size() == budget && (!active.empty() || !overflow.empty())) ++budget_stops;
    verify();
  };

  spin(0); spin(1); spin(width + 1);
  // Every pending identity switches to the same new facts at a returned
  // boundary. Ending the old physical Context catches stale per-slot prefixes.
  auto replacement = std::make_unique<Context>(Context{29, 7, loom::HintPressure::busy});
  seed = 103; common0 = 211; common1 = 307;
  check(loom::set_hint_pressure(fixture.worker, replacement->pressure), "set coarse pressure");
  check(ring.refresh_hints(), "refresh pending hardware/pressure policy");
  check(ring.set_shared({seed, replacement.get(), Vector{common0, common1}}), "refresh shared facts");
  context = std::move(replacement);
  spin(2);

  if (count > 3) {
    // Exercise both queued cleanup and removal after local scalar/vector state
    // has changed in an active frame.
    const auto id = width >= 5 ? width - 1 : count - 1;
    const auto* removed = jobs[id].get();
    const auto found_active = std::find(active.begin(), active.end(), id) != active.end();
    const auto found_overflow = std::find(overflow.begin(), overflow.end(), id) != overflow.end();
    check(found_active || found_overflow, "chosen cancellation already finished");
    check(ring.release_if([removed](const Ring::Arguments& arguments) noexcept {
      return std::get<0>(arguments) == removed;
    }) == 1, "selective shared cleanup removed wrong identity");
    std::erase(active, id); std::erase(overflow, id); ++abandoned;
    while (active.size() < width && !overflow.empty()) {
      active.push_back(overflow.front()); overflow.erase(overflow.begin()); ++promoted;
    }
    jobs[id].reset();
    verify();
  }
  check(!ring.close(), "live shared storage closed");
  unsigned iteration = 0;
  const std::array<std::uint64_t, 6> budgets{1, 0, 3, 7, 2, 99};
  while (ring.pending()) {
    check(iteration < 200, "shared drain stalled");
    spin(budgets[iteration++ % budgets.size()]);
  }
  spin(1);
  check(ring.close(), "close shared native family");
  check(!ring.set_shared({seed, context.get(), Vector{common0, common1}}), "closed prefix accepted");
  context.reset();
  check(loom::set_hint_pressure(fixture.worker, loom::HintPressure::low), "restore fixture pressure");
}
} // namespace

int main() {
  Fixture fixture;
  shared_prefix(fixture, 1, 2);
  shared_prefix(fixture, 3, 4);
  shared_prefix(fixture, 5, 2);
  std::cout << "loom_ring_shared_check: passed\n";
}
