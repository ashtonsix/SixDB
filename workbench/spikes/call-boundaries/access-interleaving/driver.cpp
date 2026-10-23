#include "fixture.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace access_spike {
void check(bool condition, const char* message) {
  if (!condition) { std::cerr << "access_interleaving: " << message << '\n'; std::abort(); }
}
template<class T, class E> void check(const std::expected<T, E>& value, const char* message) {
  check(value.has_value(), message);
}
template<class T, class E> T require(std::expected<T, E> value, const char* message) {
  check(value, message); return std::move(*value);
}

tp::layout format(unsigned shift) {
  std::array<tp::code, 64> codes;
  for (unsigned i = 0; i < codes.size(); ++i) codes[i] = {std::uint8_t(i), std::uint8_t(shift), 7};
  return require(tp::layout::make(64, codes), "TuplePack layout");
}
tp::read_plan<64> plan(const tp::layout& layout) {
  std::array<std::uint8_t, 64> map;
  for (unsigned i = 0; i < map.size(); ++i) map[i] = i;
  return require(tp::read_plan<64>::make(layout, map), "TuplePack reader plan");
}

Selected::Selected(loom::Worker& worker, std::size_t rows, Word version, Word representation,
                   unsigned seed, unsigned shift)
    : allocation(require(loom::allocate(worker, {.bytes = rows * 64}), "fixture allocation")),
      held(require(loom::hold(allocation, {0, rows * 64}), "fixture Hold")),
      layout(format(shift)), prepared(plan(layout)),
      source(require(tp::view::attach(layout,
          loom::bytes(held).first(rows * 64), rows, 64), "source View")),
      operation(require(tp::bind_reader(prepared, source), "bound reader")), reader(operation),
      mask(rows - 1), version(version), representation(representation), seed(seed), shift(shift) {
  check(std::has_single_bit(rows) && rows <= (1u << 20) && shift <= 1, "fixture shape");
  for (std::size_t row = 0; row < rows; ++row)
    for (unsigned byte = 0; byte < 64; ++byte)
      source.row_unchecked(row)[byte] = std::byte(decoded(row, byte) << shift);
  check(reader.admit(rows - 1), "last native row admission");
}
Selected::~Selected() { loom::release(held); loom::release(allocation); }
void Selected::release_owner() { loom::release(allocation); }
Word Selected::link(Word row, Word mask, unsigned seed) {
  // A full-period permutation for power-of-two row populations. Addresses are
  // discovered from the decoded row, rather than supplied by the loop counter.
  return (row * 5 + (seed | 1)) & mask;
}
std::uint8_t Selected::decoded(Word row, unsigned byte) const {
  if (byte < 3) return (link(row, mask, seed) >> (7 * byte)) & 127;
  return (row * 17 + byte * 13 + seed) & 127;
}

