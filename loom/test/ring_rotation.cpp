#include <loom/ring.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "loom_ring_rotation_check: " << message << '\n';
    std::abort();
  }
}

template<class T> void check(const orbital::Result<T>& result, const char* message) {
  check(result.has_value(), message);
}

struct Fixture {
  loom::Worker worker;

  Fixture() {
    check(loom::adopt(worker), "adopt lane");
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

struct Scenario;
struct Job {
  Scenario* scenario{};
  unsigned id{}, cycles{};
  std::array<unsigned, 3> children{};
  unsigned child_count{};
  bool terminal_admission{}, attempted{};
  std::uint64_t output{};
  unsigned done{};
};

using Ring = loom::Ring<Job*, unsigned, std::uint64_t>;

struct Event {
  unsigned id{}, phase{}, remaining{};
  std::uint64_t state{};
  bool operator==(const Event&) const = default;
};

struct Admission {
  unsigned parent{}, child{};
  bool accepted{};
  bool operator==(const Admission&) const = default;
};

// Keep trace temporaries in an ordinary frame. O3 ASan fake-stack realignment
// is unsupported inside the native convention; state and handoffs stay checked.
[[gnu::noinline]] void record_event(std::vector<Event>* events, unsigned id,
    unsigned phase, unsigned remaining, std::uint64_t state) {
  events->push_back({id, phase, remaining, state});
}

[[gnu::noinline]] void record_admission(std::vector<Admission>* admissions,
    unsigned parent, unsigned child, bool accepted) {
  admissions->push_back({parent, child, accepted});
}

LOOM_NATIVE_CC void metadata(Ring::Cursor, Ring::Slot*, Job*, unsigned, std::uint64_t);
LOOM_NATIVE_CC void payload(Ring::Cursor, Ring::Slot*, Job*, unsigned, std::uint64_t);

// This oracle stores logical identities in rotation order, without Ring Slots,
// marker state, link pointers or the native implementation's budgeting fields.
struct Model {
  struct State {
    unsigned phase{}, remaining{};
    std::uint64_t value{};
    bool attempted{}, admitted{}, finished{}, abandoned{};
  };
  loom::RingCapacity capacity;
  std::array<State, 32> state{};
  std::vector<unsigned> active, overflow;
  loom::RingCounters counters;

  explicit Model(loom::RingCapacity capacity) : capacity(capacity) {}

  bool push(const Job& job) {
    if (active.size() + overflow.size() == capacity.active + capacity.overflow) {
      ++counters.refused;
      return false;
    }
    check(!state[job.id].admitted, "oracle admitted an identity twice");
    state[job.id] = {0, job.cycles, job.id + 1, false, true, false, false};
    ++counters.accepted;
    if (active.size() < capacity.active) active.push_back(job.id);
    else overflow.push_back(job.id);
    return true;
  }

  std::uint64_t spin(std::uint64_t budget, const std::array<Job, 32>& jobs,
      std::vector<Event>& events, std::vector<Admission>& admissions) {
    std::uint64_t visited = 0;
    while (visited < budget && !active.empty()) {
      const auto id = active.front();
      std::rotate(active.begin(), active.begin() + 1, active.end());
      auto& current = state[id];
      const auto& job = jobs[id];
      events.push_back({id, current.phase, current.remaining, current.value});
      const auto admit_children = [&] {
        current.attempted = true;
        for (unsigned i = 0; i < job.child_count; ++i) {
          const auto child = job.children[i];
          admissions.push_back({id, child, push(jobs[child])});
        }
      };
      if (current.phase == 0) {
        current.value = 17 * current.value + 23 * id + current.remaining;
        if (!current.attempted && !job.terminal_admission) admit_children();
        current.phase = 1;
      } else {
        current.value = 29 * (current.value ^ (id + 0x91)) + current.remaining;
        if (!current.attempted && job.terminal_admission && current.remaining == 1) admit_children();
        if (!--current.remaining) {
          current.finished = true;
          ++counters.completed;
          // Callback admission can append behind this identity. Replacement
          // occupies its actual position, rather than assuming it is last.
          const auto position = std::find(active.begin(), active.end(), id);
          check(position != active.end(), "oracle lost current identity");
          if (overflow.empty()) active.erase(position);
          else {
            *position = overflow.front();
            overflow.erase(overflow.begin());
            ++counters.promoted;
          }
        } else current.phase = 0;
      }
      ++visited;
    }
    counters.visits += visited;
    if (budget && visited == budget && pending()) ++counters.budget_stops;
    return visited;
  }

  std::size_t pending() const { return active.size() + overflow.size(); }

  template<class Predicate> std::size_t release_if(Predicate predicate) {
    std::size_t removed = 0;
    const auto erase = [&](std::vector<unsigned>& entries) {
      std::erase_if(entries, [&](unsigned id) {
        if (!predicate(id)) return false;
        state[id].abandoned = true;
        ++removed;
        return true;
      });
    };
    erase(active);
    erase(overflow);
    while (active.size() < capacity.active && !overflow.empty()) {
      active.push_back(overflow.front());
      overflow.erase(overflow.begin());
      ++counters.promoted;
    }
    counters.abandoned += removed;
    return removed;
  }
};

struct Scenario {
  Ring ring;
  Model model;
  std::array<Job, 32> jobs{};
  std::vector<Event> actual_events;
  std::vector<Admission> actual_admissions;

  Scenario(Fixture& fixture, loom::RingCapacity capacity, unsigned seed) : model(capacity) {
    check(ring.configure(fixture.worker, capacity), "configure Ring");
    for (unsigned id = 0; id < jobs.size(); ++id)
      jobs[id] = {this, id, 1 + (id * 7 + seed) % 9};
  }

  ~Scenario() {
    check(!ring.pending(), "scenario retained native borrowers");
    check(ring.close(), "close Ring");
  }

  bool push(unsigned id) {
    auto& job = jobs[id];
    const auto actual = ring.push(metadata, {&job, job.cycles, job.id + 1});
    check(actual == model.push(job), "admission differs from finite inventory oracle");
    verify();
    return actual;
  }

  void verify() const {
    check(ring.pending() == model.pending(), "pending differs from logical inventory");
    const auto& a = ring.counters();
    const auto& e = model.counters;
    check(a.accepted == e.accepted && a.refused == e.refused &&
        a.completed == e.completed && a.abandoned == e.abandoned &&
        a.visits == e.visits && a.promoted == e.promoted &&
        a.budget_stops == e.budget_stops, "work accounting differs from oracle");
    for (const auto& job : jobs) {
      const auto& expected = model.state[job.id];
      check(job.done == unsigned(expected.finished), "completion missing or repeated");
      if (expected.finished) check(job.output == expected.value, "scalar state lost across phases");
    }
  }

  void spin(std::uint64_t budget) {
    actual_events.clear();
    actual_admissions.clear();
    std::vector<Event> expected_events;
    std::vector<Admission> expected_admissions;
    const auto expected = model.spin(budget, jobs, expected_events, expected_admissions);
    check(ring.spin({budget}) == expected, "spin dispatched wrong number of visits");
    if (actual_events != expected_events) {
      std::cerr << "capacity=" << model.capacity.active << '/' << model.capacity.overflow
          << " budget=" << budget << " actual=" << actual_events.size()
          << " expected=" << expected_events.size() << '\n';
      check(false, "phase/identity/state trace differs from independent rotation oracle");
    }
    check(actual_admissions == expected_admissions, "callback admissions differ from oracle");
    verify();
  }

  template<class Predicate> void release_if(Predicate predicate) {
    const auto expected = model.release_if(predicate);
    const auto actual = ring.release_if([predicate](const Ring::Arguments& arguments) noexcept {
      return predicate(std::get<0>(arguments)->id);
    });
    check(actual == expected, "selective cleanup removed wrong identities");
    verify();
  }

  void drain(const std::vector<std::uint64_t>& budgets) {
    unsigned iteration = 0;
    while (ring.pending()) {
      check(iteration < 4096, "drain made no finite progress");
      spin(budgets[iteration++ % budgets.size()]);
    }
    spin(0);
    spin(1);
    spin(9999);
  }
};

[[gnu::noinline]] void admit_children(Ring::Cursor ring, Job* job) {
  auto& scenario = *job->scenario;
  job->attempted = true;
  for (unsigned i = 0; i < job->child_count; ++i) {
    auto& child = scenario.jobs[job->children[i]];
    const auto accepted = ring->push(metadata, {&child, child.cycles, child.id + 1});
    record_admission(&scenario.actual_admissions, job->id, child.id, accepted);
  }
}

LOOM_NATIVE_CC void metadata(Ring::Cursor ring, Ring::Slot* slot, Job* job, unsigned remaining,
    std::uint64_t state) {
  auto& scenario = *job->scenario;
  record_event(&scenario.actual_events, job->id, 0, remaining, state);
  state = 17 * state + 23 * job->id + remaining;
  if (!job->attempted && !job->terminal_admission) admit_children(ring, job);
  slot->function = payload;
  [[clang::musttail]] return Ring::yield(ring, slot, job, remaining, state);
}

LOOM_NATIVE_CC void payload(Ring::Cursor ring, Ring::Slot* slot, Job* job, unsigned remaining,
    std::uint64_t state) {
  record_event(&job->scenario->actual_events, job->id, 1, remaining, state);
  state = 29 * (state ^ (job->id + 0x91)) + remaining;
  if (!job->attempted && job->terminal_admission && remaining == 1) admit_children(ring, job);
  if (!--remaining) {
    job->output = state;
    ++job->done;
    ring->complete(*slot);
  } else slot->function = metadata;
  [[clang::musttail]] return Ring::yield(ring, slot, job, remaining, state);
}

void fixed_membership(Fixture& fixture) {
  for (unsigned active = 1; active <= 7; ++active) {
    for (unsigned overflow = 0; overflow <= 3; ++overflow) {
      for (unsigned first_budget = 0; first_budget <= 2 * active + 1; ++first_budget) {
        Scenario scenario(fixture, {active, overflow}, first_budget);
        for (unsigned id = 0; id < active + overflow; ++id) check(scenario.push(id), "initial admission");
        check(!scenario.push(active + overflow), "full inventory accepted extra work");
        scenario.spin(first_budget);
        scenario.drain({1, active + 1, 2, 0, 2 * active, 9999});
      }
    }
  }
}

void callback_admission(Fixture& fixture) {
  for (unsigned active : {1u, 2u, 3u, 5u, 7u}) {
    for (unsigned overflow : {0u, 1u, 3u, 6u}) {
      for (unsigned first_budget : {1u, 2u, 3u, 5u, 11u}) {
        for (bool terminal : {false, true}) {
        Scenario scenario(fixture, {active, overflow}, first_budget);
        for (unsigned parent = 0; parent < 4; ++parent) {
          scenario.jobs[parent].children = {2 + 3 * parent, 3 + 3 * parent, 4 + 3 * parent};
          scenario.jobs[parent].child_count = 3;
          scenario.jobs[parent].terminal_admission = terminal;
        }
        // Keep child targets distinct: parents 0/1 enqueue 2..7, and children
        // 2/3 enqueue 8..13. Repeated calls within one callback cross capacities.
        check(scenario.push(0), "callback parent admission");
        if (active + overflow > 1) check(scenario.push(1), "second parent admission");
        scenario.spin(first_budget);
        scenario.drain({1, 2, 3, 4, 7, 11, 0, 23});
        }
      }
    }
  }
}

void selective_cleanup(Fixture& fixture) {
  for (unsigned active : {1u, 3u, 5u, 7u}) {
    Scenario scenario(fixture, {active, 5}, active);
    for (unsigned id = 0; id < active + 5; ++id) check(scenario.push(id), "cleanup admission");
    scenario.spin(active + 1);
    scenario.release_if([](unsigned) noexcept { return false; });
    scenario.release_if([](unsigned id) noexcept { return id % 3 == 2; });
    scenario.spin(1);
    scenario.release_if([](unsigned id) noexcept { return id % 2 == 1; });
    scenario.drain({1, 2, 5, 0, 17});
  }
  Scenario scenario(fixture, {5, 3}, 4);
  for (unsigned id = 0; id < 8; ++id) check(scenario.push(id), "all-match admission");
  scenario.spin(7);
  scenario.release_if([](unsigned) noexcept { return true; });
  scenario.drain({1});
}

struct PointerJob {
  unsigned id{}, remaining{}, done{};
  std::uint64_t value{};
  std::vector<Event>* events{};
};
struct Context { unsigned bias{}; };
using PointerRing = loom::Ring<PointerJob*, const Context*>;

LOOM_NATIVE_CC void pointer_payload(PointerRing::Cursor, PointerRing::Slot*, PointerJob*, const Context*);

LOOM_NATIVE_CC void pointer_metadata(PointerRing::Cursor ring, PointerRing::Slot* slot,
    PointerJob* job, const Context* context) {
  record_event(job->events, job->id, 0, job->remaining, job->value);
  job->value += ((job->id + 1) * 31 + job->remaining) * context->bias;
  slot->function = pointer_payload;
  [[clang::musttail]] return PointerRing::yield_unchanged(ring, slot, job, context);
}

LOOM_NATIVE_CC void pointer_payload(PointerRing::Cursor ring, PointerRing::Slot* slot,
    PointerJob* job, const Context* context) {
  record_event(job->events, job->id, 1, job->remaining, job->value);
  job->value = job->value * 7 + job->id + context->bias;
  if (!--job->remaining) {
    ++job->done;
    [[clang::musttail]] return PointerRing::finish(ring, slot, job, context);
  }
  slot->function = pointer_metadata;
  [[clang::musttail]] return PointerRing::yield_unchanged(ring, slot, job, context);
}

void unchanged_arguments(Fixture& fixture) {
  PointerRing ring;
  check(ring.configure(fixture.worker, {3, 5}), "configure pointer family");
  std::array<Context, 2> contexts{{{1}, {4}}};
  std::vector<Event> actual_events;
  std::array<PointerJob, 8> jobs;
  struct Expected { unsigned phase{}, remaining{}, done{}; std::uint64_t value{}; };
  std::array<Expected, 8> expected;
  std::vector<unsigned> active{0, 1, 2}, overflow{3, 4, 5, 6, 7};
  for (unsigned id = 0; id < jobs.size(); ++id) {
    jobs[id] = {id, 1 + id % 5, 0, id + 1, &actual_events};
    expected[id] = {0, jobs[id].remaining, 0, jobs[id].value};
    check(ring.push(pointer_metadata, {&jobs[id], &contexts[id % 2]}), "pointer family admission");
  }
  std::uint64_t total_visits = 0;
  unsigned abandoned = 0, iteration = 0;
  const std::array<unsigned, 6> budgets{1, 2, 7, 0, 4, 19};
  while (ring.pending()) {
    check(iteration < 100, "pointer family stalled");
    contexts[0].bias += 3;
    contexts[1].bias += 5;
    const auto budget = budgets[iteration % budgets.size()];
    std::vector<Event> expected_events;
    for (unsigned visit = 0; visit < budget && !active.empty(); ++visit) {
      const auto id = active.front();
      std::rotate(active.begin(), active.begin() + 1, active.end());
      auto& state = expected[id];
      const auto bias = contexts[id % 2].bias;
      expected_events.push_back({id, state.phase, state.remaining, state.value});
      if (state.phase == 0) {
        state.value += ((id + 1) * 31 + state.remaining) * bias;
        state.phase = 1;
      } else {
        state.value = state.value * 7 + id + bias;
        if (!--state.remaining) {
          ++state.done;
          if (overflow.empty()) active.pop_back();
          else {
            active.back() = overflow.front();
            overflow.erase(overflow.begin());
          }
        } else state.phase = 0;
      }
    }
    actual_events.clear();
    check(ring.spin({budget}) == expected_events.size(), "pointer family exact budget");
    total_visits += expected_events.size();
    check(actual_events == expected_events, "unchanged arguments lost phase/state");
    check(!ring.release_if([&](const PointerRing::Arguments& arguments) noexcept {
      const auto* job = std::get<0>(arguments);
      check(job == &jobs[job->id] && std::get<1>(arguments) == &contexts[job->id % 2],
          "stored pointer tuple changed");
      return false;
    }), "pointer inspection removed borrowers");
    if (iteration == 2) {
      const auto remove = [](unsigned id) { return id == 0 || id == 5; };
      const auto before = active.size() + overflow.size();
      std::erase_if(active, remove);
      std::erase_if(overflow, remove);
      abandoned = before - active.size() - overflow.size();
      check(ring.release_if([](const PointerRing::Arguments& arguments) noexcept {
        const auto id = std::get<0>(arguments)->id;
        return id == 0 || id == 5;
      }) == abandoned, "pointer family selective cleanup");
      while (active.size() < 3 && !overflow.empty()) {
        active.push_back(overflow.front());
        overflow.erase(overflow.begin());
      }
    }
    check(ring.pending() == active.size() + overflow.size(), "pointer family pending");
    for (unsigned id = 0; id < jobs.size(); ++id)
      check(jobs[id].remaining == expected[id].remaining && jobs[id].done == expected[id].done &&
          jobs[id].value == expected[id].value, "pointer family mutable state differs");
    ++iteration;
  }
  const auto& counters = ring.counters();
  check(counters.accepted == 8 && counters.completed + abandoned == 8 &&
      counters.abandoned == abandoned && counters.visits == total_visits,
      "finish/unchanged work accounting");
  check(ring.close(), "close pointer family");
}
} // namespace

int main() {
  Fixture fixture;
  fixed_membership(fixture);
  callback_admission(fixture);
  selective_cleanup(fixture);
  unchanged_arguments(fixture);
  std::cout << "loom_ring_rotation_check: passed\n";
}
