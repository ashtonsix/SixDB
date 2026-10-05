#include "grouped.h"
#include <frozen/grouped.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
#include "allocator_libraries.h"
#endif

namespace {
using clock_type = std::chrono::steady_clock;
using handle = std::uintptr_t;
constexpr std::size_t pool_capacity = 1024;
constexpr std::size_t packing_width = 1024;
constexpr std::size_t trim_period = 8;
constexpr std::uint64_t last_mask = 0x13579bdf2468ace0ULL;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct options {
  double ms{20};
  unsigned repetitions{3};
  bool reverse{};
  bool check{};
};
options parse(int argc, char **argv) {
  options result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--quick") {
      result.ms = 1;
      result.repetitions = 1;
    } else if (argument == "--check")
      result.check = true;
    else if (argument == "--reverse" || argument == "--reverseorder")
      result.reverse = true;
    else if (argument == "--ms" && index + 1 < argc)
      result.ms = std::stod(argv[++index]);
    else if (argument == "--repetitions" && index + 1 < argc)
      result.repetitions = static_cast<unsigned>(std::stoul(argv[++index]));
    else
      throw std::invalid_argument("unknown or incomplete tuning option");
  }
  require(std::isfinite(result.ms) && result.ms > 0 && result.repetitions,
          "positive duration and repetitions required");
  return result;
}

struct footprint {
  bool measured{};
  std::size_t backing{}, metadata{}, reserved{}, control{}, mapped{}, cached{};
  std::size_t maps{}, discarded{}, live{}, allocations{}, releases{};
  std::size_t temporary_scratch{};
};
struct tuned_policy {
  using arena_type = descriptors::grouped_arena;
  static constexpr bool deferred_pages = true;
  static constexpr auto layout = descriptors::grouped_layout::class_major_reordered;
  static void *address(handle head, const arena_type &arena) {
    return descriptors::grouped_resolve(head, arena.layout());
  }
};
struct frozen_policy {
  using arena_type = frozen_descriptors::grouped_arena;
  static constexpr bool deferred_pages = false;
  static constexpr auto layout = frozen_descriptors::grouped_layout::class_major_reordered;
  static void *address(handle head, const arena_type &arena) {
    return frozen_descriptors::grouped_resolve(head, arena.layout());
  }
};
template <class policy, bool selected = false> struct descriptor {
  static constexpr bool has_accounting = true;
  typename policy::arena_type arena;
  std::size_t bytes;
  descriptor(std::size_t size, std::size_t)
      : arena(policy::layout, packing_width, 1), bytes(size) {}
  handle allocate() {
    if constexpr (selected)
      return arena.allocate_in_grid(0, bytes);
    else
      return arena.allocate(bytes);
  }
  void release(handle head) { arena.release(head); }
  void *address(handle head) const { return policy::address(head, arena); }
  std::size_t preparation_capacity() const {
    return selected ? 16 : pool_capacity;
  }
  void trim() { arena.trim(); }
  auto report() const { return arena.counters(); }
  footprint state() const {
    const auto counters = arena.counters();
    const auto granule = arena.layout().mapping_granule;
    return {true,
            (arena.control_granules() + counters.payload_granules) * granule,
            arena.owner_record_bytes(), arena.reserved_bytes(),
            arena.control_granules() * granule,
            counters.payload_granules * granule,
            counters.cached_empty_granules * granule, counters.map_calls,
            counters.released_granules, counters.live_allocations,
            counters.allocations, counters.releases,
            policy::deferred_pages ? arena.layout().payload_bytes / granule : 0};
  }
};