struct Fixture {
  loom::Worker worker;
  orbital::Ref<orbital::allocation::Arena> arena;
  bool closed{};
  Fixture() {
    check(loom::adopt(worker), "adopt lane");
    arena = require(orbital::allocation::open_arena(192 * 1024 * 1024), "open Arena");
    check(loom::configure(worker, arena, 8), "configure Worker");
  }
  void close() {
    if (closed) return;
    unsigned attempts = 0;
    for (;;) {
      const auto progress = loom::shutdown(worker);
      check(progress.kind() != orbital::Progress::Kind::failed, "shutdown failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(++attempts < 100000, "physical drain failed to finish");
      loom::poll(worker, {256, 0});
      std::this_thread::yield();
    }
    loom::relinquish(worker); closed = true;
  }
  ~Fixture() { close(); }
};

void reset(Frame& frame) {
  frame.sum = frame.final_row = frame.visits = frame.done = 0;
  frame.output = {};
}
std::vector<Frame> frames(std::shared_ptr<const Selected> selected, std::size_t jobs,
                          Word steps, bool ragged, bool hints) {
  const auto demand = hints ? require(loom::bind_demand(
      {loom::MemoryPattern::irregular, 64}, 64), "qualified demand") : loom::DemandClass{};
  std::vector<Frame> work(jobs);
  for (std::size_t i = 0; i < jobs; ++i) {
    work[i].selected = selected; work[i].demand = demand;
    work[i].identity = 3 + i * 19;
    work[i].start = (i * 7919 + 13) & selected->mask;
    work[i].steps = ragged ? 1 + (i * 7) % steps : steps;
  }
  return work;
}

void oracle(const Frame& frame, Word version, Word representation) {
  check(frame.selected->version == version && frame.selected->representation == representation,
        "fixture selected meaning changed");
  // Decode the declared logical fixture formula; never call Ikea or read its
  // encoded storage here. The next cursor and order-sensitive sum are independent.
  std::array<std::uint8_t, 64> expected{};
  Word row = frame.start, sum = 0;
  for (Word step = 0; step < frame.steps; ++step) {
    const auto next = (row * 5 + (frame.selected->seed | 1)) & frame.selected->mask;
    Word first = 0;
    for (unsigned byte = 0; byte < 64; ++byte) {
      const std::uint8_t value = byte < 3 ? (next >> (7 * byte)) & 127 :
          (row * 17 + byte * 13 + frame.selected->seed) & 127;
      expected[byte] |= value;
      if (byte < 8) first |= Word(value) << (8 * byte);
    }
    sum = std::rotl(sum, 9) ^ (first + row + frame.identity);
    row = next;
  }
  std::array<std::uint8_t, 64> actual;
  tp::native::store_packet(actual.data(), frame.output.get());
  check(actual == expected && frame.sum == sum && frame.final_row == row &&
        frame.visits == frame.steps && frame.done == 1, "independent result/once oracle");
}
Kernel manual_kernel(unsigned width) {
  switch (width) {
    case 1: return access_manual_1;
    case 2: return access_manual_2;
    case 4: return access_manual_4;
    case 8: return access_manual_8;
    default: std::abort();
  }
}
void run_kernel(std::span<Frame> work, unsigned width, Kernel kernel,
                const loom::HintPolicy& policy) {
  for (std::size_t first = 0; first < work.size(); first += width)
    kernel(work.data() + first, std::min<std::size_t>(width, work.size() - first), policy);
}
void run_ring(std::span<Frame> work, unsigned width, Native::Ring& ring, Word quantum) {
  for (std::size_t first = 0; first < work.size(); first += width) {
    for (std::size_t i = first; i < std::min<std::size_t>(first + width, work.size()); ++i)
      check(Native::enqueue(ring, work[i]), "native group admission");
    while (ring.pending()) check(ring.spin({quantum}) > 0, "native drain stalled");
  }
}

unsigned matched_checks(Fixture& fixture) {
  unsigned cases = 0;
  for (const auto rows : {16u, 1024u}) for (const auto shift : {0u, 1u}) {
    auto selected = std::make_shared<Selected>(fixture.worker, rows, 7, shift + 1, 23, shift);
    for (const auto hints : {false, true}) for (const auto width : {1u, 2u, 4u, 8u}) {
      auto work = frames(selected, 19, 17, true, hints);
      for (auto kernel : {access_scalar, manual_kernel(width)}) {
        run_kernel(work, width, kernel, fixture.worker.hints);
        for (auto& frame : work) { oracle(frame, 7, shift + 1); reset(frame); }
        ++cases;
      }
      Native::Ring ring;
      check(ring.configure(fixture.worker, {width, 0}), "matched Ring configure");
      run_ring(work, width, ring, 3);
      Word expected_visits = 0;
      for (const auto& frame : work) { oracle(frame, 7, shift + 1); expected_visits += frame.steps; }
      check(ring.counters().completed == work.size() && ring.counters().visits == expected_visits &&
            !ring.counters().promoted && ring.close(), "matched Ring logical counts");
      ++cases;
    }
  }
  return cases;
}

struct WaitingParent {
  loom::Task task;
  orbital::Signal signal;
  Native::Ring* ring{};
  bool ready{};
  unsigned calls{};
  WaitingParent(loom::Worker& worker, Native::Ring& ring) : ring(&ring) {
    signal.lane = &worker.lane; signal.state = orbital::Readiness::pending;
    task.context = this;
    task.step = [](loom::Task& task) noexcept {
      auto& parent = *static_cast<WaitingParent*>(task.context);
      ++parent.calls;
      if (!parent.ready) return orbital::Progress::wait(parent.signal);
      parent.ring->spin({3});
      return parent.ring->pending() ? orbital::Progress::yield() : orbital::Progress::done();
    };
    check(loom::start(worker, task), "parent Task admission");
    check(loom::poll(worker, {0, 1}) == 1 && task.state == loom::TaskState::waiting,
          "actual Signal enrollment");
  }
  ~WaitingParent() {
    if (task.worker) loom::detach(task);
  }
};

void wait_and_selection(Fixture& fixture) {
  Native::Ring ring;
  check(ring.configure(fixture.worker, {2, 4}), "wait Ring configure");
  auto current = std::make_shared<Selected>(fixture.worker, 1024, 7, 11, 23, 1);
  std::weak_ptr<const Selected> old = current;
  auto work = frames(current, 6, 17, false, true);
  for (auto& frame : work) check(Native::enqueue(ring, frame), "wait native admission");
  check(ring.spin({3}) == 3 && !ring.close(), "wait prefix/live close refusal");
  WaitingParent parent(fixture.worker, ring);
  current->release_owner(); // Actual Hold now supplies physical address lifetime.
  auto equivalent = std::make_shared<Selected>(fixture.worker, 1024, 7, 12, 23, 0);
  current = std::make_shared<Selected>(fixture.worker, 1024, 8, 21, 71, 0);
  check(!old.expired(), "waiting selected closure lost");
  // An owner pulse queues a retry; it grants no readiness or replacement meaning.
  orbital::pulse(parent.signal);
  check(loom::poll(fixture.worker, {0, 1}) == 1 && parent.calls == 2 &&
        parent.task.state == loom::TaskState::waiting && ring.counters().visits == 3,
        "pulse became readiness or executed waiting bytes");
  parent.ready = true;
  orbital::pulse(parent.signal);
  unsigned visits = 0;
  while (parent.task.state != loom::TaskState::done) {
    check(++visits < 128 && loom::poll(fixture.worker, {0, 1}) == 1, "resumed Task drain");
  }
  for (const auto& frame : work) oracle(frame, 7, 11);
  auto equivalent_work = frames(equivalent, 1, 17, false, true);
  equivalent_work[0].start = work[0].start;
  access_scalar(equivalent_work.data(), 1, fixture.worker.hints);
  oracle(equivalent_work[0], 7, 12);
  check(equivalent_work[0].sum == work[0].sum, "equivalent fixture interpretation differed");
  check(!parent.signal.waiters && !fixture.worker.active_tasks && ring.close(), "wait registration drain");
  work.clear(); check(old.expired(), "selected closure retained after completion release");
}

void cancel_and_physical_drain(Fixture& fixture) {
  Native::Ring ring;
  check(ring.configure(fixture.worker, {2, 4}), "cancel Ring configure");
  auto doomed = std::make_shared<Selected>(fixture.worker, 1024, 9, 31, 41, 1);
  auto survivor = std::make_shared<Selected>(fixture.worker, 1024, 10, 41, 67, 0);
  auto work = frames(survivor, 6, 17, false, true);
  for (unsigned i = 0; i < 6; ++i) {
    if (!(i & 1)) work[i].selected = doomed;
    check(Native::enqueue(ring, work[i]), "cancel admission");
  }
  check(ring.spin({3}) == 3, "cancel prefix");
  WaitingParent parent(fixture.worker, ring);
  loom::detach(parent.task);
  check(ring.pending() == 6 && !fixture.worker.active_tasks && !parent.signal.waiters,
        "Task detach incorrectly cleaned Ring borrowers");
  check(ring.release_if([owner = doomed.get()](const Native::Ring::Arguments& args) noexcept {
    return std::get<0>(args)->selected.get() == owner;
  }) == 3 && ring.pending() == 3, "active/overflow selective cleanup");
  const auto calls = parent.calls;
  std::weak_ptr<const Selected> dead = doomed;
  for (unsigned i = 0; i < 6; i += 2) work[i].selected.reset();
  doomed.reset(); check(dead.expired(), "cancelled closure survived explicit cleanup");
  orbital::pulse(parent.signal);
  check(!loom::poll(fixture.worker, {0, 1}) && parent.calls == calls, "detached parent resumed");
  while (ring.pending()) check(ring.spin({2}) > 0, "survivor drain");
  for (unsigned i = 0; i < 6; ++i) {
    if (i & 1) oracle(work[i], 10, 41);
    else check(!work[i].done && !work[i].visits, "cancelled borrower published result");
  }
  check(ring.counters().completed == 3 && ring.counters().abandoned == 3 &&
        ring.counters().visits == 53 && ring.close(),
        "cancel retirement/close");
  // End all logical interest while one actual escaped physical Hold remains.
  auto late = require(loom::retain(survivor->held), "retain escaped physical Hold");
  work.clear(); survivor.reset();
  check(!fixture.worker.active_tasks &&
        loom::shutdown(fixture.worker).kind() == orbital::Progress::Kind::waiting,
        "empty Task queue implied physical drain");
  std::thread foreign([&] { loom::release(late); }); foreign.join();
  fixture.close();
}

void bench(Fixture& fixture, std::size_t rows, Word steps, unsigned repetitions) {
  auto selected = std::make_shared<Selected>(fixture.worker, rows, 1, 1, 23, 1);
  constexpr unsigned jobs = 128, rounds = 16;
  for (const auto hints : {false, true}) for (const auto width : {1u, 2u, 4u, 8u}) {
    std::array<std::vector<Frame>, rounds> work;
    std::vector<bool> touched(rows);
    Word useful = 0;
    for (unsigned round = 0; round < rounds; ++round) {
      work[round] = frames(selected, jobs, steps, true, hints);
      for (auto& frame : work[round]) {
        frame.start = (frame.start + round * 104729) & selected->mask;
        useful += frame.steps;
        auto row = frame.start;
        for (Word step = 0; step < frame.steps; ++step) {
          touched[row] = true;
          row = Selected::link(row, selected->mask, selected->seed);
        }
      }
    }
    const auto touched_rows = std::count(touched.begin(), touched.end(), true);
    Native::Ring ring;
    check(ring.configure(fixture.worker, {width, 0}), "bench Ring configure");
    for (const std::string_view mode : {"serial", "manual", "ring"}) {
      const auto execute = [&](std::span<Frame> group) {
        for (auto& frame : group) reset(frame);
        if (mode == "ring") run_ring(group, width, ring, UINT64_MAX);
        else run_kernel(group, width, mode == "serial" ? access_scalar : manual_kernel(width),
                        fixture.worker.hints);
      };
      for (auto& group : work) {
        execute(group); for (const auto& frame : group) oracle(frame, 1, 1);
      }
      for (unsigned rep = 0; rep < repetitions; ++rep) {
        const auto start = std::chrono::steady_clock::now();
        for (auto& group : work) execute(group);
        const auto elapsed = std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - start).count();
        Word checksum = 0;
        for (const auto& group : work) for (const auto& frame : group) {
          oracle(frame, 1, 1); checksum ^= frame.sum;
        }
        std::cout << "{\"mode\":\"" << mode << "\",\"rows\":" << rows
                  << ",\"bytes\":" << rows * 64 << ",\"touched_rows\":" << touched_rows
                  << ",\"group_width\":" << width
                  << ",\"active_width\":" << (mode == "serial" ? 1 : width)
                  << ",\"hints\":" << (hints ? "true" : "false") << ",\"rep\":" << rep
                  << ",\"jobs\":" << jobs * rounds << ",\"reads\":" << useful
                  << ",\"hint_requests\":" << (hints ? useful : 0)
                  << ",\"ns_per_read\":" << elapsed / useful
                  << ",\"batch_ns\":" << elapsed / rounds << ",\"checksum\":" << checksum << "}\n";
      }
    }
    check(ring.close(), "bench close");
  }
}
} // namespace access_spike

int main(int argc, char** argv) {
  using namespace access_spike;
  const std::string_view command = argc > 1 ? argv[1] : "check";
  Fixture fixture;
  if (command == "check") {
    const auto cases = matched_checks(fixture);
    wait_and_selection(fixture);
    cancel_and_physical_drain(fixture);
    std::cout << "{\"passed\":true,\"matched_cases\":" << cases
              << ",\"wait_selection\":true,\"cancel_physical_drain\":true}\n";
  } else if (command == "bench") {
    const auto rows = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 65536;
    const auto steps = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 128;
    const auto reps = argc > 4 ? std::strtoul(argv[4], nullptr, 10) : 7;
    check(std::has_single_bit(rows) && rows <= (1u << 20) && steps > 0 && steps <= 4096 &&
          reps > 0 && reps <= 32, "bench arguments");
    bench(fixture, rows, steps, reps);
  } else {
    std::cerr << "usage: call_boundary_access_interleaving [check|bench [rows steps reps]]\n";
    return 2;
  }
}
