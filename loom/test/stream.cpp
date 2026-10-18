#include <loom/stream.h>

#include <loom/ring.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
static_assert(sizeof(loom::IndependentStream<unsigned, 32>) == 3 * sizeof(std::size_t));
static_assert(std::is_trivially_copyable_v<loom::IndependentStream<unsigned, 32>>);
std::uint64_t checks{};
void check(bool condition, const char* message) {
  ++checks;
  if (!condition) {
    std::cerr << "loom_stream_check: " << message << '\n';
    std::abort();
  }
}
template<class T> void check(const orbital::Result<T>& result, const char* message) {
  check(result.has_value(), message);
}

struct CountingBody {
  std::size_t* count;
  explicit CountingBody(std::size_t& value) noexcept : count(&value) {}
  CountingBody(const CountingBody&) = delete;
  CountingBody(CountingBody&&) = delete;
  void operator()(const unsigned&) & noexcept { ++*count; }
};
void inline_bodies() {
  std::array<unsigned, 2> keys{7, 7};
  loom::IndependentStream<unsigned, 1> stream(keys);
  std::size_t hinted{}, consumed{};
  // Bodies are called as lvalues and never copied, even when bound temporaries.
  check(!stream.advance(CountingBody{hinted}, CountingBody{consumed}) && hinted == 2 && consumed == 1,
      "inline callback invocation/copy contract");
  check(stream.advance(CountingBody{hinted}, CountingBody{consumed}) && hinted == 2 && consumed == 2,
      "inline callback reentry frontier");
  hinted = consumed = 0;
  loom::prefetch_for_each(std::span<const unsigned>{keys}, CountingBody{hinted}, CountingBody{consumed});
  check(hinted == 2 && consumed == 2, "synchronous utility preserves noncopyable lvalue bodies");
}