struct storage {
  std::byte *base;
  std::size_t stride, count;
  storage(std::size_t bytes, std::size_t capacity) : stride(bytes), count(capacity) {
    void *allocation = nullptr;
    require(posix_memalign(&allocation, 64, bytes * capacity) == 0,
            "pool backing allocation failed");
    base = static_cast<std::byte *>(allocation);
    std::memset(base, 0, bytes * capacity);
  }
  ~storage() { std::free(base); }
  std::size_t index(handle value) const {
    const auto begin = reinterpret_cast<handle>(base);
    require(value >= begin && value < begin + stride * count &&
                (value - begin) % stride == 0,
            "invalid pool pointer");
    return (value - begin) / stride;
  }
};
struct libc_allocator {
  static constexpr bool has_accounting = false;
  std::size_t bytes;
  libc_allocator(std::size_t size, std::size_t) : bytes(size) {}
  handle allocate() { return reinterpret_cast<handle>(std::malloc(bytes)); }
  void release(handle value) { std::free(reinterpret_cast<void *>(value)); }
  void *address(handle value) const { return reinterpret_cast<void *>(value); }
  std::size_t preparation_capacity() const { return pool_capacity; }
  footprint state() const { return {}; }
};
#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
struct mimalloc_allocator : libc_allocator {
  using libc_allocator::libc_allocator;
  handle allocate() {
    return reinterpret_cast<handle>(allocator_libraries::mi_allocate(bytes));
  }
  void release(handle value) {
    allocator_libraries::mi_release(reinterpret_cast<void *>(value));
  }
};
struct snmalloc_allocator : libc_allocator {
  using libc_allocator::libc_allocator;
  handle allocate() {
    return reinterpret_cast<handle>(allocator_libraries::sn_allocate(bytes));
  }
  void release(handle value) {
    allocator_libraries::sn_release(reinterpret_cast<void *>(value));
  }
};
#endif
struct free_list {
  static constexpr bool has_accounting = false;
  storage store;
  std::vector<std::uint32_t> next;
  std::uint32_t first{};
  free_list(std::size_t bytes, std::size_t capacity)
      : store(bytes, capacity), next(capacity) {
    std::iota(next.begin(), next.end(), 1);
  }
  handle allocate() {
    if (first == next.size())
      return 0;
    const auto index = first;
    first = next[index];
    return reinterpret_cast<handle>(store.base + index * store.stride);
  }
  void release(handle value) {
    const auto index = store.index(value);
    next[index] = first;
    first = static_cast<std::uint32_t>(index);
  }
  void *address(handle value) const { return reinterpret_cast<void *>(value); }
  std::size_t preparation_capacity() const { return store.count; }
  footprint state() const {
    return {true, store.count * store.stride,
            sizeof(*this) + next.capacity() * sizeof(next[0])};
  }
};
struct bitmap {
  static constexpr bool has_accounting = false;
  storage store;
  std::vector<std::uint64_t> vacant;
  std::size_t hint{};
  bitmap(std::size_t bytes, std::size_t capacity)
      : store(bytes, capacity), vacant(capacity / 64, ~0ULL) {
    require(capacity % 64 == 0, "bitmap capacity must be a multiple of64");
  }
  handle allocate() {
    for (std::size_t step = 0; step < vacant.size(); ++step) {
      const auto row = (hint + step) % vacant.size();
      if (!vacant[row])
        continue;
      const auto slot = std::countr_zero(vacant[row]);
      vacant[row] &= vacant[row] - 1;
      hint = row;
      return reinterpret_cast<handle>(store.base + (row * 64 + slot) * store.stride);
    }
    return 0;
  }
  void release(handle value) {
    const auto index = store.index(value);
    const auto bit = 1ULL << (index % 64);
    require(!(vacant[index / 64] & bit), "duplicate bitmap free");
    vacant[index / 64] |= bit;
    hint = index / 64;
  }
  void *address(handle value) const { return reinterpret_cast<void *>(value); }
  std::size_t preparation_capacity() const { return store.count; }
  footprint state() const {
    return {true, store.count * store.stride,
            sizeof(*this) + vacant.capacity() * sizeof(vacant[0])};
  }
};

