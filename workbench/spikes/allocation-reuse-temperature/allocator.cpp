// Same deterministic caller trace against separately captured Orbital libraries.
// Samples time whole blocks; their ns/op values are not individual-op percentiles.
#include <loom/worker.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sched.h>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <vector>

namespace counters {
std::atomic<std::uint64_t> mappings{}, unmaps{}, protections{};
}

// Count actual VM calls, including helper work. This instrumentation only runs
// on those syscalls; ordinary ready reuse has no counter access.
extern "C" {
void* __real_mmap(void*, std::size_t, int, int, int, off_t);
int __real_munmap(void*, std::size_t);
int __real_mprotect(void*, std::size_t, int);

void* __wrap_mmap(void* address, std::size_t size, int protection, int flags, int fd, off_t offset) {
  counters::mappings.fetch_add(1, std::memory_order_relaxed);
  return __real_mmap(address, size, protection, flags, fd, offset);
}

int __wrap_munmap(void* address, std::size_t size) {
  counters::unmaps.fetch_add(1, std::memory_order_relaxed);
  return __real_munmap(address, size);
}

int __wrap_mprotect(void* address, std::size_t size, int protection) {
  counters::protections.fetch_add(1, std::memory_order_relaxed);
  return __real_mprotect(address, size, protection);
}
}

namespace {
namespace alloc = orbital::allocation;
using Temperature = alloc::Temperature;
using Clock = std::chrono::steady_clock;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "allocator: %s\n", message);
  std::abort();
}

template<class T> T take(orbital::Result<T> value) {
  if (!value) {
    std::fprintf(stderr, "allocator: failed operation, error %d\n", int(value.error()));
    std::abort();
  }
  return std::move(*value);
}

struct Options {
  std::string_view workload{"all"};
  std::size_t operations{65536}, slots{256}, rounds{20}, warmup{3};
  std::size_t touch_bytes{64}; // Zero means the whole requested payload.
  int cpu{-1};
};

Options options(int argc, char** argv) {
  Options result;
  for (int i = 1; i < argc; ++i) {
    const std::string_view key{argv[i]};
    check(i + 1 < argc, "each option needs a value");
    const std::string_view value{argv[++i]};
    if (key == "--workload" || key == "--workloads") { result.workload = value; continue; }
    if (key == "--touch-bytes" && value == "full") { result.touch_bytes = 0; continue; }
    char* end{};
    const auto number = std::strtoull(argv[i], &end, 10);
    check(end == argv[i] + value.size() && !value.empty(), "invalid numeric option");
    if (key == "--cpu") result.cpu = int(number);
    else if (key == "--operations") result.operations = number;
    else if (key == "--slots") result.slots = number;
    else if (key == "--rounds") result.rounds = number;
    else if (key == "--warmup") result.warmup = number;
    else if (key == "--touch-bytes") result.touch_bytes = number;
    else check(false, "unknown option");
  }
  check(result.operations && result.slots && result.rounds && result.slots <= 65536,
      "nonzero operations/slots/rounds required; slots must be <=65536");
  check(result.touch_bytes == 0 || result.touch_bytes == 64, "touch bytes must be 64 or full (0)");
  if (result.cpu < 0) {
    cpu_set_t allowed;
    check(!::sched_getaffinity(0, sizeof(allowed), &allowed), "cannot read CPU affinity");
    for (int i = 0; i < CPU_SETSIZE; ++i)
      if (CPU_ISSET(i, &allowed)) { result.cpu = i; break; }
  }
  check(result.cpu >= 0 && result.cpu < CPU_SETSIZE, "invalid CPU");
  return result;
}

enum class Workload { hot, warm, batch, contamination, hints, sizes, delayed, finite,
    fallback, locality };
constexpr std::array cases{
  std::pair{"hot-loop", Workload::hot}, std::pair{"warm-loop", Workload::warm},
  std::pair{"batch-hot", Workload::batch}, std::pair{"cold-contamination", Workload::contamination},
  std::pair{"mixed-hints", Workload::hints}, std::pair{"mixed-sizes", Workload::sizes},
  std::pair{"delayed-reuse", Workload::delayed}, std::pair{"finite-control", Workload::finite},
  std::pair{"finite-fallback", Workload::fallback}, std::pair{"window-locality", Workload::locality}};

Temperature mixed_hint(std::size_t index) {
  const auto n = index % 20;
  return n < 15 ? Temperature::hot : n < 19 ? Temperature::warm : Temperature::cold;
}

