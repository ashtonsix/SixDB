#include <loom/ring.h>
#include <charconv>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <time.h>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::uint64_t now_ns() {
  timespec value{};
  clock_gettime(CLOCK_MONOTONIC, &value);
  return std::uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}

struct Options {
  std::string_view variant{"ring"}, hint{"none"};
  std::uint64_t jobs{16}, active{16}, steps{262144}, quantum{128}, repeat{};
  int cpu{-1};
  bool vectors{};

  Options(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
      const std::string_view key(argv[i]);
      if (key == "--vectors") { vectors = true; continue; }
      require(i + 1 < argc, "missing option value");
      const std::string_view value(argv[++i]);
      if (key == "--variant") { variant = value; continue; }
      if (key == "--hint") { hint = value; continue; }
      std::uint64_t number{};
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
      require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(), "invalid option number");
      if (key == "--jobs") jobs = number;
      else if (key == "--active") active = number;
      else if (key == "--steps") steps = number;
      else if (key == "--visits") quantum = number;
      else if (key == "--repeat") repeat = number;
      else if (key == "--cpu") {
        require(number <= std::uint64_t(std::numeric_limits<int>::max()), "CPU number overflow");
        cpu = static_cast<int>(number);
      } else require(false, "unknown option");
    }
    require(variant == "direct" || variant == "manual" || variant == "ring", "unknown variant");
    require(hint == "none" || hint == "first", "unknown hint");
    require(jobs && active && steps && quantum && jobs <= active && active <= 65536,
            "positive finite full/underfilled geometry required; no refill in this probe");
    require(steps <= 1000000000 / jobs, "one-billion-visit diagnostic bound exceeded");
  }
};

struct Fixture {
  loom::Worker worker;

  Fixture(int cpu) {
    require(loom::adopt(worker, cpu).has_value(), "adopt lane");
    auto arena = orbital::allocation::open_arena(32 * 1024 * 1024);
    require(arena.has_value(), "open arena");
    require(loom::configure(worker, std::move(*arena), 8).has_value(), "configure worker");
  }

  ~Fixture() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto progress = loom::shutdown(worker);
      if (progress.kind() == orbital::Progress::Kind::done) break;
      if (progress.kind() == orbital::Progress::Kind::failed || std::chrono::steady_clock::now() >= deadline)
        std::abort();
      loom::poll(worker, {256, 0});
      std::this_thread::yield();
    }
    loom::relinquish(worker);
  }
};

template<class T> struct Storage {
  orbital::allocation::Allocation allocation;
  orbital::allocation::Hold use;
  T* data{};

  Storage(loom::Worker& worker, std::size_t count) {
    auto value = loom::allocate(worker, {.bytes = count * sizeof(T), .alignment = std::max(std::size_t(64), alignof(T))});
    require(value.has_value(), "allocate probe bytes");
    allocation = std::move(*value);
    auto held = loom::hold(allocation, {0, count * sizeof(T)});
    require(held.has_value(), "hold probe bytes");
    use = std::move(*held);
    data = reinterpret_cast<T*>(loom::bytes(use).data());
    for (std::size_t i = 0; i < count; ++i) std::construct_at(data + i);
  }
};

struct alignas(64) Node {
  std::uint64_t next{}, value{};
  std::uint64_t padding[6]{};
};

struct Job { const Node* nodes{}; std::uint64_t output{}, completed{}; };

using Vector = std::uint64_t __attribute__((vector_size(16)));
using ScalarRing = loom::Ring<std::uint64_t, std::uint64_t, std::uint64_t, Job*>;
using VectorRing = loom::Ring<std::uint64_t, std::uint64_t, std::uint64_t, Job*, Vector, Vector>;
template<bool Vectors> struct Values;

template<> struct Values<false> { std::uint64_t cursor{}, sum{}; };

template<> struct Values<true> { std::uint64_t cursor{}, sum{}; Vector x{1, 2}, y{3, 4}; };

template<loom::Hint Kind, bool Alternate, bool Vectors>
[[gnu::always_inline]] inline void useful(const Node* nodes, Values<Vectors>& values) noexcept {
  const auto& node = nodes[values.cursor];
  const auto value = Alternate ? node.value ^ 0x33 : node.value;
  values.sum += value;
  values.cursor = node.next;
  if constexpr (Vectors) { values.x += Vector{value, value ^ 0x5a5a}; values.y ^= values.x; }
  loom::prefetch<Kind>(nodes + values.cursor);
}