std::uint64_t first_word(const void *pointer) {
  std::uint64_t value;
  std::memcpy(&value, pointer, sizeof(value));
  return value;
}
void verify(const void *pointer, std::size_t bytes, std::uint64_t expected) {
  std::uint64_t last;
  std::memcpy(&last, static_cast<const std::byte *>(pointer) + bytes - 8, 8);
  require(first_word(pointer) == expected && last == (expected ^ last_mask),
          "live payload aliasing or corruption");
}
void touch(void *pointer, std::size_t bytes, std::uint64_t value) {
  require(pointer != nullptr, "allocation exhausted");
  const auto last = value ^ last_mask;
  std::memcpy(pointer, &value, 8);
  std::memcpy(static_cast<std::byte *>(pointer) + bytes - 8, &last, 8);
  asm volatile("" : : "r"(pointer) : "memory");
  verify(pointer, bytes, value);
}
std::vector<std::size_t> permutation(std::size_t count) {
  std::vector<std::size_t> result(count);
  std::iota(result.begin(), result.end(), 0);
  std::uint64_t entropy = 0x243f6a8885a308d3ULL;
  for (std::size_t remaining = count; remaining > 1; --remaining) {
    entropy ^= entropy << 13;
    entropy ^= entropy >> 7;
    entropy ^= entropy << 17;
    std::swap(result[remaining - 1], result[entropy % remaining]);
  }
  return result;
}
template <class allocator> void prepare(allocator &owner, std::size_t bytes) {
  std::vector<handle> prepared(owner.preparation_capacity());
  for (std::size_t index = 0; index < prepared.size(); ++index) {
    prepared[index] = owner.allocate();
    require(prepared[index] != 0, "backing preparation exhausted");
    touch(owner.address(prepared[index]), bytes, index + 1);
  }
  const auto order = permutation(prepared.size());
  for (const auto index : order) {
    verify(owner.address(prepared[index]), bytes, index + 1);
    owner.release(prepared[index]);
  }
}

enum class cadence { none, counters, trim };
const char *cadence_name(cadence policy) {
  switch (policy) {
  case cadence::none: return "none";
  case cadence::counters: return "counters_every_batch";
  case cadence::trim: return "trim_every8_batches";
  }
  std::terminate();
}
struct history {
  std::size_t live, replace_percent;
  bool rolling() const { return replace_percent != 0; }
  std::size_t pairs() const {
    return rolling() ? (live * replace_percent + 99) / 100 : live;
  }
};
struct batch {
  history shape;
  std::vector<handle> handles;
  std::vector<std::uint64_t> values;
  std::vector<std::size_t> order;
  std::size_t cursor{}, batches{};
  std::uint64_t report_sink{};
  explicit batch(history request)
      : shape(request), handles(request.live), values(request.live),
        order(permutation(request.live)) {
    std::iota(values.begin(), values.end(), 1);
  }
  template <class allocator> void initialize(allocator &owner, std::size_t bytes) {
    if (shape.rolling())
      for (std::size_t index = 0; index < handles.size(); ++index) {
        handles[index] = owner.allocate();
        require(handles[index], "retained initial live set exhausted");
        touch(owner.address(handles[index]), bytes, values[index]);
      }
  }
  template <class allocator> std::uint64_t cycle(allocator &owner,
                                               std::size_t bytes,
                                               cadence policy) {
    std::uint64_t sum = 0, expected = 0;
    if (shape.rolling()) {
      for (std::size_t step = 0; step < shape.pairs(); ++step) {
        const auto index = order[(cursor + step) % handles.size()];
        verify(owner.address(handles[index]), bytes, values[index]);
        owner.release(handles[index]);
      }
      for (std::size_t step = 0; step < shape.pairs(); ++step) {
        const auto index = order[(cursor + step) % handles.size()];
        handles[index] = owner.allocate();
        require(handles[index], "rolling replacement exhausted");
        values[index] += shape.live;
        auto *pointer = owner.address(handles[index]);
        touch(pointer, bytes, values[index]);
        sum += first_word(pointer);
        expected += values[index];
      }
      cursor = (cursor + shape.pairs()) % handles.size();
    } else {
      for (std::size_t index = 0; index < handles.size(); ++index) {
        handles[index] = owner.allocate();
        require(handles[index], "live batch exhausted");
        touch(owner.address(handles[index]), bytes, values[index]);
      }
      for (const auto index : order) {
        auto *pointer = owner.address(handles[index]);
        verify(pointer, bytes, values[index]);
        sum += first_word(pointer);
        expected += values[index];
        owner.release(handles[index]);
      }
    }
    require(sum == expected, "independent payload checksum mismatch");
    ++batches;
    if constexpr (allocator::has_accounting) {
      if (policy == cadence::counters) {
        const auto state = owner.report();
        require(state.live_allocations == (shape.rolling() ? shape.live : 0),
                "reported live count disagrees with history");
        report_sink += state.cached_empty_granules + state.payload_granules +
                       state.map_calls;
      } else if (policy == cadence::trim && batches % trim_period == 0)
        owner.trim();
    }
    asm volatile("" : "+r"(sum), "+m"(report_sink) : : "memory");
    return sum;
  }
  template <class allocator> void validate_live(allocator &owner,
                                               std::size_t bytes) const {
    if (shape.rolling()) {
      std::vector<handle> distinct;
      distinct.reserve(handles.size());
      for (std::size_t index = 0; index < handles.size(); ++index) {
        auto *pointer = owner.address(handles[index]);
        verify(pointer, bytes, values[index]);
        distinct.push_back(reinterpret_cast<handle>(pointer));
      }
      std::sort(distinct.begin(), distinct.end());
      require(std::adjacent_find(distinct.begin(), distinct.end()) == distinct.end(),
              "simultaneous retained payloads alias");
    }
  }
  template <class allocator> void close(allocator &owner) {
    if (shape.rolling())
      for (const auto value : handles)
        owner.release(value);
  }
};