std::size_t request_bytes(Workload workload, std::size_t index) {
  if (workload == Workload::finite) return 256;
  if (workload == Workload::fallback) {
    // Distinct palette-B classes including the 64-byte readable padding. Eight
    // rotating classes cannot all remain in the four-entry ready-candidate cache.
    constexpr std::array<std::size_t, 8> sizes{192, 320, 448, 704, 960, 1472, 1984, 3008};
    return sizes[index % sizes.size()];
  }
  if (workload == Workload::locality) {
    // Hot requests have short size-local bursts; warm/cold requests interrupt
    // them with larger extents. Phase rotation changes which size is hot.
    constexpr std::array<std::size_t, 4> sizes{6304, 16320, 32704, 65472};
    return sizes[(index / 80 + (index % 20 < 15 ? 0 : index % 20 - 14)) % sizes.size()];
  }
  if (workload == Workload::sizes) {
    constexpr std::array<std::size_t, 3> sizes{6304, 16320, 65472};
    return sizes[index % sizes.size()];
  }
  return 6304;
}

// The 64-byte mode preserves the original one-line trace and checksum. Full
// mode writes and reads every payload word, including the final partial line.
// Volatile accesses keep the real caller data work in the timed block.
std::uint64_t touch(alloc::Allocation& value, std::uint64_t seed, std::size_t bytes) {
  auto* words = reinterpret_cast<volatile std::uint64_t*>(value.address);
  const auto count = bytes / sizeof(std::uint64_t);
  for (std::size_t i = 0; i < count; ++i) words[i] = seed + i;
  if (bytes == 64) return words[0] ^ words[7];
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < count; ++i) sum += words[i];
  return sum;
}

// A closed-form expectation independent of the memory read loop.
std::uint64_t expected_touch(std::uint64_t seed, std::size_t bytes) {
  if (bytes == 64) return seed ^ (seed + 7);
  const auto count = std::uint64_t(bytes / sizeof(std::uint64_t));
  return count * seed + count * (count - 1) / 2;
}

struct Run {
  const Options& config;
  Workload workload;
  loom::Worker worker;
  orbital::Ref<alloc::Arena> arena;
  std::vector<alloc::Allocation> live;
  std::vector<std::uint64_t> live_seeds;
  std::vector<alloc::Allocation> anchors;
  std::vector<std::uint64_t> anchor_seeds;
  std::uint64_t sequence{}, checksum{}, expected{};

  Run(const Options& config, Workload workload) : config(config), workload(workload),
      arena(take(alloc::open_arena(std::uint64_t(config.slots) * 65536 * 4 + (64 << 20)))),
      live(config.slots), live_seeds(config.slots) {
    check(loom::adopt(worker, config.cpu).has_value(), "Worker adoption failed");
    check(loom::configure(worker, arena, 0).has_value(), "Worker configuration failed");
    if (workload == Workload::fallback) { seed_fallback(); return; }
    // Concurrent live uses ensure these are distinct physical reservations.
    // Seed HOT before WARM/COLD, so a shared newest-first list hides HOT entries.
    std::vector<std::uintptr_t> addresses;
    addresses.reserve(live.size());
    for (std::size_t i = 0; i < live.size(); ++i) {
      live[i] = acquire(request_bytes(workload, i));
      live_seeds[i] = i + 1;
      check(touch(live[i], i + 1, touched(live[i])) == expected_touch(i + 1, touched(live[i])),
          "setup data differs");
      addresses.push_back(reinterpret_cast<std::uintptr_t>(live[i].address));
    }
    std::sort(addresses.begin(), addresses.end());
    check(std::adjacent_find(addresses.begin(), addresses.end()) == addresses.end(),
        "distinct live allocations alias");
    for (std::size_t i = 0; i < live.size(); ++i) {
      if (workload == Workload::locality && i < retained_windows()) continue;
      auto temperature = workload == Workload::warm ? Temperature::warm : Temperature::hot;
      if (workload == Workload::contamination)
        temperature = i < std::min<std::size_t>(16, config.slots) ? Temperature::hot : Temperature::cold;
      if (workload == Workload::hints || workload == Workload::sizes || workload == Workload::locality)
        temperature = i < config.slots / 2 ? Temperature::hot :
            i < config.slots * 3 / 4 ? Temperature::warm : Temperature::cold;
      loom::release(live[i], {.temperature = temperature});
    }
    loom::poll(worker, {64, 0});
  }

  alloc::Allocation acquire(std::size_t size) {
    return take(loom::allocate(worker, {.bytes = size, .alignment = 64,
        .readable_padding = 64}));
  }

