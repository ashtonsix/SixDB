#include <loom/hints.h>
#include <loom/ring.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

namespace {
namespace tp = ikea::tuplepack;

void check(bool condition, const char* message) {
  if (!condition) { std::cerr << "loom_hint_check: " << message << '\n'; std::abort(); }
}
template<class T, class E> void check(const std::expected<T, E>& result, const char* message) {
  check(result.has_value(), message);
}
template<class T, class E> T require(std::expected<T, E> result, const char* message) {
  check(result, message);
  return std::move(*result);
}

// Independent address oracle: enumerate the hint instruction targets, not
// cache-line coverage or inferred rights over the rest of each hinted line.
template<class Visit> void targets(loom::Hint hint, std::uintptr_t address,
    loom::HintGeometry geometry, Visit visit) {
  unsigned lines = 0, pages = 0;
  switch (hint) {
    case loom::Hint::none: return;
    case loom::Hint::l1_1: case loom::Hint::l2_1: case loom::Hint::l3_1: lines = 1; break;
    case loom::Hint::l1_2: case loom::Hint::l2_2: lines = 2; break;
    case loom::Hint::l1_4: case loom::Hint::l2_4: lines = 4; break;
    case loom::Hint::l1_8: case loom::Hint::l2_8: lines = 8; break;
    case loom::Hint::l1_l2_2: lines = 3; break;
    case loom::Hint::l1_l2_4: lines = 5; break;
    case loom::Hint::page1: pages = 1; break;
    case loom::Hint::page2: pages = 2; break;
    case loom::Hint::page4: pages = 4; break;
    case loom::Hint::page8: pages = 8; break;
    default: check(false, "policy contained an invalid mechanism");
  }
  if (lines) {
    for (unsigned line = 0; line < lines; ++line) visit(address + line * geometry.line_bytes);
    return;
  }
  visit(address);
  const auto next_page = address + geometry.page_bytes - address % geometry.page_bytes;
  for (unsigned page = 0; page < pages; ++page) {
    visit(next_page + page * geometry.page_bytes);
    visit(next_page + page * geometry.page_bytes + geometry.line_bytes);
  }
}

void admission_and_envelopes() {
  constexpr std::array<std::size_t, 8> extents{64, 128, 256, 512, 1024, 8192, 32768, 65536};
  const std::array geometries{
    loom::HintGeometry{16, 64}, loom::HintGeometry{32, 4096},
    loom::HintGeometry{64, 4096}, loom::HintGeometry{128, 16384}};
  for (const auto geometry : geometries) {
    check(loom::valid_hint_geometry(geometry), "valid geometry refused");
    for (const auto training : {0u, 2u, 4u, 64u}) {
      for (const auto pressure : {loom::HintPressure::low, loom::HintPressure::busy}) {
        const auto policy = loom::make_hint_policy({17, geometry, 8, training}, pressure);
        check(policy.codes[16] == loom::Hint::none, "empty demand emitted a hint");
        loom::prefetch(policy, loom::DemandClass{}, nullptr);
        for (unsigned code = 0; code < 16; ++code) {
          const auto pattern = code < 8 ? loom::MemoryPattern::irregular : loom::MemoryPattern::linear;
          const auto extent = extents[code % 8];
          const auto admitted = require(loom::bind_demand({pattern, extent}, extent), "admit full class");
          check(admitted.code() == code && admitted.minimum_bytes() == (code % 8 ? extent : 1),
                "full class or minimum changed");
          for (std::size_t alignment = 0; alignment < geometry.page_bytes; ++alignment) {
            const auto address = geometry.page_bytes * 4 + alignment;
            targets(policy.codes[code], address, geometry, [&](std::uintptr_t target) {
              check(target >= address && target - address < admitted.minimum_bytes(),
                    "policy hint escaped a qualified window");
            });
          }
        }
      }
    }
  }

  alignas(4096) std::array<std::byte, 65536> occupied{};
  for (const auto pattern : {loom::MemoryPattern::irregular, loom::MemoryPattern::linear}) {
    for (const auto requested : {std::size_t(1), std::size_t(63), std::size_t(64),
         std::size_t(127), std::size_t(128), std::size_t(129), std::size_t(65536)}) {
      for (const auto qualified : {std::size_t(1), std::size_t(64), std::size_t(127),
           std::size_t(128), std::size_t(255), std::size_t(1024), std::size_t(65536)}) {
        unsigned expected = 0;
        while (expected + 1 < extents.size() &&
               extents[expected + 1] <= std::min(requested, qualified)) ++expected;
        const auto dependency = require(loom::bind_dependency({pattern, requested},
            std::span<const std::byte>(occupied).first(qualified)), "qualified short dependency");
        check(dependency.address == occupied.data() &&
              dependency.demand.code() == expected + (pattern == loom::MemoryPattern::linear ? 8 : 0),
              "demand did not floor to supplied bytes");
      }
    }
  }
  check(!loom::bind_demand({loom::MemoryPattern::linear, 0}, 64), "zero request admitted");
  check(!loom::bind_demand({loom::MemoryPattern::linear, 64}, 0), "empty window admitted");
  check(!loom::bind_demand({static_cast<loom::MemoryPattern>(2), 64}, 64), "invalid pattern admitted");
  check(!loom::bind_dependency({loom::MemoryPattern::linear, 64}, {}), "null empty span admitted");
  check(!loom::bind_dependency({loom::MemoryPattern::linear, 64},
        std::span<const std::byte>(occupied).first(0)), "non-null empty span admitted");

  for (const auto geometry : {loom::HintGeometry{0, 4096}, loom::HintGeometry{8, 4096},
       loom::HintGeometry{48, 4096}, loom::HintGeometry{128, 64},
       loom::HintGeometry{64, 63}, loom::HintGeometry{64, 8191},
       loom::HintGeometry{64, std::size_t(1) << 31}})
    check(!loom::valid_hint_geometry(geometry), "invalid geometry admitted");
  for (const auto profile : {loom::HintProfile{0, {}, 0, 0}, loom::HintProfile{0, {}, 65, 0},
       loom::HintProfile{0, {}, 8, 1}, loom::HintProfile{0, {}, 8, 65}})
    check(!loom::valid_hint_profile(profile), "invalid profile admitted");
}

struct Fixture {
  loom::Worker worker;
  orbital::Ref<orbital::allocation::Arena> arena;
  Fixture() {
    check(loom::adopt(worker), "lane adoption");
    arena = require(orbital::allocation::open_arena(8 * 1024 * 1024), "open arena");
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
    loom::relinquish(worker);
  }
};

void profile_validation(Fixture& fixture) {
  const auto initial = fixture.worker.hints;
  check(loom::valid_hint_profile(initial.profile), "default Worker profile invalid");
  const auto unchanged = [&] {
    check(fixture.worker.hints.profile.revision == initial.profile.revision &&
          fixture.worker.hints.profile.geometry == initial.profile.geometry &&
          fixture.worker.hints.codes == initial.codes, "rejection changed Worker policy");
  };
  check(!loom::set_hint_profile(fixture.worker, initial.profile), "equal revision accepted");
  unchanged();
  auto invalid = initial.profile;
  ++invalid.revision; invalid.preferred_width = 0;
  check(!loom::set_hint_profile(fixture.worker, invalid), "zero width profile installed"); unchanged();
  invalid = initial.profile; ++invalid.revision; invalid.training_accesses = 1;
  check(!loom::set_hint_profile(fixture.worker, invalid), "invalid training installed"); unchanged();
  invalid = initial.profile; ++invalid.revision; invalid.geometry.page_bytes *= 2;
  check(!loom::set_hint_profile(fixture.worker, invalid), "changed geometry installed"); unchanged();
  check(!loom::set_hint_pressure(fixture.worker, static_cast<loom::HintPressure>(2)),
        "invalid pressure installed"); unchanged();
}

#if defined(__aarch64__) || defined(__AVX2__)
using Carrier = tp::native::pipeline_values;
using Vector = Carrier::vector;
using Reader = tp::author::native_reader<64, std::byte>;

tp::layout format() {
  std::array<tp::code, 64> codes;
  for (unsigned i = 0; i < 64; ++i) codes[i] = {std::uint8_t(i), 1, 7};
  return require(tp::layout::make(64, codes), "TuplePack format");
}
tp::read_plan<64> plan(const tp::layout& layout) {
  std::array<std::uint8_t, 64> map;
  for (unsigned i = 0; i < map.size(); ++i) map[i] = i;
  return require(tp::read_plan<64>::make(layout, map), "TuplePack reader plan");
}

struct PackedInput {
  static constexpr std::size_t rows = 16, stride = 65536;
  orbital::allocation::Allocation allocation;
  orbital::allocation::Hold use;
  tp::layout layout;
  tp::read_plan<64> prepared;
  tp::view source;
  tp::reader<64, std::byte> operation;
  Reader reader;
  unsigned seed;