template<bool Vectors> std::uint64_t result(const Values<Vectors>& values) {
  if constexpr (Vectors) return values.sum + values.x[0] + values.x[1] + values.y[0] + values.y[1];
  else return values.sum;
}

template<loom::Hint Kind, bool Alternate>
LOOM_NATIVE_CC void scalar(ScalarRing* ring, ScalarRing::Slot* slot, std::uint64_t cursor,
    std::uint64_t remaining, std::uint64_t sum, Job* job) {
  Values<false> values{cursor, sum};
  useful<Kind, Alternate>(job->nodes, values);
  if (!--remaining) {
    job->output = values.sum;
    ++job->completed;
    ring->complete(*slot);
  }
  [[clang::musttail]] return ScalarRing::yield(ring, slot, values.cursor, remaining, values.sum, job);
}

template<loom::Hint Kind, bool Alternate>
LOOM_NATIVE_CC void vectors(VectorRing* ring, VectorRing::Slot* slot, std::uint64_t cursor,
    std::uint64_t remaining, std::uint64_t sum, Job* job, Vector x, Vector y) {
  Values<true> values{cursor, sum, x, y};
  useful<Kind, Alternate>(job->nodes, values);
  if (!--remaining) {
    job->output = result(values);
    ++job->completed;
    ring->complete(*slot);
  }
  [[clang::musttail]] return VectorRing::yield(ring, slot, values.cursor, remaining, values.sum, job, values.x, values.y);
}

template<loom::Hint Kind, bool Alternate, bool Vectors>
void direct(const Node* nodes, Values<Vectors>& values, std::uint64_t steps) {
  loom::prefetch<Kind>(nodes + values.cursor);
  for (std::uint64_t i = 0; i < steps; ++i) useful<Kind, Alternate>(nodes, values);
}

std::uint64_t expected(std::uint64_t start, std::uint64_t steps, bool alternate, bool vectors) {
  std::uint64_t total = 0, x0 = 1, x1 = 2, y0 = 3, y1 = 4;
  for (std::uint64_t i = 0; i < steps; ++i) {
    const auto plain = start * 73 + 19;
    const auto value = alternate ? plain ^ 0x33 : plain;
    total += value;
    x0 += value; x1 += value ^ 0x5a5a; y0 ^= x0; y1 ^= x1;
    start = (start * 5 + 3) & 127;
  }
  return total + (vectors ? x0 + x1 + y0 + y1 : 0);
}

