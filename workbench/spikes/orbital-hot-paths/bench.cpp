// Whole-loop microbenchmarks over Orbital/Loom owner paths. Each case reports
// median and minimum ns/operation across blocks, plus user-mode instructions
// per operation when the host exposes a PMU. These are mechanism costs, not
// request latencies or complete database operations.
#include <orbital/allocation.h>
#include <orbital/worker.h>
#if SIXDB_HOT_FULL
#include <loom/worker.h>
#define SIXDB_HOT_OBJECTS 1
#endif
#if SIXDB_HOT_OBJECTS
#include "object_material.h"
#include <memory_resource>
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <linux/perf_event.h>
#include <random>
#include <string>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <thread>
#include <sched.h>
#include <unistd.h>
#include <vector>

namespace {
namespace a = orbital::allocation;
namespace w = orbital::worker;
using Clock = std::chrono::steady_clock;

void require(bool ok, const char* why) {
  if (!ok) { std::fprintf(stderr, "failed: %s\n", why); std::abort(); }
}
template<class T> T need(orbital::Result<T> value, const char* why = "refused") {
  require(bool(value), why); return std::move(*value);
}
void done(orbital::Result<void> value, const char* why = "refused") { require(bool(value), why); }

struct Instructions {
  int fd{-1};
  Instructions() {
    perf_event_attr attr{};
    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_INSTRUCTIONS;
    attr.disabled = 1; attr.exclude_kernel = 1; attr.exclude_hv = 1;
    fd = static_cast<int>(::syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
  }
  ~Instructions() { if (fd >= 0) ::close(fd); }
  void start() const { if (fd >= 0) { ::ioctl(fd, PERF_EVENT_IOC_RESET, 0); ::ioctl(fd, PERF_EVENT_IOC_ENABLE, 0); } }
  long long stop() const {
    if (fd < 0) return -1;
    ::ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    long long value{}; require(::read(fd, &value, sizeof value) == sizeof value, "perf read");
    return value;
  }
};
Instructions counter;

int blocks = 9;
int main_cpu = -1, peer_cpus[3] = {-1, -1, -1};
void pin(int cpu) {
  if (cpu < 0) return;
  cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
  require(::sched_setaffinity(0, sizeof set, &set) == 0, "affinity");
}
template<class T> void escape(T* pointer) { asm volatile("" : : "r"(pointer) : "memory"); }
double scale = 1.0;
std::vector<std::string> filters; // Comma-separated substrings; any match selects.
bool selected(const std::string& name) {
  if (filters.empty()) return true;
  for (const auto& f : filters) if (name.find(f) != std::string::npos) return true;
  return false;
}

// body(n) performs n operations; it returns nothing and owns its own setup.
void measure(const std::string& name, std::size_t operations,
    const std::function<void(std::size_t)>& body) {
  if (!selected(name)) return;
  operations = std::max<std::size_t>(1, static_cast<std::size_t>(operations * scale));
  body(operations / 8 + 1); // warm
  std::vector<double> ns;
  std::vector<double> instructions;
  for (int block = 0; block < blocks; ++block) {
    counter.start();
    const auto start = Clock::now();
    body(operations);
    const auto stop = Clock::now();
    const auto count = counter.stop();
    ns.push_back(std::chrono::duration<double, std::nano>(stop - start).count() / operations);
    if (count >= 0) instructions.push_back(double(count) / operations);
  }
  std::sort(ns.begin(), ns.end());
  std::sort(instructions.begin(), instructions.end());
  std::printf("%s,%zu,%.3f,%.3f,%.3f,%.1f\n", name.c_str(), operations, ns[ns.size() / 2],
      ns.front(), ns.back(), instructions.empty() ? -1.0 : instructions[instructions.size() / 2]);
  std::fflush(stdout);
}

struct Owner {
  orbital::Ref<a::Arena> arena;
  w::Lane lane;
  a::Pool* pool{};
  explicit Owner(int helper_cpu = -1) : arena(need(a::open_arena(4ull << 30, helper_cpu))) {
    done(w::adopt(lane)); pool = need(a::add_pool(*arena, lane));
  }
  ~Owner() {
    const auto limit = Clock::now() + std::chrono::seconds(30);
    for (;;) {
      auto progress = a::close(*pool);
      require(progress.kind() != orbital::Progress::Kind::failed, "close failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      w::poll(lane, 4096); std::this_thread::yield();
      require(Clock::now() < limit, "close timeout");
    }
    done(a::remove_pool(*pool)); w::relinquish(lane);
  }
};

volatile std::uint8_t sink;

void allocation_cases() {
  Owner owner;
  for (std::size_t size : {16, 64, 256, 1024, 3072}) {
    const a::AllocationRequest request{.bytes = size};
    measure("alloc/pair/" + std::to_string(size), 2'000'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        auto value = need(a::allocate(*owner.pool, request));
        value.address[0] = std::byte{1};
        a::release(value);
      }
    });
  }
  {
    // Same pair with one unrelated allocation kept live: the Pool never empties,
    // so the per-Pool Arena pin is not retaken on every pair.
    auto anchor = need(a::allocate(*owner.pool, {.bytes = 3072}));
    const a::AllocationRequest request{.bytes = 64};
    measure("alloc/pair-anchored/64", 2'000'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        auto value = need(a::allocate(*owner.pool, request));
        value.address[0] = std::byte{1};
        a::release(value);
      }
    });
    a::release(anchor);
  }
  for (std::size_t size : {64, 1024}) {
    const a::AllocationRequest request{.bytes = size};
    for (std::size_t live : {256, 4096}) {
      std::vector<a::Allocation> values(live);
      measure("alloc/batch" + std::to_string(live) + "/fifo/" + std::to_string(size), 2'000'000,
          [&](std::size_t n) {
        for (std::size_t done = 0; done < n; done += live) {
          for (auto& value : values) {
            value = need(a::allocate(*owner.pool, request)); value.address[0] = std::byte{1};
          }
          for (auto& value : values) a::release(value);
        }
      });
      measure("alloc/batch" + std::to_string(live) + "/lifo/" + std::to_string(size), 2'000'000,
          [&](std::size_t n) {
        for (std::size_t done = 0; done < n; done += live) {
          for (auto& value : values) {
            value = need(a::allocate(*owner.pool, request)); value.address[0] = std::byte{1};
          }
          for (auto i = values.size(); i--;) a::release(values[i]);
        }
      });
      // Steady churn with a live population: replace a pseudo-random victim.
      measure("alloc/churn" + std::to_string(live) + "/" + std::to_string(size), 2'000'000,
          [&](std::size_t n) {
        for (auto& value : values) value = need(a::allocate(*owner.pool, request));
        std::uint64_t state = 0x9e3779b97f4a7c15ull;
        for (std::size_t i = 0; i < n; ++i) {
          state ^= state << 13; state ^= state >> 7; state ^= state << 17;
          auto& victim = values[state % live];
          a::release(victim);
          victim = need(a::allocate(*owner.pool, request));
          victim.address[0] = std::byte{1};
        }
        for (auto& value : values) a::release(value);
      });
    }
  }
  // Cold growth: each block starts from an empty, trimmed Pool, so homes need
  // fresh pages (mprotect, first-touch fault) and new Groups. ns per allocation
  // includes the final releases and trim; it is a fill rate, not one latency.
  for (std::size_t size : {64, 1024}) {
    const a::AllocationRequest request{.bytes = size};
    std::vector<a::Allocation> values;
    measure("alloc/fill/" + std::to_string(size), 65536, [&](std::size_t n) {
      Owner fresh;
      values.resize(n);
      for (auto& value : values) {
        value = need(a::allocate(*fresh.pool, request)); value.address[0] = std::byte{1};
      }
      for (auto& value : values) a::release(value);
    });
  }
  for (std::size_t size : {8192, 65536, 1 << 20}) {
    const a::AllocationRequest request{.bytes = size};
    measure("alloc/window/" + std::to_string(size), 500'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        auto value = need(a::allocate(*owner.pool, request));
        value.address[0] = std::byte{1};
        a::release(value);
      }
    });
  }
  {
    auto finite = need(a::allocate(*owner.pool, {.bytes = 64}));
    auto window = need(a::allocate(*owner.pool, {.bytes = 65536}));
    measure("hold/finite", 4'000'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) { auto h = need(a::hold(finite, {0, 64})); a::release(h); }
    });
    measure("hold/window", 4'000'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) { auto h = need(a::hold(window, {0, 4096})); a::release(h); }
    });
  }
  // Peer control: the process allocator, same loop shapes.
  for (std::size_t size : {16, 64, 256, 1024, 3072}) {
    measure("malloc/pair/" + std::to_string(size), 4'000'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        auto* p = static_cast<std::uint8_t*>(std::malloc(size)); escape(p); p[0] = 1; std::free(p);
      }
    });
  }
  for (std::size_t live : {256, 4096}) {
    std::vector<void*> values(live);
    measure("malloc/batch" + std::to_string(live) + "/fifo/64", 4'000'000, [&](std::size_t n) {
      for (std::size_t done = 0; done < n; done += live) {
        for (auto& p : values) { p = std::malloc(64); escape(p); static_cast<std::uint8_t*>(p)[0] = 1; }
        for (auto* p : values) std::free(p);
      }
    });
  }
}

