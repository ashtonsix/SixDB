#include <loom/ring.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

namespace {
void check(bool condition, const char* message) {
  if (!condition) { std::cerr << "loom_ring_check: " << message << '\n'; std::abort(); }
}

template<class T> void check(const orbital::Result<T>& result, const char* message) {
  check(result.has_value(), message);
}

struct Fixture {
  loom::Worker worker;
  orbital::Ref<orbital::allocation::Arena> arena;

  Fixture() {
    check(loom::adopt(worker), "lane adoption");
    auto opened = orbital::allocation::open_arena(8 * 1024 * 1024);
    check(opened, "open arena");
    arena = std::move(*opened);
    check(loom::configure(worker, arena, 8), "configure worker");
  }

  ~Fixture() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto progress = loom::shutdown(worker);
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(progress.kind() != orbital::Progress::Kind::failed, "shutdown failure");
      check(std::chrono::steady_clock::now() < deadline, "shutdown deadline");
      loom::poll(worker, {256, 0});
      std::this_thread::yield();
    }
    check(!orbital::worker::pending(worker.lane), "messages retained after shutdown");
    loom::relinquish(worker);
  }
};

struct alignas(64) Node {
  std::uint64_t next{}, value{};
  std::array<std::uint64_t, 6> padding{};
};

struct Job { const Node* nodes{}; std::uint64_t result{}, done{}; };

using Scalar = loom::Ring<std::uint64_t, std::uint64_t, std::uint64_t, Job*>;
using Vector = std::uint64_t __attribute__((vector_size(16)));
using Vectors = loom::Ring<std::uint64_t, std::uint64_t, std::uint64_t, Job*, Vector, Vector>;

struct Input {
  orbital::allocation::Allocation allocation;
  orbital::allocation::Hold use;
  Node* nodes{};
  std::array<Job, 8> jobs{};

  explicit Input(loom::Worker& worker, std::uint64_t seed = 0) {
    auto memory = loom::allocate(worker, {.bytes = 64 * sizeof(Node), .alignment = alignof(Node)});
    check(memory, "allocate real input");
    allocation = std::move(*memory);
    auto held = loom::hold(allocation, {0, 64 * sizeof(Node)});
    check(held, "hold real input");
    use = std::move(*held);
    nodes = reinterpret_cast<Node*>(loom::bytes(use).data());
    for (std::uint64_t i = 0; i < 64; ++i)
      std::construct_at(nodes + i, Node{(i * 5 + 3) & 63, (i * 73 + 19) ^ seed, {}});
    for (auto& job : jobs) job.nodes = nodes;
  }
};

LOOM_NATIVE_CC void scalar(Scalar::Cursor ring, Scalar::Slot* slot, std::uint64_t cursor,
    std::uint64_t remaining, std::uint64_t sum, Job* job) {
  const auto& node = job->nodes[cursor];
  sum += node.value;
  cursor = node.next;
  if (!--remaining) {
    job->result = sum;
    ++job->done;
    ring->complete(*slot);
  }
  [[clang::musttail]] return Scalar::yield(ring, slot, cursor, remaining, sum, job);
}

LOOM_NATIVE_CC void other_scalar(Scalar::Cursor ring, Scalar::Slot* slot, std::uint64_t cursor,
    std::uint64_t remaining, std::uint64_t sum, Job* job) {
  const auto& node = job->nodes[cursor];
  sum += node.value ^ 0x33;
  cursor = node.next;
  if (!--remaining) {
    job->result = sum;
    ++job->done;
    ring->complete(*slot);
  }
  [[clang::musttail]] return Scalar::yield(ring, slot, cursor, remaining, sum, job);
}

LOOM_NATIVE_CC void vectors(Vectors::Cursor ring, Vectors::Slot* slot, std::uint64_t cursor,
    std::uint64_t remaining, std::uint64_t sum, Job* job, Vector x, Vector y) {
  const auto& node = job->nodes[cursor];
  sum += node.value;
  cursor = node.next;
  x += Vector{node.value, node.value ^ 0x5a5a};
  y ^= x;
  if (!--remaining) {
    job->result = sum + x[0] + x[1] + y[0] + y[1];
    ++job->done;
    ring->complete(*slot);
  }
  [[clang::musttail]] return Vectors::yield(ring, slot, cursor, remaining, sum, job, x, y);
}

