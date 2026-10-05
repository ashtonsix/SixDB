#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#endif
#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
#include "allocator_libraries.h"
#endif

namespace {
using Clock = std::chrono::steady_clock;
struct Options {
  double ms{20};
  unsigned repetitions{3};
  bool reverse{};
  bool check{};
  int second_cpu{-1};
};

Options options(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--quick") { o.ms = 1; o.repetitions = 1; }
    else if (arg == "--check") o.check = true;
    else if (arg == "--reverseorder") o.reverse = true;
    else if (arg == "--ms" && i + 1 < argc) o.ms = std::stod(argv[++i]);
    else if (arg == "--repetitions" && i + 1 < argc)
      o.repetitions = static_cast<unsigned>(std::stoul(argv[++i]));
    else if (arg == "--second-cpu" && i + 1 < argc) o.second_cpu = std::stoi(argv[++i]);
    else throw std::invalid_argument("unknown or incomplete option: " + arg);
  }
  if (!std::isfinite(o.ms) || o.ms <= 0 || !o.repetitions)
    throw std::invalid_argument("positive duration and repetitions required");
  return o;
}

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

void escape(void* p) { asm volatile("" : : "r"(p) : "memory"); }
std::uint64_t touch(void* p, std::size_t bytes, std::uint64_t value) {
  require(p != nullptr, "allocation exhausted");
  std::memcpy(p, &value, sizeof(value));
  const auto last = value ^ 0x13579bdf2468ace0ULL;
  std::memcpy(static_cast<std::byte*>(p) + bytes - sizeof(last), &last, sizeof(last));
  escape(p);
  std::uint64_t a, b;
  std::memcpy(&a, p, sizeof(a));
  std::memcpy(&b, static_cast<std::byte*>(p) + bytes - sizeof(b), sizeof(b));
  require(a == value && b == last, "payload corruption");
  return a;
}

struct Storage {
  std::byte* bytes;
  std::size_t stride, capacity;
  Storage(std::size_t size, std::size_t count) : stride(size), capacity(count) {
    void* p = nullptr;
    require(posix_memalign(&p, 64, size * count) == 0, "pool backing allocation");
    bytes = static_cast<std::byte*>(p);
    std::memset(bytes, 0, size * count); // Prepared backing, outside measurement.
  }
  ~Storage() { std::free(bytes); }
  std::size_t index(void* p) const {
    const auto offset = static_cast<std::byte*>(p) - bytes;
    require(offset >= 0 && static_cast<std::size_t>(offset) < stride * capacity &&
            offset % stride == 0, "invalid pool pointer");
    return static_cast<std::size_t>(offset) / stride;
  }
};

struct Libc {
  std::size_t bytes;
  Libc(std::size_t size, std::size_t) : bytes(size) {}
  void* allocate() { return std::malloc(bytes); }
  void release(void* p) { std::free(p); }
  void reset() {}
  std::size_t prepared_bytes() const { return 0; }
  std::size_t metadata_bytes() const { return 0; }
  std::size_t usable(void* p) const {
#if defined(__linux__)
    return malloc_usable_size(p);
#else
    (void)p; return bytes;
#endif
  }
};

#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
struct Mimalloc : Libc {
  using Libc::Libc;
  void* allocate() { return allocator_libraries::mi_allocate(bytes); }
  void release(void* p) { allocator_libraries::mi_release(p); }
  std::size_t usable(void* p) const { return allocator_libraries::mi_capacity(p); }
};
struct Snmalloc : Libc {
  using Libc::Libc;
  void* allocate() { return allocator_libraries::sn_allocate(bytes); }
  void release(void* p) { allocator_libraries::sn_release(p); }
  std::size_t usable(void* p) const { return allocator_libraries::sn_capacity(p); }
};
#endif

struct Freelist {
  Storage store;
  std::vector<std::uint32_t> next;
  std::uint32_t first{};
  Freelist(std::size_t size, std::size_t count) : store(size, count), next(count) {
    std::iota(next.begin(), next.end(), 1);
  }
  void* allocate() {
    if (first == next.size()) return nullptr;
    const auto index = first;
    first = next[index];
    return store.bytes + index * store.stride;
  }
  void release(void* p) {
    const auto index = store.index(p);
    next[index] = first;
    first = static_cast<std::uint32_t>(index);
  }
  void reset() {}
  std::size_t prepared_bytes() const { return store.stride * store.capacity; }
  std::size_t metadata_bytes() const { return next.size() * sizeof(next[0]); }
  std::size_t usable(void*) const { return store.stride; }
};

