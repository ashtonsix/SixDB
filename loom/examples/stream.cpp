#include <loom/ring.h>
#include <loom/stream.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <thread>

// A known-key stream and an unrelated dependent chain share the existing Ring.
// This complete synchronous call keeps all bytes/frames alive through cleanup.
namespace {
void check(bool result) { if (!result) std::abort(); }
struct alignas(64) Row {
  std::uint64_t next{}, value{};
  std::array<std::uint64_t, 6> padding{};
};
using Ring = loom::Ring<void*>;
struct Known {
  loom::IndependentStream<unsigned, 8, 16> stream;
  const Row* rows;
  loom::DemandClass demand;
  std::uint64_t sum{};
};
struct Dependent {
  const Row* rows;
  loom::DemandClass demand;
  std::uint64_t next{2}, left{31}, sum{};
};

LOOM_NATIVE_CC void known(Ring::Cursor ring, Ring::Slot* slot, void* opaque) {
  auto& frame = *static_cast<Known*>(opaque);
  auto sum = frame.sum;
  const auto done = frame.stream.advance(
      [&](const unsigned& key) noexcept { ring->prefetch(frame.demand, frame.rows + key); },
      [&](const unsigned& key) noexcept { sum += frame.rows[key].value; });
  frame.sum = sum;
  if (done) [[clang::musttail]] return Ring::finish(ring, slot, opaque);
  [[clang::musttail]] return Ring::yield_unchanged(ring, slot, opaque);
}
LOOM_NATIVE_CC void dependent(Ring::Cursor ring, Ring::Slot* slot, void* opaque) {
  auto& frame = *static_cast<Dependent*>(opaque);
  const auto& row = frame.rows[frame.next];
  frame.sum += row.value;
  frame.next = row.next;
  if (!--frame.left) [[clang::musttail]] return Ring::finish(ring, slot, opaque);
  ring->prefetch(frame.demand, frame.rows + frame.next);
  [[clang::musttail]] return Ring::yield_unchanged(ring, slot, opaque);
}
}

int main() {
  std::array<Row, 64> rows;
  for (unsigned i = 0; i < rows.size(); ++i) rows[i] = {(i * 5 + 3) % 64, i * 73 + 19, {}};
  std::array<unsigned, 73> keys;
  for (unsigned i = 0; i < keys.size(); ++i) keys[i] = (i * 17 + 3) % 64;
  // Demand binding and the Ring's hardware/pressure snapshot are setup work.
  // Each typed body reuses Loom's selection; the stream only sequences keys.
  auto demand = loom::bind_demand({loom::MemoryPattern::irregular, sizeof(Row)}, sizeof(Row));
  check(bool(demand));
  Known stream{loom::IndependentStream<unsigned, 8, 16>{keys}, rows.data(), *demand};
  Dependent chain{rows.data(), *demand};

  loom::Worker worker;
  check(bool(loom::adopt(worker)));
  auto arena = orbital::allocation::open_arena(8 * 1024 * 1024);
  check(bool(arena));
  check(bool(loom::configure(worker, *arena, 0)));
  Ring ring;
  check(bool(ring.configure(worker, {2, 0})));
  check(ring.push(known, {&stream}));
  check(ring.push(dependent, {&chain}));
  // Values illustrate work grains; choose them for the consumer's service needs.
  while (ring.pending()) ring.spin({4});

  std::uint64_t expected_stream{}, expected_chain{}, next = 2;
  for (unsigned i = 0; i < keys.size(); ++i) expected_stream += ((i * 17 + 3) % 64) * 73 + 19;
  for (unsigned i = 0; i < 31; ++i) {
    expected_chain += next * 73 + 19;
    next = (next * 5 + 3) % 64;
  }
  check(stream.stream.processed() == keys.size() && stream.sum == expected_stream);
  check(!chain.left && chain.sum == expected_chain && chain.next == next);
  check(ring.counters().visits == 10 + 31 && ring.counters().completed == 2);
  check(bool(ring.close()));
  for (;;) {
    const auto progress = loom::shutdown(worker);
    check(progress.kind() != orbital::Progress::Kind::failed);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    loom::poll(worker, {64, 0});
    std::this_thread::yield();
  }
  loom::relinquish(worker);
  std::puts("PASS: 73 known-key requests and 31 dependent steps shared one Ring");
}
