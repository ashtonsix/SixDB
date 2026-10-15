// Ignored current-API comparison, linked against one captured Orbital variant.
// Timings are whole-block means, not individual allocation latency percentiles.
// Owned peaks come from a separate untimed replay with the same fresh Pool trace.
#include <orbital/allocation.h>
#include <orbital/worker.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <sched.h>
#include <source_location>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace vm {
std::atomic<std::uint64_t> mappings{}, protections{}, unmaps{};
struct Counts { std::uint64_t mappings{}, protections{}, unmaps{}; };
Counts read() noexcept { return {mappings.load(), protections.load(), unmaps.load()}; }
}

extern "C" {
void* __real_mmap(void*, std::size_t, int, int, int, off_t);
int __real_mprotect(void*, std::size_t, int);
int __real_munmap(void*, std::size_t);
void* __wrap_mmap(void* address, std::size_t bytes, int protection, int flags, int fd, off_t offset) {
  vm::mappings.fetch_add(1, std::memory_order_relaxed);
  return __real_mmap(address, bytes, protection, flags, fd, offset);
}
int __wrap_mprotect(void* address, std::size_t bytes, int protection) {
  vm::protections.fetch_add(1, std::memory_order_relaxed);
  return __real_mprotect(address, bytes, protection);
}
int __wrap_munmap(void* address, std::size_t bytes) {
  vm::unmaps.fetch_add(1, std::memory_order_relaxed);
  return __real_munmap(address, bytes);
}
}

namespace {
namespace oa = orbital::allocation;
namespace ow = orbital::worker;
using Clock = std::chrono::steady_clock;
constexpr std::size_t small_bytes = 4096, large_bytes = 65536;
constexpr std::uint64_t multiplier = 0x9e3779b97f4a7c15ULL;

void check(bool condition, const char* message,
    std::source_location where = std::source_location::current()) {
  if (condition) return;
  std::fprintf(stderr, "window-reuse: %s:%u: %s\n", where.file_name(), where.line(), message);
  std::abort();
}

template<class T> T take(orbital::Result<T> result,
    std::source_location where = std::source_location::current()) {
  if (!result) {
    std::fprintf(stderr, "window-reuse: %s:%u: operation refused, error=%u\n",
        where.file_name(), where.line(), unsigned(result.error()));
    std::abort();
  }
  return std::move(*result);
}

struct Options {
  std::string_view traces{"all"};
  std::size_t operations{4096}, rounds{4}, slots{16}, touch_bytes{64};
  int cpu{-1};
  bool qualification_only{};
};

Options parse(int argc, char** argv) {
  Options result;
  for (int i = 1; i < argc; ++i) {
    const std::string_view key{argv[i]};
    if (key == "--check") { result.qualification_only = true; continue; }
    check(++i < argc, "option requires a value");
    const std::string_view value{argv[i]};
    if (key == "--trace" || key == "--workloads") { result.traces = value; continue; }
    if (key == "--touch-bytes" && value == "full") { result.touch_bytes = 0; continue; }
    char* end{};
    const auto number = std::strtoull(argv[i], &end, 10);
    check(!value.empty() && value.front() != '-' && end == argv[i] + value.size(), "invalid integer option");
    if (key == "--cpu") result.cpu = int(number);
    else if (key == "--operations") result.operations = number;
    else if (key == "--rounds") result.rounds = number;
    else if (key == "--slots") result.slots = number;
    else if (key == "--touch-bytes") result.touch_bytes = number;
    else check(false, "unknown option");
  }
  check(result.operations && result.rounds && result.slots && result.slots <= 4096,
      "positive operations/rounds/slots required; slots must be <=4096");
  check(result.touch_bytes == 0 || result.touch_bytes == 64, "touch width must be 64 or full");
  cpu_set_t allowed;
  check(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0, "affinity query failed");
  if (result.cpu < 0)
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
      if (CPU_ISSET(cpu, &allowed)) { result.cpu = cpu; break; }
  check(result.cpu >= 0 && result.cpu < CPU_SETSIZE && CPU_ISSET(result.cpu, &allowed),
      "CPU must be in the allowed affinity set");
  return result;
}

enum class Trace { same, buried, oversized, mixed, simultaneous, tight };
constexpr std::array trace_names{
  std::pair{"same-size", Trace::same}, std::pair{"buried-small", Trace::buried},
  std::pair{"oversized-only", Trace::oversized}, std::pair{"mixed-bursts", Trace::mixed},
  std::pair{"small-then-large", Trace::simultaneous}, std::pair{"tight-oversized-only", Trace::tight}};

std::size_t mixed_bytes(std::uint64_t operation) {
  constexpr std::array sizes{small_bytes, std::size_t{16384}, large_bytes};
  // Mostly size-local bursts, interrupted with another size every eighth request.
  return sizes[(operation / 32 + (operation % 8 == 7)) % sizes.size()];
}

struct Lease { oa::Allocation value; std::uint64_t seed{}; };

std::size_t touched(const Options& options, std::size_t size) {
  return options.touch_bytes ? options.touch_bytes : size;
}

std::uint64_t write_read(const Options& options, Lease& lease) {
  auto* words = reinterpret_cast<volatile std::uint64_t*>(lease.value.address);
  const auto count = touched(options, lease.value.size) / sizeof(std::uint64_t);
  for (std::size_t i = 0; i < count; ++i) words[i] = lease.seed + i;
  std::uint64_t sum{};
  for (std::size_t i = 0; i < count; ++i) sum += words[i];
  return sum;
}

void verify(const Options& options, const Lease& lease) {
  check(bool(lease.value), "missing retained allocation");
  const auto* words = reinterpret_cast<const volatile std::uint64_t*>(lease.value.address);
  const auto count = touched(options, lease.value.size) / sizeof(std::uint64_t);
  for (std::size_t i = 0; i < count; ++i)
    check(words[i] == lease.seed + i, "a retained payload changed");
}

struct Peaks {
  std::uint64_t requested{}, owned{}, slack{};
};

struct Observation {
  struct Record { const std::byte* address; std::size_t size, capacity; };
  std::vector<Record> records;
  std::uint64_t requested{}, owned{};
  Peaks peaks;