struct Bitmap {
  Storage store;
  std::vector<std::uint64_t> free;
  std::size_t hint{};
  Bitmap(std::size_t size, std::size_t count) : store(size, count), free(count / 64, ~0ULL) {
    require(count % 64 == 0, "bitmap capacity must be a multiple of 64");
  }
  void* allocate() {
    for (std::size_t n = 0; n < free.size(); ++n) {
      const auto row = (hint + n) % free.size();
      if (!free[row]) continue;
      const auto bit = std::countr_zero(free[row]);
      free[row] &= free[row] - 1;
      hint = row;
      return store.bytes + (row * 64 + bit) * store.stride;
    }
    return nullptr;
  }
  void release(void* p) {
    const auto index = store.index(p);
    require(!(free[index / 64] & (1ULL << (index % 64))), "duplicate pool free");
    free[index / 64] |= 1ULL << (index % 64);
    hint = index / 64;
  }
  void reset() {}
  std::size_t prepared_bytes() const { return store.stride * store.capacity; }
  std::size_t metadata_bytes() const { return free.size() * sizeof(free[0]); }
  std::size_t usable(void*) const { return store.stride; }
};

struct LockedBitmap : Bitmap {
  std::mutex mutex;
  using Bitmap::Bitmap;
  void* allocate() { std::lock_guard lock(mutex); return Bitmap::allocate(); }
  void release(void* p) { std::lock_guard lock(mutex); Bitmap::release(p); }
  std::size_t metadata_bytes() const { return Bitmap::metadata_bytes() + sizeof(mutex); }
};

struct AtomicBitmap {
  struct alignas(64) Word { std::atomic<std::uint64_t> free{~0ULL}; };
  Storage store;
  std::unique_ptr<Word[]> words;
  std::size_t count;
  AtomicBitmap(std::size_t size, std::size_t capacity)
      : store(size, capacity), words(std::make_unique<Word[]>(capacity / 64)), count(capacity / 64) {
    require(capacity % 64 == 0, "atomic capacity must be a multiple of 64");
  }
  void* allocate() {
    for (std::size_t row = 0; row < count; ++row) {
      auto value = words[row].free.load(std::memory_order_relaxed);
      while (value) {
        const auto next = value & (value - 1);
        const auto bit = std::countr_zero(value);
        if (words[row].free.compare_exchange_weak(value, next,
                std::memory_order_acquire, std::memory_order_relaxed))
          return store.bytes + (row * 64 + bit) * store.stride;
      }
    }
    return nullptr;
  }
  void release(void* p) {
    const auto index = store.index(p);
    const auto bit = 1ULL << (index % 64);
    const auto prior = words[index / 64].free.fetch_or(bit, std::memory_order_release);
    require(!(prior & bit), "duplicate atomic free");
  }
  void reset() {}
  std::size_t prepared_bytes() const { return store.stride * store.capacity; }
  std::size_t metadata_bytes() const { return count * sizeof(Word); }
  std::size_t usable(void*) const { return store.stride; }
};

struct Region {
  Storage store;
  std::size_t used{};
  Region(std::size_t size, std::size_t count) : store(size, count) {}
  void* allocate() {
    if (used == store.capacity) return nullptr;
    return store.bytes + used++ * store.stride;
  }
  void release(void*) {} // Only valid when every allocation ends before reset.
  void reset() { used = 0; }
  std::size_t prepared_bytes() const { return store.stride * store.capacity; }
  std::size_t metadata_bytes() const { return sizeof(used); }
  std::size_t usable(void*) const { return store.stride; }
};

struct Batch {
  std::vector<void*> pointers;
  std::vector<std::size_t> order;
  std::uint64_t base, expected;
  explicit Batch(std::size_t live, std::uint64_t prefix = 0)
      : pointers(live), order(live), base(prefix), expected(live * prefix + live * (live + 1) / 2) {
    std::iota(order.begin(), order.end(), 0);
    // A coprime rotation is not enough: use a fixed independent permutation.
    std::uint64_t entropy = 0x243f6a8885a308d3ULL;
    for (std::size_t n = live; n > 1; --n) {
      entropy ^= entropy << 13; entropy ^= entropy >> 7; entropy ^= entropy << 17;
      std::swap(order[n - 1], order[entropy % n]);
    }
  }
  template<class Allocator> std::uint64_t cycle(Allocator& allocator, std::size_t bytes) {
    for (std::size_t i = 0; i < pointers.size(); ++i) {
      pointers[i] = allocator.allocate();
      touch(pointers[i], bytes, base + i + 1);
    }
    std::uint64_t sum{};
    for (const auto i : order) {
      std::uint64_t value;
      std::memcpy(&value, pointers[i], sizeof(value));
      require(value == base + i + 1, "aliasing or premature reuse");
      sum += value;
      allocator.release(pointers[i]);
    }
    allocator.reset();
    return sum;
  }
};