// One owner allocates; another thread performs every final release. The owner
// polls its Lane until those returns are recycled. ns per allocation round.
void foreign_cases() {
  for (std::size_t size : {64, 65536}) {
    Owner owner;
    constexpr std::size_t live = 256;
    std::vector<a::Allocation> values(live);
    std::atomic<int> phase{0};
    std::atomic<bool> stop{false};
    std::thread releaser([&] {
      pin(peer_cpus[0]);
      int seen = 0;
      for (;;) {
        int now;
        while ((now = phase.load(std::memory_order_acquire)) == seen) {
          if (stop.load(std::memory_order_acquire)) return;
        }
        seen = now;
        for (auto& value : values) a::release(value);
        phase.store(seen + 1, std::memory_order_release);
        seen += 1;
      }
    });
    int current = 0;
    const a::AllocationRequest request{.bytes = size};
    measure("foreign/release/" + std::to_string(size), 1'000'000, [&](std::size_t n) {
      for (std::size_t done = 0; done < n; done += live) {
        for (auto& value : values) value = need(a::allocate(*owner.pool, request));
        phase.store(++current, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) == current) {}
        ++current;
        while (w::pending(owner.lane)) w::poll(owner.lane, 1 << 20);
      }
    });
    stop.store(true, std::memory_order_release);
    releaser.join();
  }
}