  std::size_t touched(const alloc::Allocation& value) const {
    const auto bytes = config.touch_bytes ? config.touch_bytes : value.size;
    check(bytes <= value.size && bytes % sizeof(std::uint64_t) == 0, "invalid payload width");
    return bytes;
  }

  std::size_t retained_windows() const { return std::max<std::size_t>(1, config.slots / 4); }

  void seed_fallback() {
    // Fill at least three Groups, then leave two thirds of every Group occupied.
    // Released holes carry different Group hints; mapped classes coexist with
    // untouched pages and permanently live Homes throughout the measurement.
    const auto count = 3 * std::max<std::size_t>(config.slots, 256);
    std::vector<alloc::Allocation> seed(count);
    std::vector<std::uintptr_t> addresses;
    addresses.reserve(count);
    anchors.reserve(count * 2 / 3);
    anchor_seeds.reserve(count * 2 / 3);
    for (std::size_t i = 0; i < count; ++i) {
      seed[i] = acquire(request_bytes(workload, i));
      check(touch(seed[i], i + 1, touched(seed[i])) == expected_touch(i + 1, touched(seed[i])),
          "finite setup data differs");
      addresses.push_back(reinterpret_cast<std::uintptr_t>(seed[i].address));
    }
    std::sort(addresses.begin(), addresses.end());
    check(std::adjacent_find(addresses.begin(), addresses.end()) == addresses.end(),
        "distinct finite setup allocations alias");
    constexpr std::array temperatures{Temperature::hot, Temperature::warm, Temperature::cold};
    for (std::size_t i = 0; i < count; ++i) {
      if (i % 3) {
        anchors.push_back(std::move(seed[i])); anchor_seeds.push_back(i + 1);
      } else loom::release(seed[i], {.temperature = temperatures[(i / 256) % 3]});
    }
    loom::poll(worker, {64, 0});
  }

  void data(alloc::Allocation& value) {
    const auto seed = ++sequence * 0x9e3779b97f4a7c15ULL;
    checksum ^= touch(value, seed, touched(value));
  }

  void verify_retained() const {
    const auto verify = [&](const alloc::Allocation& value, std::uint64_t seed) {
      const auto* words = reinterpret_cast<const volatile std::uint64_t*>(value.address);
      for (std::size_t i = 0; i < touched(value) / sizeof(std::uint64_t); ++i)
        check(words[i] == seed + i, "retained live payload was overwritten");
    };
    for (std::size_t i = 0; i < anchors.size(); ++i) verify(anchors[i], anchor_seeds[i]);
    if (workload == Workload::locality)
      for (std::size_t i = 0; i < retained_windows(); ++i) verify(live[i], live_seeds[i]);
  }

  void verify_block(std::uint64_t first) {
    // Keep reference calculation and retained-live verification untimed.
    for (std::size_t i = 0; i < config.operations; ++i) {
      const auto bytes = config.touch_bytes ? config.touch_bytes : request_bytes(workload, first + i);
      expected ^= expected_touch((first + i + 1) * 0x9e3779b97f4a7c15ULL, bytes);
    }
    check(checksum == expected, "caller payload checksum differs");
    verify_retained();
  }

  void batch(std::size_t count, Temperature temperature, bool varied) {
    const auto first = sequence;
    for (std::size_t i = 0; i < count; ++i) {
      live[i] = acquire(request_bytes(workload, first + i));
      data(live[i]);
    }
    for (std::size_t i = 0; i < count; ++i)
      loom::release(live[i], {.temperature = varied ? mixed_hint(first + i) : temperature});
    // Service physical replies identically in both builds, once per caller batch.
    loom::poll(worker, {64, 0});
  }