template<class Allocator> void probe() {
  for (const auto bytes : {16U, 64U, 96U}) {
    Allocator a(bytes, 1024);
    Batch b(1024);
    for (unsigned i = 0; i < 3; ++i)
      require(b.cycle(a, bytes) == b.expected, "allocation probe checksum");
  }
}

template<class Allocator> void bounded_probe() {
  Allocator a(16, 64);
  std::vector<void*> pointers;
  for (unsigned i = 0; i < 64; ++i) {
    auto* p = a.allocate();
    require(p != nullptr, "premature pool exhaustion");
    touch(p, 16, i + 1);
    pointers.push_back(p);
  }
  require(a.allocate() == nullptr, "pool capacity overflow");
  for (unsigned i = 0; i < 64; ++i) {
    std::uint64_t value;
    std::memcpy(&value, pointers[i], sizeof(value));
    require(value == i + 1, "full pool alias");
    a.release(pointers[i]);
  }
  a.reset();
  Batch batch(64);
  require(batch.cycle(a, 16) == batch.expected, "exhausted pool failed to recover");
}

template<class Allocator> void concurrent_probe() {
  Allocator a(16, 1024);
  std::vector<void*> first(64), second(64);
  std::atomic<bool> ready{}, release{};
  std::thread worker([&] {
    for (std::size_t i = 0; i < second.size(); ++i) {
      second[i] = a.allocate();
      touch(second[i], 16, 0x100000000ULL + i + 1);
    }
    ready.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
    for (auto p : second) a.release(p);
  });
  for (std::size_t i = 0; i < first.size(); ++i) {
    first[i] = a.allocate();
    touch(first[i], 16, i + 1);
  }
  while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
  std::vector<std::uintptr_t> addresses;
  bool unique = true, intact = true;
  for (std::size_t i = 0; i < first.size(); ++i) {
    std::uint64_t x, y;
    std::memcpy(&x, first[i], sizeof(x));
    std::memcpy(&y, second[i], sizeof(y));
    intact &= x == i + 1 && y == 0x100000000ULL + i + 1;
    addresses.push_back(reinterpret_cast<std::uintptr_t>(first[i]));
    addresses.push_back(reinterpret_cast<std::uintptr_t>(second[i]));
  }
  std::sort(addresses.begin(), addresses.end());
  unique = std::adjacent_find(addresses.begin(), addresses.end()) == addresses.end();
  release.store(true, std::memory_order_release);
  worker.join();
  for (auto p : first) a.release(p);
  require(unique && intact, "simultaneous live allocations alias");
}

struct Sample { std::uint64_t cycles{}, checksum{}; double nanoseconds{}; };
template<class Allocator> Sample timed(Allocator& a, Batch& batch, std::size_t bytes, double ms) {
  Sample s;
  const auto begin = Clock::now();
  auto end = begin;
  do {
    for (unsigned i = 0; i < 64; ++i) {
      s.checksum += batch.cycle(a, bytes);
      ++s.cycles;
    }
    end = Clock::now();
  } while (std::chrono::duration<double, std::milli>(end - begin).count() < ms);
  s.nanoseconds = std::chrono::duration<double, std::nano>(end - begin).count();
  require(s.checksum == batch.expected * s.cycles, "timing checksum");
  return s;
}

template<class Allocator> void sequential(const char* name, std::size_t bytes, std::size_t live,
                                         const Options& o) {
  const auto capacity = std::max<std::size_t>(64, live);
  Allocator a(bytes, capacity);
  Batch batch(live);
  std::size_t usable{};
  for (auto& pointer : batch.pointers) {
    pointer = a.allocate(); require(pointer != nullptr, "preparation exhausted");
    usable += a.usable(pointer);
  }
  for (auto pointer : batch.pointers) a.release(pointer);
  a.reset();
  require(batch.cycle(a, bytes) == batch.expected, "warmup checksum");
  for (unsigned r = 0; r < o.repetitions; ++r) {
    const auto s = timed(a, batch, bytes, o.ms);
    std::cout << name << ",single," << bytes << ',' << live << ',' << r << ','
              << s.nanoseconds / (s.cycles * live) << ',' << s.cycles * live << ','
              << s.checksum << ',' << live * bytes << ',' << usable << ','
              << a.prepared_bytes() << ',' << a.metadata_bytes() << '\n';
  }
}

void pin(int cpu) {
#if defined(__linux__)
  require(cpu >= 0 && cpu < CPU_SETSIZE, "invalid second CPU");
  cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(cpu, &mask);
  require(pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) == 0, "thread pin failed");
#else
  (void)cpu; throw std::runtime_error("two-core experiment requires Linux affinity");
#endif
}