struct Bounce {
  w::Message message;
  w::Lane* target{};
  std::atomic<std::uint64_t>* count{};
};
void bounce_handle(w::Message& message, w::Lane&) noexcept {
  auto& bounce = *static_cast<Bounce*>(message.context);
  bounce.count->fetch_add(1, std::memory_order_relaxed);
}
void no_retain(w::Message&) noexcept {}
void no_release(w::Message&) noexcept {}
const w::MessageOps bounce_ops{no_retain, bounce_handle, no_release};

void lane_cases() {
  // Ping-pong: each side posts to the other after receiving.
  {
    w::Lane left, right;
    std::atomic<std::uint64_t> left_count{0}, right_count{0};
    Bounce to_right{{}, &right, &right_count}, to_left{{}, &left, &left_count};
    to_right.message.context = &to_right; to_right.message.ops = &bounce_ops;
    to_left.message.context = &to_left; to_left.message.ops = &bounce_ops;
    std::atomic<bool> stop{false};
    std::atomic<bool> ready{false};
    std::thread peer([&] {
      pin(peer_cpus[0]);
      done(w::adopt(right));
      ready.store(true);
      std::uint64_t seen = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        w::poll(right, 64);
        const auto now = right_count.load(std::memory_order_relaxed);
        if (now != seen) { seen = now; w::post(left, to_left.message); }
      }
      while (w::pending(right)) w::poll(right, 64);
      w::relinquish(right);
    });
    done(w::adopt(left));
    while (!ready.load()) {}
    measure("lane/pingpong", 200'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        const auto before = left_count.load(std::memory_order_relaxed);
        w::post(right, to_right.message);
        while (left_count.load(std::memory_order_relaxed) == before) w::poll(left, 64);
      }
    });
    stop.store(true);
    peer.join();
    while (w::pending(left)) w::poll(left, 64);
    w::relinquish(left);
  }
  // Same-thread post/poll: the queue's uncontended local cost.
  {
    w::Lane lane;
    done(w::adopt(lane));
    std::atomic<std::uint64_t> count{0};
    std::vector<Bounce> records(64);
    for (auto& r : records) { r.count = &count; r.message.context = &r; r.message.ops = &bounce_ops; }
    measure("lane/local64", 4'000'000, [&](std::size_t n) {
      for (std::size_t done = 0; done < n; done += records.size()) {
        for (auto& r : records) w::post(lane, r.message);
        while (w::poll(lane, 64)) {}
      }
    });
    measure("lane/pending-empty", 20'000'000, [&](std::size_t n) {
      std::size_t found = 0;
      for (std::size_t i = 0; i < n; ++i) found += w::pending(lane);
      sink = static_cast<std::uint8_t>(found);
    });
    w::relinquish(lane);
  }
  // Many producers posting to one consumer: per-message consumer throughput.
