#include <loom/ring.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using U = std::uint64_t;
using V = U __attribute__((vector_size(16)));
struct alignas(64) Frame {
  U id{}, remaining{}, sum{}, result{};
  std::array<V, 8> packet{};
};
#if FRAME_ARG
using Ring = loom::Ring<Frame*>;
#define PARAMS Frame* f
#define TRANSFER f
#else
using Ring = loom::Ring<Frame*, U, U, V, V, V, V, V, V, V, V>;
#define PARAMS Frame* f, U remaining, U sum, V a, V b, V c, V d, V e, V g, V h, V j
#define TRANSFER f, remaining, sum, a, b, c, d, e, g, h, j
#endif
LOOM_NATIVE_CC void metadata(Ring::Cursor, Ring::Slot*, PARAMS);
LOOM_NATIVE_CC void payload(Ring::Cursor, Ring::Slot*, PARAMS);

LOOM_NATIVE_CC void metadata(Ring::Cursor cursor, Ring::Slot* slot, PARAMS) {
#if FRAME_ARG
  f->sum += f->id + 13;
  slot->function = payload;
  [[clang::musttail]] return Ring::yield_unchanged(cursor, slot, TRANSFER);
#else
  sum += f->id + 13;
  slot->function = payload;
  [[clang::musttail]] return Ring::yield(cursor, slot, TRANSFER);
#endif
}

LOOM_NATIVE_CC void payload(Ring::Cursor cursor, Ring::Slot* slot, PARAMS) {
#if FRAME_ARG
  const V delta{f->id + f->remaining, f->sum ^ f->remaining};
  for (auto& v : f->packet) v += delta;
  if (!--f->remaining) {
    U result = f->sum;
    for (const auto v : f->packet) result += v[0] + v[1];
    f->result = result;
    [[clang::musttail]] return Ring::finish(cursor, slot, TRANSFER);
  }
  slot->function = metadata;
  [[clang::musttail]] return Ring::yield_unchanged(cursor, slot, TRANSFER);
#else
  const V delta{f->id + remaining, sum ^ remaining};
  a += delta; b += delta; c += delta; d += delta;
  e += delta; g += delta; h += delta; j += delta;
  if (!--remaining) {
    f->remaining = remaining; f->sum = sum;
    f->packet = {a, b, c, d, e, g, h, j};
    f->result = sum + a[0] + a[1] + b[0] + b[1] + c[0] + c[1] +
        d[0] + d[1] + e[0] + e[1] + g[0] + g[1] + h[0] + h[1] + j[0] + j[1];
    [[clang::musttail]] return Ring::finish(cursor, slot, TRANSFER);
  }
  slot->function = metadata;
  [[clang::musttail]] return Ring::yield(cursor, slot, TRANSFER);
#endif
}

void check(bool v) { if (!v) std::abort(); }
[[gnu::noinline]] U operation(Ring& ring, std::array<Frame, 8>& frames) {
  for (U i = 0; i < frames.size(); ++i) {
    auto& f = frames[i]; f.id = i; f.remaining = 7; f.sum = i * 5; f.result = 0;
    for (U j = 0; j < 8; ++j) f.packet[j] = V{i * 31 + j * 7, i * 41 + j * 11};
#if FRAME_ARG
    check(ring.push(metadata, {&f}));
#else
    check(ring.push(metadata, {&f, f.remaining, f.sum, f.packet[0], f.packet[1],
        f.packet[2], f.packet[3], f.packet[4], f.packet[5], f.packet[6], f.packet[7]}));
#endif
  }
  while (ring.pending()) ring.spin({4096});
  U sum = 0; for (const auto& f : frames) sum += f.result;
  return sum;
}

int main(int argc, char**) {
  loom::Worker worker; check(bool(loom::adopt(worker)));
  auto arena = orbital::allocation::open_arena(8 * 1024 * 1024); check(bool(arena));
  check(bool(loom::configure(worker, *arena, 8)));
  Ring ring; check(bool(ring.configure(worker, {8, 0})));
  std::array<Frame, 8> frames;
  U expected = 0;
  for (U i = 0; i < 8; ++i) {
    U sum = i * 5, remaining = 7;
    std::array<std::array<U, 2>, 8> v;
    for (U j = 0; j < 8; ++j) v[j] = {i * 31 + j * 7, i * 41 + j * 11};
    while (remaining) {
      sum += i + 13;
      for (auto& x : v) { x[0] += i + remaining; x[1] += sum ^ remaining; }
      --remaining;
    }
    expected += sum; for (const auto x : v) expected += x[0] + x[1];
  }
  const auto actual = operation(ring, frames); check(actual == expected);
  for (U i = 0; i < 8; ++i) {
    U sum = i * 5, remaining = 7;
    std::array<std::array<U, 2>, 8> v;
    for (U j = 0; j < 8; ++j) v[j] = {i * 31 + j * 7, i * 41 + j * 11};
    while (remaining) {
      sum += i + 13;
      for (auto& x : v) { x[0] += i + remaining; x[1] += sum ^ remaining; }
      --remaining;
    }
    check(frames[i].remaining == 0 && frames[i].sum == sum);
    for (U j = 0; j < 8; ++j)
      check(frames[i].packet[j][0] == v[j][0] && frames[i].packet[j][1] == v[j][1]);
  }
  check(ring.counters().visits == 112 && ring.counters().completed == 8);
  std::printf("PASS frame_arg=%d checksum=%llu expected=%llu visits=%llu slot_bytes=%zu frame_bytes=%zu\n",
      FRAME_ARG, (unsigned long long)actual, (unsigned long long)expected,
      (unsigned long long)ring.counters().visits, sizeof(Ring::Slot), sizeof(Frame));
  if (argc > 1) {
    for (unsigned i = 0; i < 100; ++i) check(operation(ring, frames) == expected);
    for (unsigned rep = 0; rep < 11; ++rep) {
      const auto before = ring.counters().visits;
      const auto start = std::chrono::steady_clock::now();
      U checksum = 0;
      for (unsigned i = 0; i < 1000; ++i) checksum += operation(ring, frames);
      const auto finish = std::chrono::steady_clock::now();
      check(checksum == 1000 * expected && ring.counters().visits - before == 112000);
      const auto ns = std::chrono::duration<double, std::nano>(finish - start).count();
      std::printf("TIMING frame_arg=%d rep=%u operations=1000 visits=112000 ns_per_operation=%.3f checksum=%llu\n",
          FRAME_ARG, rep, ns / 1000, (unsigned long long)checksum);
    }
  }
  check(bool(ring.close()));
  for (;;) {
    const auto progress = loom::shutdown(worker);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    check(progress.kind() != orbital::Progress::Kind::failed);
    loom::poll(worker, {256, 0}); std::this_thread::yield();
  }
  loom::relinquish(worker);
}