std::uint64_t expected(std::uint64_t start, std::uint64_t steps,
    std::uint64_t seed = 0, bool other = false, bool vector_state = false) {
  std::uint64_t sum = 0, x0 = 1, x1 = 2, y0 = 3, y1 = 4;
  // Derive directly from the source law; do not read populated native bytes.
  for (std::uint64_t i = 0; i < steps; ++i) {
    const auto value = (start * 73 + 19) ^ seed;
    sum += other ? value ^ 0x33 : value;
    x0 += value; x1 += value ^ 0x5a5a; y0 ^= x0; y1 ^= x1;
    start = (start * 5 + 3) & 63;
  }
  return sum + (vector_state ? x0 + x1 + y0 + y1 : 0);
}

void scalar_checks(Fixture& fixture) {
  Scalar ring;
  check(!ring.configure(fixture.worker, {0, 2}), "zero active accepted");
  check(!ring.configure(fixture.worker, {std::numeric_limits<std::size_t>::max(), 1}), "sum overflow accepted");
  check(!ring.configure(fixture.worker, {std::numeric_limits<std::size_t>::max() / 2, 0}), "byte overflow accepted");
  check(!ring.configure(fixture.worker, {1, 1}, 63), "bad hint geometry accepted");
  check(!ring.configured(), "failed configure retained storage");
  check(ring.configure(fixture.worker, {3, 2}), "configure scalar storage");
  check(!ring.configure(fixture.worker, {1, 0}), "duplicate configure accepted");
  Input input(fixture.worker);
  const std::array<std::uint64_t, 6> steps{1, 7, 2, 9, 3, 5};
  for (unsigned i = 0; i < 5; ++i) {
    auto dependency = loom::bind_dependency(loom::Hint::l1_1,
        {reinterpret_cast<const std::byte*>(input.nodes + i), sizeof(Node)});
    check(dependency, "bind initial hint");
    check(ring.push(i & 1 ? other_scalar : scalar, {i, steps[i], 0, &input.jobs[i]}, *dependency), "accept slot");
  }
  check(!ring.push(scalar, {5, steps[5], 0, &input.jobs[5]}), "full inventory did not refuse");
  check(ring.pending() == 5 && ring.spin({0}) == 0 && ring.counters().visits == 0, "zero budget changed progress");
  check(!ring.close() && ring.pending() == 5, "close freed live entries");
  while (ring.pending()) check(ring.spin({1}) == 1, "one-visit quantum failed");
  for (unsigned i = 0; i < 5; ++i)
    check(input.jobs[i].done == 1 && input.jobs[i].result == expected(i, steps[i], 0, i & 1), "scalar output/once");
  check(input.jobs[5].done == 0, "refused frame ran");
  check(ring.counters().accepted == 5 && ring.counters().completed == 5 &&
        ring.counters().refused == 1 && ring.counters().promoted == 2 &&
        ring.counters().visits == 22, "scalar independent counts");
  for (auto& job : input.jobs) { job.done = 0; job.result = 0; }
  for (unsigned i = 0; i < 5; ++i) check(ring.push(scalar, {i, 9, 0, &input.jobs[i]}), "reuse admission");
  check(ring.spin({4}) == 4 && ring.discard() == 5 && !ring.pending(), "discard exact pending");
  for (const auto& job : input.jobs) check(!job.done, "discard completed abandoned contribution");
  check(ring.push(scalar, {6, 3, 0, &input.jobs[6]}), "underfilled reuse");
  check(ring.spin({99}) == 3 && input.jobs[6].done == 1 && input.jobs[6].result == expected(6, 3), "underfilled native state");
  check(ring.counters().accepted == ring.counters().completed + ring.counters().abandoned &&
        ring.counters().abandoned == 5, "abandon accounting");
  check(ring.close(), "close drained ring");
  check(!ring.configured() && ring.close(), "idempotent close");
  check(ring.configure(fixture.worker, {1, 0}), "reconfigure reusable storage");
  check(ring.push(scalar, {7, 2, 0, &input.jobs[7]}), "reconfigured admission");
  check(ring.spin({8}) == 2 && input.jobs[7].done == 1 && input.jobs[7].result == expected(7, 2), "reconfigured output");
  check(ring.close(), "reconfigured close");
}

