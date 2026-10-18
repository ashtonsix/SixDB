#include <loom/execution.h>
#include <loom/ring.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <sched.h>
#include <string>
#include <thread>

namespace {
namespace ex = loom::execution;
using Clock = std::chrono::steady_clock;
constexpr unsigned width = 4, steps = 8, count = 64;
void check(bool value) { if (!value) std::abort(); }
unsigned next(unsigned cursor) { return (cursor * 5 + 3) % count; }
std::uint64_t value(unsigned cursor) { return cursor * 73 + 19; }
struct alignas(64) Node { unsigned next; std::uint64_t value; };
struct Frame { loom::DemandClass demand; std::uint64_t result{}; };
using Ring = loom::Ring<Frame*, unsigned, unsigned, std::uint64_t>::WithShared<const Node*>;

LOOM_NATIVE_CC void visit(Ring::Cursor ring, Ring::Slot* slot, const Node* nodes,
    Frame* frame, unsigned cursor, unsigned left, std::uint64_t sum) {
  const auto& node = nodes[cursor];
  sum += node.value;
  cursor = node.next;
  if (!--left) {
    frame->result = sum;
    [[clang::musttail]] return Ring::finish(ring, slot, nodes, frame, cursor, left, sum);
  }
  ring->prefetch(frame->demand, nodes + cursor);
  [[clang::musttail]] return Ring::yield(ring, slot, nodes, frame, cursor, left, sum);
}

[[gnu::noinline]] std::uint64_t serial(const Node* nodes, unsigned cursor) {
  std::uint64_t sum = 0;
  for (unsigned i = 0; i < steps; ++i) {
    const auto& node = nodes[cursor];
    sum += node.value;
    cursor = node.next;
  }
  return sum;
}
std::uint64_t expected(unsigned cursor) {
  std::uint64_t sum = 0;
  for (unsigned i = 0; i < steps; ++i) { sum += value(cursor); cursor = next(cursor); }
  return sum;
}
ex::Epoch epoch(Clock::time_point now) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}
}

int main(int argc, char** argv) {
  // An explicit illustrative cutoff keeps this example from prescribing a
  // crossover. Qualify real complete consumer paths before choosing a cutoff.
  check(argc == 2);
  const auto cutoff = std::stoul(argv[1]);
  check(cutoff <= std::numeric_limits<std::uint32_t>::max());
  auto policy = ex::Policy::bind({static_cast<std::uint32_t>(cutoff), 8, 2, 2, 32});
  check(bool(policy));
  const ex::Paths paths{true, true, width, width};
  loom::Worker worker;
  const auto cpu = sched_getcpu(); check(cpu >= 0);
  check(bool(loom::adopt(worker, cpu)));
  // This caller keeps one unchanged complete procedure pair. If its evidence
  // cohort changes, it must reset the Policy before reusing learned feedback.
  auto arena = orbital::allocation::open_arena(1024 * 1024); check(bool(arena));
  check(bool(loom::configure(worker, *arena, 0)));
  auto storage = loom::allocate(worker, {.bytes = count * sizeof(Node), .alignment = alignof(Node)});
  check(bool(storage));
  // One nonsuspending owner call preserves Allocation, initialized contents and
  // bindings through all direct calls and synchronous Ring drains below.
  auto* nodes = reinterpret_cast<Node*>(loom::bytes(*storage).data());
  for (unsigned i = 0; i < count; ++i) std::construct_at(nodes + i, Node{next(i), value(i)});
  auto demand = loom::bind_demand({loom::MemoryPattern::irregular, sizeof(Node)}, sizeof(Node));
  check(bool(demand));
  Ring ring; check(bool(ring.configure(worker, {width, 0}, {nodes})));
  std::array<Frame, width> frames;
  auto execute = [&](ex::Path path) {
    check(path == ex::Path::direct || path == ex::Path::ring);
    for (unsigned i = 0; i < width; ++i) {
      frames[i] = {*demand, 0};
      if (path == ex::Path::direct) frames[i].result = serial(nodes, i);
      else check(ring.push(visit, {&frames[i], i, steps, 0}, loom::MemoryDependency{nodes + i, *demand}));
    }
    while (ring.pending()) ring.spin({128});
    for (unsigned i = 0; i < width; ++i) check(frames[i].result == expected(i));
  };
  execute(ex::Path::direct); execute(ex::Path::ring); // Independent result controls, no evidence.
  unsigned probes = 0, direct = 0;
  const auto begin = Clock::now();
  for (unsigned batch = 0; batch < 64; ++batch) {
    auto now = epoch(Clock::now());
    if (policy->sample_due(now, paths)) {
      // The caller owns representativeness and rotation. Each selected chain
      // completes serially; it does not advance the useful batch's denominator.
      const unsigned head = batch % width;
      const auto start = Clock::now();
      const auto got = serial(nodes, head);
      const auto finish = Clock::now();
      check(got == expected(head));
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count();
      now = epoch(finish);
      // One useful request is this complete eight-node chain, not each load.
      if (elapsed) check(policy->observe(now, {std::uint64_t(elapsed), 1}));
      ++probes;
    }
    const auto decision = policy->select(now, paths);
    direct += decision.path == ex::Path::direct;
    execute(decision.path);
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
  std::printf("PASS: %u direct / %u Ring batches, %u serial probes; %lld ns including samples and checks\n",
      direct, 64 - direct, probes, static_cast<long long>(elapsed));
  check(bool(ring.close()));
  loom::release(*storage);
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  for (;;) {
    const auto progress = loom::shutdown(worker);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    check(progress.kind() != orbital::Progress::Kind::failed && Clock::now() < deadline);
    loom::poll(worker, {64, 0});
    std::this_thread::yield();
  }
  loom::relinquish(worker);
}