  void sample() {
    peaks.requested = std::max(peaks.requested, requested);
    peaks.owned = std::max(peaks.owned, owned);
    peaks.slack = std::max(peaks.slack, owned - requested);
  }

  void add(const Lease& lease) {
    const auto& value = lease.value;
    const auto begin = reinterpret_cast<std::uintptr_t>(value.address);
    check(begin % 64 == 0 && value.size >= 64, "allocation alignment/length is invalid");
    for (const auto& record : records) {
      const auto other = reinterpret_cast<std::uintptr_t>(record.address);
      check(begin + value.size <= other || other + record.size <= begin,
          "distinct live payloads overlap");
    }
    const auto capacity = oa::capacity(value);
    check(capacity >= value.size, "allocation backing is smaller than requested bytes");
    records.push_back({value.address, value.size, capacity});
    requested += value.size; owned += capacity; sample();
  }

  void remove(const Lease& lease) {
    auto selected = std::find_if(records.begin(), records.end(),
        [&](const auto& record) { return record.address == lease.value.address; });
    check(selected != records.end(), "live accounting lost a lease");
    requested -= selected->size; owned -= selected->capacity;
    *selected = records.back(); records.pop_back();
  }

  void reset_peaks() { peaks = {}; sample(); }
};

template<bool Observe> struct Run {
  const Options& options;
  Trace trace;
  orbital::Ref<oa::Arena> arena;
  ow::Lane lane;
  oa::Pool* pool{};
  std::vector<Lease> anchors, live, batch;
  Observation observation;
  std::uint64_t sequence{}, operation_index{}, checksum{}, expected{};
  bool closed{};

  Run(const Options& options, Trace trace) : options(options), trace(trace),
      arena(take(oa::open_arena((128ULL << 20) + std::uint64_t(options.slots) * large_bytes * 4))) {
    check(ow::adopt(lane, options.cpu).has_value(), "Lane adoption failed");
    pool = take(oa::add_pool(*arena, lane));
    anchors.reserve(options.slots); live.reserve(options.slots); batch.resize(options.slots);
    if constexpr (Observe) observation.records.reserve(options.slots * 4 + 4);
    seed();
    // Seeding is outside the measured caller work. The next block starts a new
    // checksum expectation; the observed replay separately verifies live bytes.
    expected = checksum;
    if constexpr (Observe) observation.reset_peaks();
  }

  ~Run() { check(closed, "Run was not closed"); }

  Lease acquire(std::size_t size) {
    Lease lease{take(oa::allocate(*pool, {.bytes = size, .alignment = 64})), ++sequence * multiplier};
    checksum += write_read(options, lease);
    if constexpr (Observe) observation.add(lease);
    return lease;
  }

  void release(Lease& lease) {
    if (!lease.value) return;
    if constexpr (Observe) { verify(options, lease); observation.remove(lease); }
    oa::release(lease.value);
  }

  void seed() {
    if (trace == Trace::mixed) {
      const auto anchor_count = std::max<std::size_t>(1, options.slots / 4);
      for (std::size_t i = 0; i < anchor_count; ++i) anchors.push_back(acquire(large_bytes));
      for (std::size_t i = 0; i < options.slots; ++i) live.push_back(acquire(mixed_bytes(i)));
      for (std::size_t i = 0; i < options.slots; ++i) batch[i] = acquire(mixed_bytes(i * 32));
      for (auto& lease : batch) release(lease);
      return;
    }
    if (trace == Trace::same) {
      auto small = acquire(small_bytes); release(small); return;
    }
    if (trace == Trace::simultaneous) {
      auto small = acquire(small_bytes), large = acquire(large_bytes);
      release(small); release(large); return;
    }
    Lease small;
    if (trace == Trace::buried) small = acquire(small_bytes);
    for (auto& lease : batch) lease = acquire(large_bytes);
    release(small);
    for (auto& lease : batch) release(lease);
    if (trace == Trace::tight) {
      // Fix the experimental budget at already charged inventory before any
      // concurrent preparation/reclamation. Ready storage must remain usable;
      // rejecting it would be an admission regression, not a memory saving.
      arena->byte_limit = arena->charged_bytes.load(std::memory_order_acquire);
    }
  }

  std::size_t block_operations() const {
    if (trace == Trace::buried) {
      const auto width = options.slots + 1;
      return std::max<std::size_t>(1, options.operations / width) * width;
    }
    if (trace == Trace::simultaneous)
      return std::max<std::size_t>(1, options.operations / 2) * 2;
    return options.operations;
  }

  void block() {
    const auto count = block_operations();
    if constexpr (Observe) observation.reset_peaks();
    switch (trace) {
      case Trace::same:
      case Trace::oversized:
      case Trace::tight:
        for (std::size_t i = 0; i < count; ++i) {
          auto lease = acquire(small_bytes); release(lease);
        }
        break;
      case Trace::buried:
        for (std::size_t i = 0; i < count / (options.slots + 1); ++i) {
          auto small = acquire(small_bytes); release(small);
          // Acquiring the entire large inventory and returning it afterwards
          // buries the small candidate again; warmup cannot erase the event.
          for (auto& lease : batch) lease = acquire(large_bytes);
          for (auto& lease : batch) release(lease);
        }
        break;
      case Trace::mixed:
        for (std::size_t i = 0; i < count; ++i) {
          auto next = acquire(mixed_bytes(operation_index + i));
          auto& previous = live[(operation_index + i) % live.size()];
          release(previous); previous = std::move(next);
        }
        break;
      case Trace::simultaneous:
        for (std::size_t i = 0; i < count / 2; ++i) {
          auto small = acquire(small_bytes), large = acquire(large_bytes);
          release(small); release(large);
        }
        break;
    }
    operation_index += count;
  }

  void verify_block(std::uint64_t first_sequence, std::uint64_t first_operation) {
    const auto count = block_operations();
    check(sequence - first_sequence == count, "trace issued the wrong number of requests");
    // Closed-form expectations are computed outside timing and do not read the
    // payload or the allocator's choice/capacity/accounting.
    for (std::size_t i = 0; i < count; ++i) {
      auto size = small_bytes;
      if (trace == Trace::buried && i % (options.slots + 1)) size = large_bytes;
      if (trace == Trace::simultaneous && i % 2) size = large_bytes;
      if (trace == Trace::mixed) size = mixed_bytes(first_operation + i);
      const auto words = std::uint64_t(touched(options, size) / sizeof(std::uint64_t));
      const auto seed = (first_sequence + i + 1) * multiplier;
      expected += words * seed + words * (words - 1) / 2;
    }
    check(checksum == expected, "caller checksum differs from closed-form expectation");
    for (const auto& lease : anchors) verify(options, lease);
    for (const auto& lease : live) verify(options, lease);
  }

  void close() {
    for (auto& lease : anchors) release(lease);
    for (auto& lease : live) release(lease);
    for (auto& lease : batch) release(lease);
    if constexpr (Observe)
      check(observation.records.empty() && observation.requested == 0 && observation.owned == 0,
          "owned live accounting did not drain");
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    for (;;) {
      const auto progress = oa::close(*pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "Pool close failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(Clock::now() < deadline, "Pool close stalled");
      if (!ow::poll(lane, 128)) std::this_thread::yield();
    }
    check(arena->charged_bytes.load(std::memory_order_acquire) == 0, "close retained charged bytes");
    check(!ow::pending(lane), "close left pending Lane messages");
    check(oa::remove_pool(*pool).has_value(), "closed Pool removal failed");
    pool = nullptr; ow::relinquish(lane); closed = true;
  }
};

struct Sample {
  std::uint64_t elapsed{}, operations{}, checksum{};
  oa::Statistics before, after;
  vm::Counts vm_before, vm_after;
};

std::vector<Peaks> replay(const Options& options, Trace trace) {
  Run<true> run(options, trace);
  std::vector<Peaks> peaks; peaks.reserve(options.rounds);
  for (std::size_t sample = 0; sample < options.rounds; ++sample) {
    const auto first_sequence = run.sequence, first_operation = run.operation_index;
    run.block(); run.verify_block(first_sequence, first_operation);
    peaks.push_back(run.observation.peaks);
  }
  run.close();
  return peaks;
}

void measure(const Options& options, const char* name, Trace trace) {
  if (options.qualification_only) {
    (void)replay(options, trace);
    std::fprintf(stderr, "window-reuse: %s qualified; payloads, live ranges and close verified\n", name);
    return;
  }
  std::vector<Sample> samples; samples.reserve(options.rounds);
  {
    Run<false> run(options, trace);
    for (std::size_t sample = 0; sample < options.rounds; ++sample) {
      const auto before = oa::statistics(*run.pool);
      const auto vm_before = vm::read();
      const auto first_sequence = run.sequence, first_operation = run.operation_index;
      const auto begin = Clock::now();
      run.block();
      const auto end = Clock::now();
      const auto vm_after = vm::read();
      const auto after = oa::statistics(*run.pool);
      run.verify_block(first_sequence, first_operation);
      check(after.allocations - before.allocations == run.block_operations(), "allocation counts differ from caller trace");
      samples.push_back({std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()),
          run.block_operations(), run.checksum, before, after, vm_before, vm_after});
    }
    run.close();
  }
  const auto peaks = replay(options, trace);
  const auto page_bytes = ::sysconf(_SC_PAGESIZE);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const auto& s = samples[i];
    const auto allocations = s.after.allocations - s.before.allocations;
    const auto reuses = s.after.reuses - s.before.reuses;
    // No reclamation is requested during these traces: Pool backing, reservation
    // and metadata do not shrink, so the post-block values are also highwaters.
    std::printf("%s,%zu,%d,%ld,%zu,%llu,%zu,%llu,%.4f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        name, i, options.cpu, page_bytes, options.slots,
        (unsigned long long)s.operations, options.touch_bytes,
        (unsigned long long)s.elapsed, double(s.elapsed) / s.operations,
        (unsigned long long)allocations, (unsigned long long)reuses,
        (unsigned long long)(allocations - reuses),
        (unsigned long long)(s.after.growths - s.before.growths),
        (unsigned long long)(s.after.relocations - s.before.relocations),
        (unsigned long long)s.after.mapped_bytes, (unsigned long long)s.after.idle_bytes,
        (unsigned long long)s.after.metadata_bytes, (unsigned long long)s.after.reserved_bytes,
        (unsigned long long)(s.vm_after.mappings - s.vm_before.mappings),
        (unsigned long long)(s.vm_after.protections - s.vm_before.protections),
        (unsigned long long)(s.vm_after.unmaps - s.vm_before.unmaps),
        (unsigned long long)peaks[i].requested, (unsigned long long)peaks[i].owned,
        (unsigned long long)peaks[i].slack, (unsigned long long)s.checksum);
  }
  std::fprintf(stderr, "window-reuse: %s timing and replay verified; zero charged bytes after each close\n", name);
}
} // namespace

int main(int argc, char** argv) {
  const auto options = parse(argc, argv);
  if (!options.qualification_only)
    std::puts("trace,sample,cpu,page_bytes,slots,operations,touch_bytes,elapsed_ns,block_mean_ns_per_op,allocations,reuses,new_allocations,growths,relocations,pool_mapped_peak,pool_idle_after,pool_metadata_peak,pool_reserved_peak,mmap_calls,mprotect_calls,munmap_calls,replay_requested_live_peak,replay_owned_capacity_peak,replay_owned_slack_peak,checksum");
  auto remaining = options.traces;
  do {
    const auto separator = remaining.find(',');
    const auto requested = remaining.substr(0, separator);
    bool found{};
    for (const auto& [name, trace] : trace_names)
      if (requested == "all" || requested == name) { measure(options, name, trace); found = true; }
    check(found, "unknown trace name");
    if (separator == std::string_view::npos) break;
    remaining.remove_prefix(separator + 1);
  } while (true);
}