  PackedInput(loom::Worker& worker, unsigned seed)
      : allocation(require(loom::allocate(worker, {.bytes = rows * stride}), "input allocation")),
        use(require(loom::hold(allocation, {0, rows * stride}), "input hold")),
        layout(format()), prepared(plan(layout)),
        source(require(tp::view::attach(layout,
            {loom::bytes(use).data(), rows * stride}, rows, stride),
            "TuplePack source")),
        operation(require(tp::bind_reader(prepared, source), "TuplePack operation")),
        reader(operation), seed(seed) {
    std::fill(loom::bytes(use).begin(), loom::bytes(use).end(), std::byte{0});
    for (unsigned row = 0; row < rows; ++row)
      for (unsigned byte = 0; byte < 64; ++byte)
        source.row_unchecked(row)[byte] = std::byte((row * 17 + byte * 13 + seed) & 255);
    check(reader.admit(rows - 1), "TuplePack last row admission");
  }
};

struct Frame {
  PackedInput* input{};
  loom::DemandClass demand;
  std::uint64_t identity{}, start{}, steps{}, sum{}, visits{}, done{};
  Carrier output{};
};

template<std::size_t> using Part = Vector;
template<class> struct Family;
template<std::size_t... V> struct Family<std::index_sequence<V...>> {
  using Ring = loom::Ring<Frame*, std::uint64_t, std::uint64_t, std::uint64_t, Part<V>...>;
  [[gnu::noinline]] static LOOM_NATIVE_CC void work(typename Ring::Cursor ring, typename Ring::Slot* slot, Frame* frame,
      std::uint64_t row, std::uint64_t remaining, std::uint64_t sum, Part<V>... values) {
    const auto decoded = frame->input->reader.get_unchecked(row);
    const Carrier accumulator{{values...}};
    const auto next = Carrier::from(tp::native::bit_or(accumulator.get(), decoded));
    sum += tp::native::word<0>(decoded) + row + frame->identity;
    ++frame->visits;
    row = (row + 1) & 15;
    if (!--remaining) {
      frame->output = next;
      frame->sum = sum;
      ++frame->done;
      ring->complete(*slot);
    } else {
      ring->prefetch(frame->demand, frame->input->source.row_unchecked(row));
    }
    [[clang::musttail]] return Ring::yield(ring, slot, frame, row, remaining, sum, next.v[V]...);
  }
  static LOOM_NATIVE_CC void clobber(typename Ring::Cursor ring, typename Ring::Slot* slot, Frame* frame,
      std::uint64_t row, std::uint64_t remaining, std::uint64_t sum, Part<V>... values) {
#if defined(__aarch64__)
    __asm__ volatile("mov x19, #321" ::: "x19");
#endif
    [[clang::musttail]] return work(ring, slot, frame, row, remaining, sum, values...);
  }
  static bool enqueue(Ring& ring, Frame& frame, bool realigned = false) {
    const auto qualified = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(frame.input->source.row_unchecked(frame.start)),
        frame.demand.minimum_bytes());
    const loom::MemoryDependency dependency{qualified.data(), frame.demand};
    return ring.push(realigned ? clobber : work,
                     typename Ring::Arguments{&frame, frame.start, frame.steps, 0, Part<V>{}...},
                     dependency);
  }
};
using Native = Family<std::make_index_sequence<Carrier::parts>>;