void selective_checks(Fixture& fixture) {
  Scalar ring;
  check(ring.configure(fixture.worker, {2, 4}), "configure shared typed bank");
  auto a = std::make_unique<Input>(fixture.worker, 7);
  Input b(fixture.worker, 13);
  // A0 active, B0 active, then A1/B1/A2/B2 overflow. First B0 finishes,
  // promoting A1, and a new B3 wraps the FIFO tail into the released slot.
  check(ring.push(scalar, {0, 20, 0, &a->jobs[0]}), "A0");
  check(ring.push(scalar, {0, 1, 0, &b.jobs[0]}), "B0");
  for (unsigned i = 1; i < 3; ++i) {
    check(ring.push(scalar, {i, 20, 0, &a->jobs[i]}), "A overflow");
    check(ring.push(scalar, {i, 3, 0, &b.jobs[i]}), "B overflow");
  }
  check(ring.spin({3}) == 3 && b.jobs[0].done == 1, "wrapped progress");
  check(ring.push(scalar, {3, 3, 0, &b.jobs[3]}), "wrapped reuse");
  const auto count = ring.pending();
  check(!ring.release_if([](const Scalar::Arguments&) noexcept { return false; }) && ring.pending() == count,
        "no-match changed progress");
  const auto begin = a->jobs.data(), end = begin + a->jobs.size();
  check(ring.release_if([begin, end](const Scalar::Arguments& arguments) noexcept {
    const auto* job = std::get<3>(arguments);
    // Equality uses stable concrete job identities; unrelated pointer ordering
    // is not used as a membership proof.
    for (auto* item = begin; item != end; ++item) if (job == item) return true;
    return false;
  }) == 3 && ring.pending() == 3, "selective active/overflow count");
  for (unsigned i = 0; i < 3; ++i) check(!a->jobs[i].done, "cancelled A published result");
  a.reset(); // Actual Allocation/Hold and borrowed state end before B resumes.
  while (ring.pending()) ring.spin({2});
  for (unsigned i = 0; i < 4; ++i)
    check(b.jobs[i].done == 1 && b.jobs[i].result == expected(i, i ? 3 : 1, 13), "B survivor state/once");
  for (unsigned i = 0; i < 6; ++i) check(ring.push(scalar, {i, 5, 0, &b.jobs[i]}), "full reuse");
  check(ring.release_if([](const Scalar::Arguments&) noexcept { return true; }) == 6 && !ring.pending(), "all-match restore");
  check(ring.counters().accepted == ring.counters().completed + ring.counters().abandoned, "selective accounting");
  check(ring.close(), "close selective bank");
}

void vector_checks(Fixture& fixture) {
  Vectors ring;
  check(ring.configure(fixture.worker, {2, 2}), "configure vector family");
  auto a = std::make_unique<Input>(fixture.worker, 11);
  Input b(fixture.worker, 29);
  for (unsigned i = 0; i < 2; ++i) {
    for (auto* input : {a.get(), &b})
      check(ring.push(vectors, {i, 7, 0, &input->jobs[i], Vector{1, 2}, Vector{3, 4}}), "vector admission");
  }
  ring.spin({3}); // A0 and B0 both hold changed live scalar/vector values.
  check(ring.release_if([first = &a->jobs[0], second = &a->jobs[1]](const Vectors::Arguments& args) noexcept {
    return std::get<3>(args) == first || std::get<3>(args) == second;
  }) == 2, "vector selective removal");
  a.reset();
  while (ring.pending()) ring.spin({1});
  for (unsigned i = 0; i < 2; ++i)
    check(b.jobs[i].done == 1 && b.jobs[i].result == expected(i, 7, 29, false, true), "live vector argument persistence");
  check(ring.counters().accepted == 4 && ring.counters().completed == 2 &&
        ring.counters().abandoned == 2 && ring.counters().visits == 16, "vector independent counts");
  check(ring.close(), "close vector family");
}


struct RotationLog { std::array<unsigned, 128> ids{}; unsigned used{}; };

struct RotationJob;
using Rotation = loom::Ring<std::uint64_t, std::uint64_t, RotationJob*>;

struct RotationJob {
  RotationLog* log{};
  unsigned id{};
  std::uint64_t steps{}, result{}, done{};
  RotationJob* enqueue{};
  bool attempted{}, accepted{};
};