#ifdef ORBITAL_POST_CHAIN
  constexpr bool chained_modes[] = {false, true};
#else
  constexpr bool chained_modes[] = {false};
#endif
  for (bool chained : chained_modes)
  for (int producers : {1, 3}) {
    w::Lane lane;
    done(w::adopt(lane));
    std::atomic<std::uint64_t> count{0};
    constexpr std::size_t per = 1024;
    std::vector<std::unique_ptr<Bounce[]>> records;
    for (int p = 0; p < producers; ++p) records.emplace_back(new Bounce[per]);
    for (auto& set : records) for (std::size_t k = 0; k < per; ++k) { auto& r = set[k]; r.count = &count; r.message.context = &r; r.message.ops = &bounce_ops; }
    std::atomic<int> round{0};
    std::atomic<int> finished{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p) threads.emplace_back([&, p] {
      pin(peer_cpus[p]);
      int seen = 0;
      for (;;) {
        int now;
        while ((now = round.load(std::memory_order_acquire)) == seen)
          if (stop.load(std::memory_order_acquire)) return;
        seen = now;
        if (!chained) {
          for (std::size_t k = 0; k < per; ++k) w::post(lane, records[p][k].message);
        } else {
#ifdef ORBITAL_POST_CHAIN
          // Groups of 64 already-linked records share one producer exchange.
          for (std::size_t k = 0; k < per; k += 64) {
            for (std::size_t j = k; j + 1 < k + 64; ++j)
              records[p][j].message.next.store(&records[p][j + 1].message, std::memory_order_relaxed);
            w::post_chain(lane, records[p][k].message, records[p][k + 63].message);
          }
#endif
        }
        finished.fetch_add(1, std::memory_order_acq_rel);
      }
    });
    int current = 0;
    measure("lane/mpsc" + std::to_string(producers) + (chained ? "-chain64" : ""), 2'000'000, [&](std::size_t n) {
      const std::size_t total = per * producers;
      for (std::size_t done = 0; done < n; done += total) {
        const auto target = count.load(std::memory_order_relaxed) + total;
        finished.store(0, std::memory_order_relaxed);
        round.store(++current, std::memory_order_release);
        while (count.load(std::memory_order_relaxed) < target) w::poll(lane, 256);
        while (finished.load(std::memory_order_acquire) != producers) {}
        while (w::pending(lane)) w::poll(lane, 256);
      }
    });
    stop.store(true);
    for (auto& t : threads) t.join();
    while (w::pending(lane)) w::poll(lane, 256);
    w::relinquish(lane);
  }
}