void output(const Frame& frame) {
  std::array<std::uint8_t, 64> expected{};
  std::uint64_t sum = 0;
  for (std::uint64_t step = 0; step < frame.steps; ++step) {
    const auto row = (frame.start + step) & 15;
    std::uint64_t first = 0;
    for (unsigned byte = 0; byte < 64; ++byte) {
      const auto decoded = std::uint8_t(((row * 17 + byte * 13 + frame.input->seed) & 255) >> 1);
      expected[byte] |= decoded;
      if (byte < 8) first |= std::uint64_t(decoded) << (byte * 8);
    }
    sum += first + row + frame.identity;
  }
  std::array<std::uint8_t, 64> actual;
  tp::native::store_packet(actual.data(), frame.output.get());
  check(actual == expected && frame.sum == sum && frame.done == 1 && frame.visits == frame.steps,
        "native scalar/vector output, once or visit count");
}

struct Waiting {
  loom::Task task;
  orbital::Signal signal;
  unsigned calls{};
  explicit Waiting(loom::Worker& worker) {
    signal.lane = &worker.lane;
    signal.state = orbital::Readiness::pending;
    task.context = this;
    task.step = [](loom::Task& task) noexcept {
      auto& waiting = *static_cast<Waiting*>(task.context);
      ++waiting.calls;
      return orbital::Progress::wait(waiting.signal);
    };
    check(loom::start(worker, task), "waiting Task admission");
    check(loom::poll(worker, {0, 1}) == 1 && task.state == loom::TaskState::waiting,
          "Task did not enroll on actual Signal");
  }
  void unchanged() const {
    check(task.state == loom::TaskState::waiting && calls == 1 && !signal.generation &&
          signal.state == orbital::Readiness::pending && signal.waiters == &task.wait,
          "memory hints implied Signal or Task readiness");
  }
  ~Waiting() { loom::detach(task); }
};