LOOM_NATIVE_CC void rotation(Rotation::Cursor ring, Rotation::Slot* slot, std::uint64_t remaining,
    std::uint64_t sum, RotationJob* job) {
  job->log->ids[job->log->used++] = job->id;
  sum += job->id * 13 + remaining;
  if (job->enqueue && !job->attempted) {
    job->attempted = true;
    job->accepted = ring->push(rotation, {job->enqueue->steps, 0, job->enqueue});
  }
  if (!--remaining) {
    job->result = sum;
    ++job->done;
    ring->complete(*slot);
  }
  [[clang::musttail]] return Rotation::yield(ring, slot, remaining, sum, job);
}

void rotation_checks(Fixture& fixture) {
  Rotation ring;
  RotationLog log;
  std::array<RotationJob, 8> jobs;
  auto reset = [&](unsigned steps) {
    log = {};
    for (unsigned i = 0; i < jobs.size(); ++i) jobs[i] = {&log, i, steps};
  };
  auto drain = [&] {
    unsigned stops = 0;
    while (ring.pending()) {
      check(++stops < 128 && ring.spin({1}) == 1, "rotation made no progress");
    }
  };
  auto output = [&](unsigned i) {
    const auto n = jobs[i].steps;
    check(jobs[i].done == 1 && jobs[i].result == i * 13 * n + n * (n + 1) / 2,
          "rotation output/once");
  };
  reset(1);
  check(ring.configure(fixture.worker, {1, 3}), "singleton replacement geometry");
  for (unsigned i = 0; i < 4; ++i) check(ring.push(rotation, {1, 0, &jobs[i]}), "singleton FIFO admission");
  drain();
  for (unsigned i = 0; i < 4; ++i) { output(i); check(log.ids[i] == i, "singleton replacement FIFO"); }
  check(ring.counters().promoted == 3 && ring.close(), "singleton FIFO retirement");

  reset(2);
  check(ring.configure(fixture.worker, {1, 2}), "callback enqueue overflow geometry");
  jobs[0].enqueue = &jobs[2];
  check(ring.push(rotation, {2, 0, &jobs[0]}) && ring.push(rotation, {2, 0, &jobs[1]}), "callback FIFO initial admission");
  drain();
  check(jobs[0].attempted && jobs[0].accepted, "callback enqueue failed");
  for (unsigned i = 0; i < 3; ++i) {
    output(i);
    check(log.ids[i * 2] == i && log.ids[i * 2 + 1] == i, "callback overflow violated FIFO");
  }
  check(ring.counters().accepted == 3 && ring.counters().completed == 3 && ring.close(), "callback FIFO counts");

  reset(3); jobs[0].steps = 1;
  check(ring.configure(fixture.worker, {7, 0}), "arbitrary sparse geometry");
  jobs[0].enqueue = &jobs[1];
  check(ring.push(rotation, {1, 0, &jobs[0]}), "callback vacant initial admission");
  drain(); output(0); output(1);
  check(jobs[0].accepted && log.used == 4 && log.ids[0] == 0 && log.ids[1] == 1,
        "singleton callback enqueue lost child");
  check(ring.close(), "callback vacant close");

  reset(2); jobs[1].steps = 1;
  check(ring.configure(fixture.worker, {1, 1}), "callback refusal geometry");
  jobs[0].enqueue = &jobs[2];
  check(ring.push(rotation, {2, 0, &jobs[0]}) && ring.push(rotation, {1, 0, &jobs[1]}), "callback refusal initial admission");
  drain(); output(0); output(1);
  check(jobs[0].attempted && !jobs[0].accepted && !jobs[2].done && ring.counters().refused == 1,
        "callback refusal consumed work");
  check(ring.close(), "callback refusal close");

  reset(3);
  check(ring.configure(fixture.worker, {5, 2}), "survivor rotation nonpower geometry");
  for (unsigned i = 0; i < 7; ++i) check(ring.push(rotation, {3, 0, &jobs[i]}), "survivor admission");
  check(ring.spin({2}) == 2, "survivor prefix progress");
  check(ring.release_if([](const Rotation::Arguments& args) noexcept {
    const auto id = std::get<2>(args)->id;
    return id == 0 || id == 3;
  }) == 2, "survivor selective removal");
  drain();
  std::array<unsigned, 3> survivors{}; unsigned found = 0;
  for (unsigned i = 2; i < log.used && found < 3; ++i)
    if (log.ids[i] == 1 || log.ids[i] == 2 || log.ids[i] == 4) survivors[found++] = log.ids[i];
  check(found == 3 && survivors == std::array<unsigned, 3>{2, 4, 1}, "survivor relative rotation changed");
  unsigned first5 = log.used, first6 = log.used;
  for (unsigned i = 2; i < log.used; ++i) {
    if (log.ids[i] == 5 && first5 == log.used) first5 = i;
    if (log.ids[i] == 6 && first6 == log.used) first6 = i;
  }
  check(first5 < first6, "surviving overflow FIFO changed");
  for (unsigned i : {1u, 2u, 4u, 5u, 6u}) output(i);
  check(!jobs[0].done && !jobs[3].done && ring.counters().accepted == 7 &&
        ring.counters().completed == 5 && ring.counters().abandoned == 2 && ring.close(),
        "survivor exact accepted disposition");
}