template<class Allocator> void concurrent(const char* name, std::size_t bytes, const Options& o) {
  Allocator a(bytes, 1024);
  // Both threads complete their own byte use before freeing; this is contention,
  // not delayed remote retirement or a shared descriptor acquisition protocol.
  for (unsigned r = 0; r < o.repetitions; ++r) {
    std::atomic<bool> ready{}, go{};
    Sample second;
    std::thread worker([&] {
      pin(o.second_cpu);
      Batch batch(64, 0x100000000ULL);
      ready.store(true, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
      second = timed(a, batch, bytes, o.ms);
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    Batch batch(64);
    const auto begin = Clock::now();
    go.store(true, std::memory_order_release);
    const auto first = timed(a, batch, bytes, o.ms);
    worker.join();
    const auto elapsed = std::chrono::duration<double, std::nano>(Clock::now() - begin).count();
    const auto pairs = (first.cycles + second.cycles) * 64;
    std::cout << name << ",two_core_shared," << bytes << ",128," << r << ','
              << elapsed / pairs << ',' << pairs << ',' << first.checksum + second.checksum
              << ',' << 128 * bytes << ",0," << a.prepared_bytes() << ','
              << a.metadata_bytes() << '\n';
  }
}

struct Case { std::size_t bytes, live; };
} // namespace

int main(int argc, char** argv) {
  try {
    const auto o = options(argc, argv);
    // All candidates see libc's multithreaded process mode, even single-core runs.
    std::thread([] {}).join();
    probe<Libc>(); probe<Freelist>(); probe<Bitmap>(); probe<LockedBitmap>();
    probe<AtomicBitmap>(); probe<Region>();
    bounded_probe<Freelist>(); bounded_probe<Bitmap>(); bounded_probe<LockedBitmap>();
    bounded_probe<AtomicBitmap>(); bounded_probe<Region>();
    concurrent_probe<Libc>(); concurrent_probe<LockedBitmap>(); concurrent_probe<AtomicBitmap>();
#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
    probe<Mimalloc>(); probe<Snmalloc>();
    concurrent_probe<Mimalloc>(); concurrent_probe<Snmalloc>();
#endif
    if (o.check) { std::cout << "allocation payload/reuse checks passed\n"; return 0; }
    std::cout << "allocator,workload,bytes,live,rep,ns_per_pair,pairs,checksum,requested_live_bytes,"
                 "initial_usable_live_bytes,prepared_capacity_bytes,policy_metadata_bytes\n";
    std::cout << std::setprecision(10);
    std::vector<Case> cases;
    for (auto bytes : {16U, 64U, 96U})
      for (auto live : {1U, 64U, 1024U}) cases.push_back({bytes, live});
    if (o.reverse) std::reverse(cases.begin(), cases.end());
    for (const auto [bytes, live] : cases) {
      auto run = [&]<class A>(const char* name) { sequential<A>(name, bytes, live, o); };
      if (!o.reverse) {
        run.template operator()<Libc>("libc");
#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
        run.template operator()<Mimalloc>("mimalloc_3.5.3");
        run.template operator()<Snmalloc>("snmalloc_0.7.5");
#endif
        run.template operator()<Freelist>("local_freelist");
        run.template operator()<Bitmap>("local_bitmap");
        run.template operator()<LockedBitmap>("shared_mutex_bitmap");
        run.template operator()<AtomicBitmap>("shared_atomic_bitmap");
        run.template operator()<Region>("region_bulk_reset");
      } else {
        run.template operator()<Region>("region_bulk_reset");
        run.template operator()<AtomicBitmap>("shared_atomic_bitmap");
        run.template operator()<LockedBitmap>("shared_mutex_bitmap");
        run.template operator()<Bitmap>("local_bitmap");
        run.template operator()<Freelist>("local_freelist");
#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
        run.template operator()<Snmalloc>("snmalloc_0.7.5");
        run.template operator()<Mimalloc>("mimalloc_3.5.3");
#endif
        run.template operator()<Libc>("libc");
      }
    }
    if (o.second_cpu >= 0) {
      for (const auto bytes : {16U, 64U, 96U}) {
        if (!o.reverse) {
          concurrent<Libc>("libc", bytes, o);
#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
          concurrent<Mimalloc>("mimalloc_3.5.3", bytes, o);
          concurrent<Snmalloc>("snmalloc_0.7.5", bytes, o);
#endif
          concurrent<AtomicBitmap>("shared_atomic_bitmap", bytes, o);
          concurrent<LockedBitmap>("shared_mutex_bitmap", bytes, o);
        } else {
          concurrent<LockedBitmap>("shared_mutex_bitmap", bytes, o);
          concurrent<AtomicBitmap>("shared_atomic_bitmap", bytes, o);
#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
          concurrent<Snmalloc>("snmalloc_0.7.5", bytes, o);
          concurrent<Mimalloc>("mimalloc_3.5.3", bytes, o);
#endif
          concurrent<Libc>("libc", bytes, o);
        }
      }
    }
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