void native_checks(Fixture& fixture) {
  Native::Ring ring;
  check(!ring.refresh_hints(), "unconfigured snapshot refreshed");
  check(ring.configure(fixture.worker, {2, 4}), "native ring configure");
  PackedInput input(fixture.worker, 23);
  Waiting waiting(fixture.worker);
  std::array<Frame, 7> frames;
  constexpr std::array<std::uint64_t, 7> steps{1, 4, 7, 3, 6, 5, 2};
  constexpr std::array<unsigned, 7> codes{0, 2, 9, 12, 13, 15, 7};
  for (unsigned i = 0; i < frames.size(); ++i) {
    const auto code = codes[i];
    const auto demand = require(loom::bind_demand(
        {code < 8 ? loom::MemoryPattern::irregular : loom::MemoryPattern::linear,
         loom::demand_extents[code % 8]}, PackedInput::stride), "mixed native demand");
    frames[i] = {&input, demand, i * 19 + 3, i, steps[i]};
    check(ring.hint_policy().codes[demand.code()] != loom::Hint::none, "initial admitted hint was empty");
    if (i < 6) check(Native::enqueue(ring, frames[i]), "mixed native admission");
  }
  check(!Native::enqueue(ring, frames[6]), "full hinted ring accepted extra work");
  check(!ring.close() && ring.pending() == 6 && !ring.spin({0}), "live close or zero-budget mutation");
  waiting.unchanged();
  check(ring.spin({3}) == 3 && ring.pending() == 5 && ring.counters().promoted == 1,
        "initial retirement/promotion or bounded progress");
  const auto old = ring.hint_policy();
  auto profile = fixture.worker.hints.profile;
  ++profile.revision; profile.preferred_width = 4; profile.training_accesses = 4;
  check(loom::set_hint_profile(fixture.worker, profile), "coarse profile installation");
  check(loom::set_hint_pressure(fixture.worker, loom::HintPressure::busy), "coarse pressure installation");
  check(ring.hint_policy().profile.revision == old.profile.revision &&
        ring.hint_policy().pressure == old.pressure, "Worker update mutated live Ring snapshot");
  check(ring.refresh_hints(), "pending Ring policy refresh");
  check(ring.hint_policy().profile.revision == profile.revision &&
        ring.hint_policy().pressure == loom::HintPressure::busy && ring.pending() == 5,
        "returned-boundary adoption changed pending frames");
  check(ring.spin({5}) == 5 && ring.pending(), "partial busy-policy progress");
  ++profile.revision; profile.training_accesses = 2;
  check(loom::set_hint_profile(fixture.worker, profile), "second profile installation");
  check(loom::set_hint_pressure(fixture.worker, loom::HintPressure::low), "restore low pressure");
  check(ring.refresh_hints(), "second pending Ring refresh");
  unsigned rounds = 0;
  while (ring.pending()) {
    check(++rounds < 64, "native drain did not terminate");
    check(ring.spin({1 + rounds % 3}) > 0, "native drain made no progress");
  }
  for (unsigned i = 0; i < 6; ++i) output(frames[i]);
  check(!frames[6].done && !frames[6].visits, "refused borrower ran");
  check(ring.counters().accepted == 6 && ring.counters().completed == 6 &&
        ring.counters().refused == 1 && ring.counters().promoted == 4 &&
        ring.counters().visits == 26, "independent mixed-work counts");
  waiting.unchanged();
  check(ring.close(), "mixed native close");

  const auto alternate_page = fixture.worker.hints.profile.geometry.page_bytes * 2;
  check(!ring.configure(fixture.worker, {1, 0}, alternate_page) && !ring.configured(),
        "mismatched Ring geometry published configured storage");
  check(ring.configure(fixture.worker, {1, 0}, fixture.worker.hints.profile.geometry.page_bytes),
        "matching explicit Ring page geometry");
  check(ring.refresh_hints(), "matching explicit geometry refresh");
  check(ring.close(), "explicit geometry Ring close");
}

