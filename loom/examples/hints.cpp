#include <loom/hardware.h>
#include <loom/ring.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>

// One shared startup service, then ordinary owner-boundary refreshes. The
// operation keeps physical use, initialized contents and frames alive itself.
namespace {
void check(bool value) { if (!value) std::abort(); }
struct alignas(64) Node { std::uint64_t next{}, value{}; std::array<std::uint64_t, 6> padding{}; };
struct Frame { loom::DemandClass demand; std::uint64_t result{}; };
using Ring = loom::Ring<Frame*, std::uint64_t, std::uint64_t, std::uint64_t>::WithShared<const Node*>;
LOOM_NATIVE_CC void visit(Ring::Cursor ring, Ring::Slot* slot, const Node* nodes, Frame* frame,
    std::uint64_t cursor, std::uint64_t left, std::uint64_t sum) {
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
}
int main(int argc, char** argv) {
  loom::HardwareOptions options;
  // This example bounds its demonstration; production startup chooses its own
  // calibration memory/time allowance. Small probes may remain unresolved.
  options.limits = {2 * 1024 * 1024, 3 * 1024 * 1024, std::chrono::milliseconds(100)};
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if (argument == "--force-miss") options.table = {};
    else if (argument == "--calibrate") options.limits = {};
    else check(false);
  }
  loom::HardwareService hardware(options);
  auto context = loom::discover_hardware(); check(bool(context));
  auto profile = hardware.request(*context); check(bool(profile));
  loom::Worker worker;
  check(bool(loom::adopt(worker, context->cpu)));
  auto arena = orbital::allocation::open_arena(8 * 1024 * 1024); check(bool(arena));
  check(bool(loom::configure(worker, *arena, 0, *profile)));
  auto storage = loom::allocate(worker, {.bytes = 64 * sizeof(Node), .alignment = alignof(Node)});
  check(bool(storage));
  auto held = loom::hold(*storage, {0, 64 * sizeof(Node)}); check(bool(held));
  auto* nodes = reinterpret_cast<Node*>(held->address);
  for (unsigned i = 0; i < 64; ++i) std::construct_at(nodes + i, Node{(i * 5 + 3) & 63, i * 73 + 19, {}});
  auto demand = loom::bind_demand({loom::MemoryPattern::irregular, sizeof(Node)}, sizeof(Node));
  check(bool(demand));
  // This base is common to every frame. Load it once at spin entry, then carry
  // it through the native tail jumps instead of restoring it from each Slot.
  Ring ring; check(bool(ring.configure(worker, {4, 0}, {nodes})));
  std::array<Frame, 4> frames;
  const auto deadline = std::chrono::steady_clock::now() + options.limits.wall_time + std::chrono::seconds(1);
  std::uint64_t batches = 0;
  do {
    check(bool(loom::refresh_hints(worker)));
    check(bool(ring.refresh_hints()));
    for (unsigned i = 0; i < frames.size(); ++i) {
      frames[i] = {*demand, 0};
      check(ring.push(visit, {&frames[i], i, 31, 0}, loom::MemoryDependency{nodes + i, *demand}));
    }
    while (ring.pending()) ring.spin({128});
    for (unsigned i = 0; i < frames.size(); ++i) {
      std::uint64_t expected = 0, cursor = i;
      for (unsigned step = 0; step < 31; ++step) { expected += cursor * 73 + 19; cursor = (cursor * 5 + 3) & 63; }
      check(frames[i].result == expected);
    }
    ++batches;
    if ((*profile)->snapshot().state != loom::HardwareState::pending) break;
  } while (std::chrono::steady_clock::now() < deadline);
  hardware.cancel();
  check(bool(loom::refresh_hints(worker)));
  check(bool(ring.close()));
  loom::release(*held); loom::release(*storage);
  for (;;) {
    const auto progress = loom::shutdown(worker);
    check(progress.kind() != orbital::Progress::Kind::failed);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    loom::poll(worker, {64, 0});
    std::this_thread::yield();
  }
  loom::relinquish(worker);
  const auto observed = (*profile)->snapshot();
  std::printf("PASS: %llu native batches while startup state=%u, revision=%llu, width=%u (%s), training=%u (%s)\n",
      static_cast<unsigned long long>(batches), unsigned(observed.state),
      static_cast<unsigned long long>(worker.hints.profile.revision), observed.profile.preferred_width,
      observed.width_qualified ? "local candidate" : "default",
      observed.profile.training_accesses, observed.training_qualified ? "qualified" : "unknown");
}