struct sample {
  double ns{};
  std::size_t cycles{};
  std::uint64_t checksum{};
};
template <class allocator> sample measure(allocator &owner, batch &data,
                                         std::size_t bytes, cadence policy,
                                         const options &config) {
  sample result;
  const auto repetitions = std::max<std::size_t>(1, 4096 / data.shape.pairs());
  const auto begin = clock_type::now();
  auto end = begin;
  do {
    for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
      result.checksum += data.cycle(owner, bytes, policy);
      ++result.cycles;
    }
    end = clock_type::now();
  } while (std::chrono::duration<double, std::milli>(end - begin).count() < config.ms);
  result.ns = std::chrono::duration<double, std::nano>(end - begin).count();
  return result;
}
template <class allocator>
void run(const char *name, const char *path, std::size_t bytes, history request,
         cadence policy, const options &config) {
  allocator owner(bytes, pool_capacity);
  prepare(owner, bytes);
  batch data(request);
  data.initialize(owner, bytes);
  for (unsigned warmup = 0; warmup < trim_period; ++warmup)
    data.cycle(owner, bytes, policy);
  data.validate_live(owner, bytes);
  for (unsigned repetition = 0; repetition < config.repetitions; ++repetition) {
    const auto before = owner.state();
    const auto result = measure(owner, data, bytes, policy, config);
    const auto after = owner.state();
    data.validate_live(owner, bytes);
    const auto pairs = result.cycles * request.pairs();
    if constexpr (allocator::has_accounting) {
      require(after.live == (request.rolling() ? request.live : 0) &&
                  after.allocations - before.allocations == pairs &&
                  after.releases - before.releases == pairs,
              "timed allocation/release counters disagree");
      if (policy != cadence::trim)
        require(after.maps == before.maps,
                "prepared history unexpectedly mapped new backing");
    }
    if (!config.check)
      std::cout << name << ',' << path << ','
                << (request.rolling() ? "rolling" : "allocate_all_free_permuted")
                << ',' << cadence_name(policy) << ',' << bytes << ','
                << request.live << ',' << request.replace_percent << ','
                << request.pairs() << ',' << owner.preparation_capacity() << ',' << repetition
                << ',' << result.ns / static_cast<double>(pairs) << ','
                << pairs << ',' << result.cycles << ',' << result.checksum << ','
                << (request.rolling() ? request.live * bytes : 0) << ','
                << request.live * bytes << ',' << after.measured << ','
                << after.backing << ',' << after.metadata << ',' << after.reserved
                << ',' << after.control << ',' << after.mapped << ',' << after.cached
                << ',' << after.maps - before.maps << ','
                << after.discarded - before.discarded << ',' << data.report_sink
                << ',' << after.temporary_scratch
                << '\n';
  }
  data.close(owner);
  if constexpr (allocator::has_accounting) {
    require(owner.state().live == 0, "live allocation remains after close");
    owner.trim();
    require(owner.state().mapped == 0, "payload backing remains after close/trim");
  }
}