void cancellation_lifetime(Fixture& fixture) {
  Native::Ring ring;
  check(ring.configure(fixture.worker, {2, 4}), "cancel ring configure");
  auto doomed = std::make_unique<PackedInput>(fixture.worker, 7);
  PackedInput survivor(fixture.worker, 31);
  const auto demand = require(loom::bind_demand({loom::MemoryPattern::linear, 65536},
      PackedInput::stride), "cancel demand admission");
  std::array<Frame, 6> frames;
  for (unsigned i = 0; i < frames.size(); ++i) {
    frames[i] = {i & 1 ? &survivor : doomed.get(), demand, i + 9, i, 9};
    check(Native::enqueue(ring, frames[i]), "cancel admission");
  }
  check(ring.spin({3}) == 3, "cancel prefix");
  check(ring.release_if([owner = doomed.get()](const Native::Ring::Arguments& arguments) noexcept {
    return std::get<0>(arguments)->input == owner;
  }) == 3 && ring.pending() == 3, "selective removal of hinted borrowers");
  for (unsigned i = 0; i < frames.size(); i += 2)
    check(!frames[i].done, "removed borrower completed");
  doomed.reset();
  while (ring.pending()) check(ring.spin({2}) > 0, "surviving hinted work made no progress");
  for (unsigned i = 1; i < frames.size(); i += 2) output(frames[i]);
  for (unsigned i = 0; i < frames.size(); ++i) {
    frames[i] = {&survivor, demand, i + 19, i, 9};
    check(Native::enqueue(ring, frames[i]), "hinted discard admission");
  }
  check(ring.spin({3}) == 3 && ring.discard() == 6 && !ring.pending(), "hinted discard exact pending");
  for (unsigned i = 0; i < frames.size(); ++i) check(!frames[i].done, "discard published abandoned result");
  // Reuse after borrowed input destruction must use only the new stable owner.
  frames[0] = {&survivor, demand, 91, 15, 4};
  check(Native::enqueue(ring, frames[0]), "hinted reuse after discard");
  check(ring.spin({99}) == 4, "hinted reuse progress"); output(frames[0]);
  check(ring.counters().accepted == 13 && ring.counters().completed == 4 &&
        ring.counters().abandoned == 9 && ring.counters().visits == 36 && ring.close(),
        "hinted discard/retirement counts");
}