  void block() {
    std::size_t remaining = config.operations;
    while (remaining) {
      if (workload == Workload::locality) {
        const auto count = std::min<std::size_t>(remaining, 64);
        for (std::size_t i = 0; i < count; ++i) {
          const auto first = sequence;
          auto value = acquire(request_bytes(workload, first));
          data(value);
          const auto slot = first % retained_windows();
          loom::release(live[slot], {.temperature = mixed_hint(first / 3)});
          live[slot] = std::move(value);
          live_seeds[slot] = sequence * 0x9e3779b97f4a7c15ULL;
        }
        loom::poll(worker, {64, 0}); remaining -= count;
      } else if (workload == Workload::contamination) {
        const auto hot = std::min({remaining, config.slots, std::size_t{16}});
        batch(hot, Temperature::hot, false); remaining -= hot;
        const auto cold = std::min(remaining, config.slots);
        if (cold) { batch(cold, Temperature::cold, false); remaining -= cold; }
      } else if (workload == Workload::hot || workload == Workload::warm) {
        const auto count = std::min<std::size_t>(remaining, 64);
        for (std::size_t i = 0; i < count; ++i) {
          const auto temperature = workload == Workload::warm ? Temperature::warm : Temperature::hot;
          auto value = acquire(6304);
          data(value);
          loom::release(value, {.temperature = temperature});
        }
        loom::poll(worker, {64, 0}); remaining -= count;
      } else {
        const auto count = std::min(remaining, config.slots);
        const bool varied = workload == Workload::hints || workload == Workload::sizes ||
            workload == Workload::fallback;
        batch(count, Temperature::hot, varied); remaining -= count;
      }
    }
  }

  void shutdown() {
    verify_retained();
    for (auto& value : anchors) loom::release(value, {.temperature = Temperature::cold});
    if (workload == Workload::locality)
      for (std::size_t i = 0; i < retained_windows(); ++i)
        loom::release(live[i], {.temperature = Temperature::cold});
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    for (;;) {
      const auto progress = loom::shutdown(worker);
      check(progress.kind() != orbital::Progress::Kind::failed, "shutdown failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(Clock::now() < deadline, "shutdown stalled");
      if (!loom::poll(worker, {256, 0})) std::this_thread::yield();
    }
    check(arena->charged_bytes.load() == 0, "shutdown left charged bytes");
    loom::relinquish(worker);
  }
};

void run(const Options& options, const char* name, Workload workload) {
  Run state(options, workload);
  for (std::size_t i = 0; i < options.warmup; ++i) {
    const auto first = state.sequence;
    state.block(); state.verify_block(first);
  }
  check(state.checksum == state.expected, "warmup data differs");
  for (std::size_t sample = 0; sample < options.rounds; ++sample) {
    const auto before = alloc::statistics(*state.worker.memory);
    const auto maps = counters::mappings.load(std::memory_order_relaxed);
    const auto unmaps = counters::unmaps.load(std::memory_order_relaxed);
    const auto protections = counters::protections.load(std::memory_order_relaxed);
    const auto first = state.sequence;
    const auto begin = Clock::now();
    state.block();
    const auto end = Clock::now();
    const auto after = alloc::statistics(*state.worker.memory);
    state.verify_block(first);
    check(state.checksum == state.expected, "timed data differs");
    const auto allocations = after.allocations - before.allocations;
    const auto reuses = after.reuses - before.reuses;
    check(allocations == options.operations, "allocation accounting differs from trace");
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    std::printf("%s,%zu,%d,%zu,%zu,%zu,%lld,%.4f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        name, sample, options.cpu, options.slots, options.operations, options.touch_bytes, (long long)ns,
        double(ns) / options.operations, (unsigned long long)allocations,
        (unsigned long long)reuses, (unsigned long long)(allocations - reuses),
        (unsigned long long)after.mapped_bytes, (unsigned long long)after.idle_bytes,
        (unsigned long long)(after.trim_pages - before.trim_pages),
        (unsigned long long)(counters::mappings.load(std::memory_order_relaxed) - maps),
        (unsigned long long)(counters::unmaps.load(std::memory_order_relaxed) - unmaps),
        (unsigned long long)(counters::protections.load(std::memory_order_relaxed) - protections),
        (unsigned long long)state.checksum);
  }
  state.shutdown();
  std::fprintf(stderr, "allocator: %s, checksum verified, zero charged bytes after shutdown\n", name);
}
} // namespace

int main(int argc, char** argv) {
  const auto config = options(argc, argv);
  std::puts("workload,sample,cpu,slots,operations,touch_bytes,elapsed_ns,block_mean_ns_per_op,allocations,reuses,new_allocations,mapped_bytes,idle_bytes,trim_pages,mmap_calls,munmap_calls,mprotect_calls,checksum");
  bool selected = false;
  auto remaining = config.workload;
  do {
    const auto delimiter = remaining.find(',');
    const auto selection = remaining.substr(0, delimiter);
    bool known = false;
    for (const auto& [name, workload] : cases)
      if ((selection == "all" && workload != Workload::delayed) || selection == name) {
        run(config, name, workload); selected = known = true;
      }
    check(known, "unknown workload");
    if (delimiter == std::string_view::npos) break;
    remaining.remove_prefix(delimiter + 1);
  } while (true);
  check(selected, "unknown workload");
}