template<std::size_t Q, std::size_t D>
void cursor_checks() {
  constexpr std::array<std::size_t, 12> sizes{0, 1, 2, 3, 4, 7, 31, 32, 33, 65, 137, 257};
  for (const auto size : sizes) for (const auto all_miss : {false, true}) {
    std::vector<unsigned> keys(size);
    for (std::size_t i = 0; i < size; ++i) keys[i] = unsigned((i * 17 + 3) % 11);
    std::vector<std::size_t> hinted, consumed;
    std::uint64_t hits{}, sum{};
    loom::IndependentStream<unsigned, Q, D> stream(keys);
    const auto prefetch = [&](const unsigned& key) noexcept {
      hinted.push_back(std::size_t(&key - keys.data()));
    };
    const auto consume = [&](const unsigned& key) noexcept {
      const auto index = std::size_t(&key - keys.data());
      check(index == consumed.size(), "consume skipped or repeated an index");
      // Independent event-order oracle: the hint for i+D is before find(i).
      const auto through_current = index + 1;
      check(hinted.size() == through_current + std::min(D, size - through_current),
          "future hint did not precede consume");
      consumed.push_back(index);
      if (!all_miss && key % 3 == 0) { ++hits; sum += key * 19 + 5; }
    };
    do {
      const auto before = consumed.size();
      const auto done = stream.advance(prefetch, consume);
      const auto expected = before + std::min(Q, size - before);
      check(consumed.size() == expected && stream.processed() == expected,
          "a visit exceeded or underfilled its useful-key quantum");
      check(stream.remaining() == size - expected && stream.empty() == (expected == size),
          "cursor remaining/completion mismatch");
      check(done == (expected == size), "return completion depends on hits");
      check(hinted.size() == expected + std::min(D, size - expected),
          "implicit frontier changed across reentry");
    } while (!stream.empty());
    check(hinted.size() == size && consumed.size() == size, "request multiplicity differs");
    for (std::size_t i = 0; i < size; ++i)
      check(hinted[i] == i && consumed[i] == i, "duplicate-valued key indexes not preserved");
    std::uint64_t expected_hits{}, expected_sum{};
    for (std::size_t i = 0; i < size; ++i) {
      const auto value = unsigned((i * 17 + 3) % 11);
      if (!all_miss && value % 3 == 0) { ++expected_hits; expected_sum += value * 19 + 5; }
    }
    check(hits == expected_hits && sum == expected_sum, "independent useful output differs");
    for (unsigned repeat = 0; repeat < 3; ++repeat)
      check(stream.advance(prefetch, consume) && hinted.size() == size && consumed.size() == size,
          "a completed/empty cursor performed more work");
    hinted.clear(); consumed.clear(); hits = sum = 0;
    loom::prefetch_for_each<D>(std::span<const unsigned>{keys}, prefetch, consume);
    check(hinted.size() == size && consumed.size() == size && hits == expected_hits && sum == expected_sum,
        "synchronous utility complete output/request count differs");
    for (std::size_t i = 0; i < size; ++i)
      check(hinted[i] == i && consumed[i] == i, "synchronous utility request order differs");
  }
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

struct Event {
  unsigned id{};
  std::size_t before{}, after{};
  bool done{};
  bool operator==(const Event&) const = default;
};
struct Trace {
  std::array<Event, 128> events{};
  std::size_t size{};
  void append(Event event) noexcept {
    check(size < events.size(), "fixed trace overflow");
    events[size++] = event;
  }
};
using Ring = loom::Ring<void*>;

template<class Key, std::size_t N, std::size_t Q, std::size_t D, unsigned Id>
struct Work {
  std::array<Key, N> keys{};
  loom::IndependentStream<Key, Q, D> stream;
  std::array<std::size_t, N> hint_order{};
  std::size_t hints{}, consumed{}, completed{};
  std::uint64_t hits{}, sum{};
  Trace* trace;

  explicit Work(Trace& log) : stream(keys), trace(&log) {
    for (std::size_t i = 0; i < N; ++i) keys[i] = Key((i * (Id + 3) + 1) % 13);
  }
  // Fake readers deliberately use different key types and value laws. Actual
  // hash-table callers put their canonical inline prefetch/find bodies here.
  void prefetch(const Key& key) noexcept {
    check(hints < N, "native visit repeated hints");
    hint_order[hints++] = std::size_t(&key - keys.data());
  }
  std::uint64_t find(const Key& key) const noexcept { return std::uint64_t(key) * (Id + 11) + 7; }

  static LOOM_NATIVE_CC void visit(Ring::Cursor ring, Ring::Slot* slot, void* opaque) {
    auto& work = *static_cast<Work*>(opaque);
    const auto before = work.stream.processed();
    auto sum = work.sum;
    auto hits = work.hits;
    const auto done = work.stream.advance(
        [&](const Key& key) noexcept { work.prefetch(key); },
        [&](const Key& key) noexcept {
          check(std::size_t(&key - work.keys.data()) == work.consumed++, "native consume order");
          if (key % (Id + 2) == 0) { ++hits; sum += work.find(key); }
        });
    // Publish each scalar once before handing this frame back to the Ring.
    work.sum = sum;
    work.hits = hits;
    work.trace->append({Id, before, work.stream.processed(), done});
    if (done) {
      ++work.completed;
      [[clang::musttail]] return Ring::finish(ring, slot, opaque);
    }
    [[clang::musttail]] return Ring::yield_unchanged(ring, slot, opaque);
  }
  void verify() const {
    check(stream.processed() == N && hints == N && consumed == N && completed == 1,
        "native stream useful requests/completion differ");
    std::uint64_t expected_hits{}, expected_sum{};
    for (std::size_t i = 0; i < N; ++i) {
      check(hint_order[i] == i, "native duplicate request hint multiplicity differs");
      const auto key = Key((i * (Id + 3) + 1) % 13);
      if (key % (Id + 2) == 0) {
        ++expected_hits; expected_sum += std::uint64_t(key) * (Id + 11) + 7;
      }
    }
    check(hits == expected_hits && sum == expected_sum, "native independent output oracle differs");
  }
};
using A = Work<std::uint64_t, 37, 4, 32, 0>;
using B = Work<std::uint32_t, 17, 7, 3, 1>;
using Empty = Work<std::uint16_t, 0, 32, 32, 3>;

struct Chain {
  struct Node { unsigned next{}, value{}; };
  std::array<Node, 19> nodes{};
  Trace* trace;
  std::size_t done{}, completed{};
  unsigned cursor{2};
  std::uint64_t sum{};
  explicit Chain(Trace& log) : trace(&log) {
    for (unsigned i = 0; i < nodes.size(); ++i) nodes[i] = {(i * 7 + 3) % 19, i * 23 + 11};
  }
  static LOOM_NATIVE_CC void visit(Ring::Cursor ring, Ring::Slot* slot, void* opaque) {
    auto& work = *static_cast<Chain*>(opaque);
    const auto before = work.done++;
    const auto node = work.nodes[work.cursor];
    work.sum += node.value;
    work.cursor = node.next;
    const auto done = work.done == 11;
    work.trace->append({2, before, work.done, done});
    if (done) {
      ++work.completed;
      [[clang::musttail]] return Ring::finish(ring, slot, opaque);
    }
    [[clang::musttail]] return Ring::yield_unchanged(ring, slot, opaque);
  }
  void verify() const {
    unsigned expected_cursor = 2;
    std::uint64_t expected_sum{};
    for (unsigned i = 0; i < 11; ++i) {
      expected_sum += expected_cursor * 23 + 11;
      expected_cursor = (expected_cursor * 7 + 3) % 19;
    }
    check(done == 11 && completed == 1 && cursor == expected_cursor && sum == expected_sum,
        "unrelated dependent chain output differs");
  }
};

void mixed_rotation(Fixture& fixture, std::uint64_t budget) {
  Trace trace;
  A a(trace), refused(trace);
  B b(trace);
  Chain chain(trace);
  Ring ring;
  check(ring.configure(fixture.worker, {3, 0}), "configure mixed Ring");
  check(ring.push(A::visit, {&a}) && ring.push(B::visit, {&b}) && ring.push(Chain::visit, {&chain}),
      "heterogeneous admission");
  check(!ring.push(A::visit, {&refused}) && refused.hints == 0 && !refused.stream.processed(),
      "refusal started a borrowed stream");
  check(ring.spin({0}) == 0 && trace.size == 0, "zero budget started priming");
  while (ring.pending()) {
    const auto before = trace.size;
    const auto visits = ring.spin({budget});
    check(visits > 0 && visits <= budget && trace.size - before == visits,
        "bounded native visit count differs");
  }
  a.verify(); b.verify(); chain.verify();
  check(!refused.hints && !refused.stream.processed(), "refused stream ran later");

  // Independent round-robin model uses useful request counts, no actual cursor
  // or callback trace to choose the expected next worker.
  constexpr std::array<std::size_t, 3> lengths{37, 17, 11}, quanta{4, 7, 1};
  std::array<std::size_t, 3> processed{};
  std::vector<unsigned> queue{0, 1, 2};
  std::size_t event{};
  while (!queue.empty()) {
    const auto id = queue.front();
    queue.erase(queue.begin());
    const auto before = processed[id];
    const auto after = std::min(lengths[id], before + quanta[id]);
    processed[id] = after;
    const auto done = after == lengths[id];
    check(event < trace.size && trace.events[event++] == Event{id, before, after, done},
        "other callback kinds were not served between stream quanta");
    if (!done) queue.push_back(id);
  }
  check(event == trace.size && ring.counters().completed == 3 && ring.counters().refused == 1,
      "heterogeneous physical inventory differs");
  check(ring.close(), "close completed mixed Ring");
}

void overflow_and_discard(Fixture& fixture) {
  Trace trace;
  A a(trace);
  B b(trace);
  Empty empty(trace);
  Ring ring;
  check(ring.configure(fixture.worker, {1, 2}), "configure overflow Ring");
  check(ring.push(A::visit, {&a}) && ring.push(B::visit, {&b}) && ring.push(Empty::visit, {&empty}),
      "overflow admission");
  check(!a.hints && !b.hints && !empty.hints, "admission performed stream hints");
  check(ring.spin({1}) == 1 && a.stream.processed() == 4 && a.hints == 36 && !b.hints,
      "waiting overflow progressed or implicit frontier differs");
  check(ring.release_if([&](const Ring::Arguments& arguments) noexcept {
    return std::get<0>(arguments) == &a;
  }) == 1, "returned-boundary cancellation");
  const auto cancelled_hints = a.hints;
  while (ring.pending()) check(ring.spin({1}) == 1, "overflow survivor progress");
  b.verify(); empty.verify();
  check(a.stream.processed() == 4 && a.hints == cancelled_hints && !a.completed,
      "cancelled stream accessed a borrower after removal");
  check(ring.counters().completed == 2 && ring.counters().abandoned == 1,
      "cancelled/overflow completion accounting");
  check(ring.push(A::visit, {&a}) && ring.spin({1}) == 1, "partial stream explicit readmission");
  check(a.stream.processed() == 8 && a.hints == 37, "readmission re-primed or skipped a future hint");
  check(ring.discard() == 1 && !ring.pending(), "returned-boundary discard");
  check(ring.spin({8}) == 0 && a.stream.processed() == 8, "discarded borrower ran");
  check(ring.close(), "close discarded Ring");
}
} // namespace

int main() {
  inline_bodies();
  cursor_checks<1, 0>(); cursor_checks<1, 32>(); cursor_checks<3, 3>();
  cursor_checks<4, 32>(); cursor_checks<32, 32>(); cursor_checks<128, 32>();
  cursor_checks<32, 64>(); cursor_checks<128, 0>();
  cursor_checks<3, std::numeric_limits<std::size_t>::max()>();
  cursor_checks<std::numeric_limits<std::size_t>::max(), 32>();
  cursor_checks<std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::size_t>::max()>();
  Fixture fixture;
  for (const auto budget : {1, 2, 3, 7, 128}) mixed_rotation(fixture, budget);
  overflow_and_discard(fixture);
  std::cout << checks << " independent cursor/mixed native Ring checks passed\n";
}