void gaps_and_activity(Fixture& fixture) {
  const auto page = std::size_t(::sysconf(_SC_PAGESIZE));
  check(page >= 64, "system page geometry");
  auto* mapping = static_cast<std::byte*>(::mmap(nullptr, page * 3, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  check(mapping != MAP_FAILED, "guarded mapping");
  check(::mprotect(mapping + page, page, PROT_NONE) == 0, "guarded gap");
  auto* first = reinterpret_cast<std::uint8_t*>(mapping + page - 64);
  auto* last = reinterpret_cast<std::uint8_t*>(mapping + 2 * page);
  for (unsigned i = 0; i < 64; ++i) { first[i] = i + 17; last[i] = i + 93; }
  const auto low = loom::make_hint_policy(fixture.worker.hints.profile, loom::HintPressure::low);
  for (auto* source : {first, last}) {
    const auto dependency = require(loom::bind_dependency({loom::MemoryPattern::linear, 65536},
        {reinterpret_cast<const std::byte*>(source), 64}), "gapped short-window binding");
    check(dependency.demand.code() == 8 && dependency.demand.minimum_bytes() == 1,
          "gap became a contiguous dependency");
    targets(low.codes[dependency.demand.code()], reinterpret_cast<std::uintptr_t>(source),
        low.profile.geometry, [&](std::uintptr_t target) {
      check(target - reinterpret_cast<std::uintptr_t>(source) < 64, "hint entered protected gap");
    });
    loom::prefetch(low, dependency.demand, dependency.address);
  }
  // Arbitrary row address callback proves the inactive coordinate is never
  // formed. The protected middle page would expose an accidental wide read.
  const auto layout = require(tp::layout::make(64,
      std::array<tp::code, 2>{tp::code{0, 0, 8}, tp::code{63, 0, 8}}), "gapped layout");
  const auto prepared = require(tp::read_plan<64, 2>::make(layout,
      std::array<std::uint8_t, 3>{0, tp::hole, 1}), "gapped packet plan");
  for (const auto active : {0u, 1u, 2u, 3u}) {
    unsigned formed = 0;
    const auto decoded = tp::native::read_body<2>(prepared.controls(), [&](unsigned row) {
      check(active & (1u << row), "inactive payload address formed");
      formed |= 1u << row;
      return row ? last : first;
    }, active, 0);
    check(formed == active, "active payload frontier lost");
    const auto expected = prepared.get_unchecked(reinterpret_cast<const std::byte*>(first), std::size_t(last - first), active);
    std::array<std::uint8_t, 64> actual;
    tp::native::store_packet(actual.data(), decoded);
    check(actual == expected, "gapped packet mask/hole semantics changed");
  }
  check(::munmap(mapping, page * 3) == 0, "guarded mapping release");
}

void realigned_entry(Fixture& fixture) {
  Native::Ring ring;
  PackedInput input(fixture.worker, 11);
  check(ring.configure(fixture.worker, {1, 0}), "realigned hinted ring");
  const auto demand = require(loom::bind_demand({loom::MemoryPattern::linear, 8192},
      PackedInput::stride), "realigned dependency");
  Frame frame{&input, demand, 13, 14, 5};
  alignas(512) volatile std::uint64_t canary[64];
  for (unsigned i = 0; i < 64; ++i) canary[i] = i * 31 + 17;
  check(Native::enqueue(ring, frame, true), "realigned hinted enqueue");
  while (ring.pending()) ring.spin({1});
  for (unsigned i = 0; i < 64; ++i) check(canary[i] == i * 31 + 17, "realigned caller frame changed");
  output(frame);
  check(ring.close(), "realigned hinted close");
}
#endif
} // namespace

int main() {
  admission_and_envelopes();
  Fixture fixture;
  profile_validation(fixture);
#if defined(__aarch64__) || defined(__AVX2__)
  native_checks(fixture);
  cancellation_lifetime(fixture);
  gaps_and_activity(fixture);
  realigned_entry(fixture);
#else
  std::cout << "loom_hint_check: scalar admission only; vector target ISA unavailable\n";
#endif
  std::cout << "loom_hint_check: passed\n";
}