#if SIXDB_HOT_OBJECTS
// Sequential low words under one high word, as a shard-local allocator might
// issue them, and random 128-bit identities. Results are reported per family.
enum class Family { sequential, random };
orbital::ObjectId id_of(Family family, std::uint64_t i) {
  if (family == Family::sequential) return {0x51d0'0000'0000'0001ull, i};
  std::uint64_t x = i + 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull; x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  const auto high = x ^ (x >> 31);
  x = high + 0x632be59bd9b4e019ull;
  x = (x ^ (x >> 33)) * 0xff51afd7ed558ccdull; x = (x ^ (x >> 33)) * 0xc4ceb9fe1a85ec53ull;
  return {high, x ^ (x >> 33)};
}

void object_cases() {
  using namespace object_test;
  for (auto family : {Family::sequential, Family::random})
  for (std::size_t objects : {1024, 65536, 1 << 20}) {
    const auto id_of = [family](std::uint64_t i) { return ::id_of(family, i); };
    std::pmr::unsynchronized_pool_resource memory;
    Store store(memory);
    {
      std::vector<Store::Change> changes;
      changes.reserve(objects);
      for (std::size_t i = 0; i < objects; ++i)
        changes.push_back({id_of(i), need(store.capture({i, false}, {}))});
      Store::Scope basis(store);
      Store::Prepared packet(store);
      done(store.prepare(basis, changes, packet)); done(store.install(packet));
    }
    std::vector<orbital::ObjectId> order(objects);
    std::mt19937_64 random(42);
    for (std::size_t i = 0; i < objects; ++i) order[i] = id_of(random() % objects);
    std::vector<orbital::ObjectId> misses(objects);
    for (std::size_t i = 0; i < objects; ++i) misses[i] = id_of(objects + random() % objects);
    const auto suffix = std::string(family == Family::sequential ? "seq/" : "rand/") + std::to_string(objects);
    {
      Store::Scope scope(store);
      measure("object/bind-hit/" + suffix, 2'000'000, [&](std::size_t n) {
        std::uint64_t total = 0;
        for (std::size_t i = 0; i < n; ++i) total += scope.bind(order[i % objects]).value->encoded;
        sink = static_cast<std::uint8_t>(total);
      });
      // Dependent: the next key depends on the previous value (latency-bound).
      measure("object/bind-chain/" + suffix, 1'000'000, [&](std::size_t n) {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < n; ++i)
          value = scope.bind(id_of((value * 2654435761u + i) % objects)).value->encoded;
        sink = static_cast<std::uint8_t>(value);
      });
      measure("object/bind-miss/" + suffix, 2'000'000, [&](std::size_t n) {
        std::size_t found = 0;
        for (std::size_t i = 0; i < n; ++i) found += bool(scope.bind(misses[i % objects]));
        sink = static_cast<std::uint8_t>(found);
      });
    }
    measure("object/scope/" + suffix, 4'000'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) { Store::Scope s(store); sink = static_cast<std::uint8_t>(s.cut()); }
    });
    for (std::size_t batch : {1, 64}) {
      std::uint64_t next = 0;
      measure("object/publish" + std::to_string(batch) + "/" + suffix, objects >= 65536 ? 200'000 : 100'000,
          [&](std::size_t n) {
        std::vector<Store::Change> changes;
        changes.reserve(batch);
        for (std::size_t completed = 0; completed < n; completed += batch) {
          changes.clear();
          for (std::size_t k = 0; k < batch; ++k, ++next)
            changes.push_back({id_of((next * 7919) % objects), need(store.capture({next, false}, {}))});
          Store::Scope basis(store);
          Store::Prepared packet(store);
          done(store.prepare(basis, changes, packet)); done(store.install(packet));
          changes.clear();
          store.drain(batch * 8);
        }
      });
      store.drain();
    }
  }
}