template <class allocator> void cases(const char *name, const options &config) {
  std::vector<std::size_t> sizes{16, 64, 96};
  std::vector<history> histories{{1, 0}, {64, 0}, {1024, 0},
                               {64, 10}, {64, 90}, {1024, 10}, {1024, 90}};
  std::vector<cadence> policies{cadence::none};
  if constexpr (allocator::has_accounting)
    policies = {cadence::none, cadence::counters, cadence::trim};
  if (config.reverse) {
    std::reverse(sizes.begin(), sizes.end());
    std::reverse(histories.begin(), histories.end());
    std::reverse(policies.begin(), policies.end());
  }
  for (const auto bytes : sizes)
    for (const auto shape : histories)
      for (const auto policy : policies)
        run<allocator>(name, "ordinary_allocate", bytes, shape, policy, config);
}
template <class policy> void selected_cases(const char *name, const options &config) {
  std::array<std::size_t, 3> sizes{16, 64, 96};
  if (config.reverse)
    std::reverse(sizes.begin(), sizes.end());
  for (const auto bytes : sizes)
    run<descriptor<policy, true>>(name, "selected_grid", bytes, {1, 0},
                                 cadence::none, config);
}
} // namespace

int main(int argc, char **argv) {
  try {
    auto config = parse(argc, argv);
    if (config.check) {
      config.ms = 0.01;
      config.repetitions = 1;
    }
    // Keep libc in the same multithreaded-process mode as optional competitors.
    std::thread([] {}).join();
    if (!config.check)
      std::cout << "allocator,path,history,accounting,bytes,live,replace_percent,"
                   "pairs_per_batch,prepared_slots,rep,ns_per_pair,pairs,batches,"
                   "checksum,persistent_live_requested_bytes,peak_timed_requested_live_bytes,"
                   "footprint_measured,backing_bytes,policy_metadata_bytes,"
                   "reserved_va_bytes,control_bytes,mapped_payload_bytes,"
                   "cached_empty_payload_bytes,timed_map_calls,"
                   "timed_discarded_granules,report_sink,report_trim_scratch_bytes\n";
    std::cout << std::setprecision(10);
    if (!config.reverse) {
      cases<descriptor<frozen_policy>>("descriptor_frozen_v1", config);
      selected_cases<frozen_policy>("descriptor_frozen_v1", config);
      cases<descriptor<tuned_policy>>("descriptor_tuned", config);
      selected_cases<tuned_policy>("descriptor_tuned", config);
      cases<libc_allocator>("libc", config);
#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
      cases<mimalloc_allocator>("mimalloc_3.5.3", config);
      cases<snmalloc_allocator>("snmalloc_0.7.5", config);
#endif
      cases<free_list>("local_freelist", config);
      cases<bitmap>("local_bitmap", config);
    } else {
      cases<bitmap>("local_bitmap", config);
      cases<free_list>("local_freelist", config);
#ifdef DESCRIPTOR_ALLOCATOR_LIBRARIES
      cases<snmalloc_allocator>("snmalloc_0.7.5", config);
      cases<mimalloc_allocator>("mimalloc_3.5.3", config);
#endif
      cases<libc_allocator>("libc", config);
      selected_cases<tuned_policy>("descriptor_tuned", config);
      cases<descriptor<tuned_policy>>("descriptor_tuned", config);
      selected_cases<frozen_policy>("descriptor_frozen_v1", config);
      cases<descriptor<frozen_policy>>("descriptor_frozen_v1", config);
    }
    if (config.check)
      std::cout << "matched allocation/churn/accounting checks passed\n";
  } catch (const std::exception &failure) {
    std::cerr << "descriptor tuning benchmark: " << failure.what() << '\n';
    return 1;
  }
}