template<loom::Hint Kind, bool Vectors>
void run(const Options& options) {
  Fixture fixture(options.cpu);
  Storage<Node> input(fixture.worker, 128);
  Storage<Job> jobs(fixture.worker, options.jobs);
  Storage<Values<Vectors>> values(fixture.worker, options.jobs);
  for (unsigned i = 0; i < 128; ++i) input.data[i] = {(i * 5 + 3) & 127, i * 73 + 19, {}};
  for (std::size_t i = 0; i < options.jobs; ++i) {
    jobs.data[i].nodes = input.data;
    values.data[i].cursor = (i * 13) & 127;
  }
  using Ring = std::conditional_t<Vectors, VectorRing, ScalarRing>;
  Ring ring;
  if (options.variant == "ring") require(ring.configure(fixture.worker, {options.active, 0}).has_value(), "configure Ring");
  std::uint64_t spins = 0;
  const auto begin = now_ns();
  if (options.variant == "ring") {
    for (std::size_t i = 0; i < options.jobs; ++i) {
      const auto cursor = values.data[i].cursor;
      // Owner proof: every selected Node is 64 B in the held 8 KiB array;
      // this probe uses only none/first-line classes and no page geometry.
      const loom::BoundDependency dependency{input.data + cursor, Kind};
      if constexpr (Vectors) {
        require(ring.push(i & 1 ? vectors<Kind, true> : vectors<Kind, false>,
                         {cursor, options.steps, 0, jobs.data + i, Vector{1, 2}, Vector{3, 4}}, dependency),
                "bounded probe enqueue refused");
      } else {
        require(ring.push(i & 1 ? scalar<Kind, true> : scalar<Kind, false>,
                         {cursor, options.steps, 0, jobs.data + i}, dependency), "bounded probe enqueue refused");
      }
    }
    while (ring.pending()) { ring.spin({options.quantum}); ++spins; }
  } else if (options.variant == "direct") {
    for (std::size_t i = 0; i < options.jobs; i += 2) {
      direct<Kind, false>(input.data, values.data[i], options.steps);
      jobs.data[i].output = result(values.data[i]); ++jobs.data[i].completed;
      if (i + 1 < options.jobs) {
        direct<Kind, true>(input.data, values.data[i + 1], options.steps);
        jobs.data[i + 1].output = result(values.data[i + 1]); ++jobs.data[i + 1].completed;
      }
    }
  } else {
    for (std::size_t i = 0; i < options.jobs; ++i) loom::prefetch<Kind>(input.data + values.data[i].cursor);
    for (std::uint64_t step = 0; step < options.steps; ++step) {
      for (std::size_t i = 0; i < options.jobs; i += 2) {
        useful<Kind, false>(input.data, values.data[i]);
        if (i + 1 < options.jobs) useful<Kind, true>(input.data, values.data[i + 1]);
      }
    }
    for (std::size_t i = 0; i < options.jobs; ++i) {
      jobs.data[i].output = result(values.data[i]); ++jobs.data[i].completed;
    }
  }
  const auto elapsed = now_ns() - begin;
  std::uint64_t checksum = 0;
  // Oracle follows timing; setup intentionally populated this tiny warm input.
  for (std::size_t i = 0; i < options.jobs; ++i) {
    require(jobs.data[i].completed == 1 && jobs.data[i].output ==
        expected((i * 13) & 127, options.steps, i & 1, Vectors), "independent result/once oracle");
    checksum += jobs.data[i].output;
  }
  if (options.variant == "ring") require(ring.counters().accepted == options.jobs &&
      ring.counters().completed == options.jobs && ring.counters().visits == options.jobs * options.steps,
      "Ring work accounting");
  const auto scheduler_bytes = options.variant == "ring" ? ring.storage_bytes() : 0;
  const auto visits = options.jobs * options.steps;
  std::cout << "{\"family\":\"loom-ring\",\"variant\":\"" << options.variant
      << "\",\"unit\":\"native visit\",\"repeat\":" << options.repeat
      << ",\"elapsed_ns\":" << elapsed << ",\"items\":" << visits << ",\"verified\":true,"
      << "\"parameters\":{\"hint\":\"" << options.hint << "\",\"jobs\":\"" << options.jobs
      << "\",\"active\":\"" << options.active << "\",\"steps\":\"" << options.steps
      << "\",\"quantum\":\"" << options.quantum << "\",\"vectors\":\"" << (Vectors ? 2 : 0)
      << "\",\"calling_convention\":\"preserve_none\",\"input\":\"warm 8 KiB,128 dependent64B nodes\","
      << "\"timed_scope\":\"whole admission/native work/retirement; setup and oracle excluded\"},"
      << "\"counters\":{\"visits\":" << visits << ",\"completed\":" << options.jobs
      << ",\"spins\":" << spins << ",\"scheduler_bytes\":" << scheduler_bytes
      << ",\"qualified_bytes\":8192,\"job_bytes\":" << options.jobs * sizeof(Job)
      << ",\"control_state_bytes\":" << options.jobs * sizeof(Values<Vectors>)
      << ",\"hints\":" << (Kind == loom::Hint::none ? 0 : visits + options.jobs)
      << ",\"checksum\":" << checksum << "}}\n";
  require(ring.close().has_value(), "explicit Ring close");
}
} // namespace

int main(int argc, char** argv) {
  try {
    const Options options(argc, argv);
    if (options.vectors) {
      if (options.hint == "first") run<loom::Hint::l1_1, true>(options);
      else run<loom::Hint::none, true>(options);
    } else {
      if (options.hint == "first") run<loom::Hint::l1_1, false>(options);
      else run<loom::Hint::none, false>(options);
    }
  } catch (const std::exception& error) {
    std::cerr << "loom_ring_probe: " << error.what() << '\n'; return 1;
  }
}