#endif
#if SIXDB_HOT_FULL
orbital::Progress trivial_step(loom::Task&) noexcept { return orbital::Progress::done(); }
orbital::Progress yield_step(loom::Task& task) noexcept {
  auto& left = *static_cast<std::uint64_t*>(task.context);
  return left-- ? orbital::Progress::yield() : orbital::Progress::done();
}

void loom_cases() {
  auto arena = need(a::open_arena(1ull << 30));
  loom::Worker worker;
  done(loom::adopt(worker));
  done(loom::configure(worker, arena, 1024));
  std::vector<loom::Task> tasks(256);
  measure("loom/task-start-done", 2'000'000, [&](std::size_t n) {
    for (std::size_t done = 0; done < n; done += tasks.size()) {
      for (auto& task : tasks) { task = {}; task.step = trivial_step; require(loom::start(worker, task), "start"); }
      while (loom::poll(worker, {64, 256})) {}
    }
  });
  std::uint64_t remaining = 0;
  loom::Task looping{};
  measure("loom/task-yield", 4'000'000, [&](std::size_t n) {
    remaining = n; looping = {}; looping.step = yield_step; looping.context = &remaining;
    require(loom::start(worker, looping), "start");
    while (loom::poll(worker, {64, 1 << 20})) {}
  });
  measure("loom/allocate-pair/64", 2'000'000, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      auto value = need(loom::allocate(worker, {.bytes = 64}));
      value.address[0] = std::byte{1};
      a::release(value);
    }
  });
  for (;;) {
    auto progress = loom::shutdown(worker);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    require(progress.kind() != orbital::Progress::Kind::failed, "shutdown");
    loom::poll(worker, {4096, 0});
  }
  loom::relinquish(worker);
}
#endif
} // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--blocks" && i + 1 < argc) blocks = std::atoi(argv[++i]);
    else if (arg == "--scale" && i + 1 < argc) scale = std::atof(argv[++i]);
    else if (arg == "--filter" && i + 1 < argc) {
      std::string list = argv[++i];
      for (std::size_t start = 0; start <= list.size();) {
        const auto comma = std::min(list.find(',', start), list.size());
        if (comma > start) filters.push_back(list.substr(start, comma - start));
        start = comma + 1;
      }
    }
    else if (arg == "--cpu" && i + 1 < argc) main_cpu = std::atoi(argv[++i]);
    else if (arg == "--peers" && i + 1 < argc) {
      int k = 0;
      for (char* cursor = argv[++i]; *cursor && k < 3; ++k) {
        peer_cpus[k] = static_cast<int>(std::strtol(cursor, &cursor, 10));
        if (*cursor == ',') ++cursor;
      }
    }
  }
  pin(main_cpu);
  std::printf("case,operations,median_ns,min_ns,max_ns,instructions\n");
  // A filter naming a section skips the others' setup as well as their timing.
  const auto wants = [](std::initializer_list<const char*> prefixes) {
    if (filters.empty()) return true;
    for (const auto& filter : filters)
      for (const auto* prefix : prefixes)
        if (filter.starts_with(prefix) || std::string(prefix).starts_with(filter)) return true;
    return false;
  };
  if (wants({"alloc", "malloc", "hold"})) allocation_cases();
  if (wants({"foreign"})) foreign_cases();
  if (wants({"lane"})) lane_cases();
#if SIXDB_HOT_OBJECTS
  if (wants({"object"})) object_cases();
#endif
#if SIXDB_HOT_FULL
  if (wants({"loom"})) loom_cases();
#endif
}