LOOM_NATIVE_CC void clobber_entry(Scalar::Cursor ring, Scalar::Slot* slot, std::uint64_t cursor,
    std::uint64_t remaining, std::uint64_t sum, Job* job) {
#if defined(__aarch64__)
  __asm__ volatile("mov x19, #321" ::: "x19");
#endif
  [[clang::musttail]] return scalar(ring, slot, cursor, remaining, sum, job);
}

[[gnu::noinline]] void realigned_check(Scalar& ring, Job& job) {
  alignas(512) volatile std::uint64_t canary[64];
  for (unsigned i = 0; i < 64; ++i) canary[i] = 0x76543210 + i;
  check(ring.push(clobber_entry, {4, 2, 0, &job}), "realigned admission");
  check(ring.spin({8}) == 2, "realigned entry work");
  for (unsigned i = 0; i < 64; ++i) check(canary[i] == 0x76543210 + i, "realigned frame borrow corrupted");
}

void frame_check(Fixture& fixture) {
  Scalar ring; Input input(fixture.worker);
  check(ring.configure(fixture.worker, {1, 0}), "frame ring");
  realigned_check(ring, input.jobs[0]);
  check(input.jobs[0].result == expected(4, 2) && input.jobs[0].done == 1, "realigned result");
  check(ring.close(), "frame close");
}

void hints() {
  alignas(4096) std::array<std::byte, 65536> qualified{};
  for (unsigned i = 0; i < 16; ++i) {
    const auto kind = static_cast<loom::Hint>(i);
    const auto full = loom::bind_dependency(kind, qualified);
    check(full && full->hint == kind, "qualified fixed class changed");
    const auto empty = loom::bind_dependency(kind, {});
    check(empty && empty->hint == loom::Hint::none, "empty span hint escaped");
  }
  check(!loom::bind_dependency(static_cast<loom::Hint>(16), qualified), "invalid class accepted");
  check(!loom::bind_dependency(loom::Hint::page1, qualified, 63), "invalid page geometry accepted");
  auto near_end = loom::bind_dependency(loom::Hint::page8, std::span(qualified).subspan(4095, 66));
  check(near_end && near_end->hint == loom::Hint::page1, "unaligned page-head bounds");
  auto short_page = loom::bind_dependency(loom::Hint::page1, std::span(qualified).subspan(4095, 65));
  check(short_page && short_page->hint == loom::Hint::l1_1, "last page hint exceeded span");
  auto two_lines = loom::bind_dependency(loom::Hint::l1_8, std::span(qualified).first(65));
  check(two_lines && two_lines->hint == loom::Hint::l1_2, "line class bounds");
  auto shape = loom::bind_hint_shape(loom::Hint::page8, 64);
  check(shape && *shape == loom::Hint::l1_1, "future address shape bounds");
  auto full_shape = loom::bind_hint_shape(loom::Hint::page8, 8 * 4096 + 65);
  check(full_shape && *full_shape == loom::Hint::page8, "qualified page shape");
}

void storage_lifetime(Fixture& fixture) {
  Scalar ring;
  check(ring.configure(fixture.worker, {1, 0}), "lifetime storage");
  // Even an empty Ring owns real live storage, so Pool close cannot finish.
  const auto progress = orbital::allocation::close(*fixture.worker.memory);
  check(progress.kind() != orbital::Progress::Kind::done && progress.kind() != orbital::Progress::Kind::failed,
        "Pool closed across Ring storage Allocation");
  check(ring.close(), "release actual Ring storage");
}
} // namespace

int main() {
  Fixture fixture;
  hints();
  scalar_checks(fixture);
  selective_checks(fixture);
  vector_checks(fixture);
  frame_check(fixture);
  rotation_checks(fixture);
  storage_lifetime(fixture);
  std::cout << "loom_ring_check: passed\n";
}
