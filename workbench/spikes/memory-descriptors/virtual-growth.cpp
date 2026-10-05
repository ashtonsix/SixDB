#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
constexpr std::size_t kib = 1024, mib = 1024 * kib;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
std::size_t round_up(std::size_t bytes, std::size_t unit) {
  return ((bytes + unit - 1) / unit) * unit;
}
enum class placement { stable_window, copy_binding, remap_binding };
enum class capacity_policy { exact_pages, geometric };
struct candidate {
  placement kind;
  capacity_policy capacity;
};
const char *placement_name(placement value) {
  switch (value) {
  case placement::stable_window: return "stable_window";
  case placement::copy_binding: return "copy_binding";
  case placement::remap_binding: return "mremap_binding";
  }
  std::terminate();
}
const char *capacity_name(capacity_policy value) {
  return value == capacity_policy::exact_pages ? "exact_pages" : "geometric";
}
std::vector<candidate> candidates() {
  std::vector<candidate> result{
      {placement::stable_window, capacity_policy::exact_pages},
      {placement::stable_window, capacity_policy::geometric},
      {placement::copy_binding, capacity_policy::exact_pages},
      {placement::copy_binding, capacity_policy::geometric}};
#ifdef __linux__
  result.push_back({placement::remap_binding, capacity_policy::exact_pages});
  result.push_back({placement::remap_binding, capacity_policy::geometric});
#endif
  return result;
}

struct counters {
  std::size_t current_va{}, current_mapped{}, peak_va{}, peak_mapped{};
  std::size_t mmap_calls{}, mprotect_calls{}, mremap_calls{}, munmap_calls{};
  std::size_t page_touches{}, copied_bytes{}, initialized_bytes{}, growth_calls{};
  std::size_t moves{}, held_copy_fallbacks{}, blocked_growths{};
  void peak() {
    peak_va = std::max(peak_va, current_va);
    peak_mapped = std::max(peak_mapped, current_mapped);
  }
};
void release_span(counters &counts, std::byte *address, std::size_t reserved,
                  std::size_t mapped) {
  ++counts.munmap_calls;
  if (munmap(address, reserved) != 0)
    std::abort();
  counts.current_va -= reserved;
  counts.current_mapped -= mapped;
}
/// Own raw pages before allocating their metadata. Guarded setup can transfer
/// one prefix at a time, leaving every remaining byte under this cleanup owner.
struct pending_mapping {
  std::shared_ptr<counters> counts;
  std::byte *address;
  std::size_t reserved, mapped;
  pending_mapping(std::shared_ptr<counters> record, std::byte *base,
                  std::size_t va, std::size_t prepared)
      : counts(std::move(record)), address(base), reserved(va), mapped(prepared) {
    counts->current_va += reserved;
    counts->current_mapped += mapped;
    counts->peak();
  }
  ~pending_mapping() {
    if (address)
      release_span(*counts, address, reserved, mapped);
  }
  void transfer() { address = nullptr; }
  void transfer_prefix(std::size_t bytes) {
    assert(!mapped && bytes < reserved);
    address += bytes;
    reserved -= bytes;
  }
};
struct mapping {
  std::shared_ptr<counters> counts;
  std::byte *address;
  std::size_t reserved, mapped;
  std::size_t borrows{}, max_admitted{};
  std::vector<std::size_t> admitted_windows;
  static constexpr std::size_t inactive = static_cast<std::size_t>(-1);
  mapping(std::shared_ptr<counters> record, std::byte *base, std::size_t va,
          std::size_t prepared)
      : counts(std::move(record)), address(base), reserved(va), mapped(prepared) {}
  ~mapping() {
    release_span(*counts, address, reserved, mapped);
  }
  std::size_t admit(std::size_t extent) {
    auto vacant = std::find(admitted_windows.begin(), admitted_windows.end(), inactive);
    const auto index = static_cast<std::size_t>(vacant - admitted_windows.begin());
    if (vacant == admitted_windows.end())
      admitted_windows.push_back(extent);
    else
      *vacant = extent;
    ++borrows;
    max_admitted = std::max(max_admitted, extent);
    return index;
  }
  void complete(std::size_t index) {
    assert(index < admitted_windows.size() && admitted_windows[index] != inactive);
    const auto previous = admitted_windows[index];
    admitted_windows[index] = inactive;
    if (!--borrows) {
      admitted_windows.clear();
      max_admitted = 0;
    } else if (previous == max_admitted) {
      max_admitted = 0;
      for (const auto extent : admitted_windows)
        if (extent != inactive)
          max_admitted = std::max(max_admitted, extent);
    }
  }
};
class grant {
public:
  grant(std::shared_ptr<mapping> backing, std::size_t initialized,
        std::size_t admitted)
      : backing_(std::move(backing)), initialized_(initialized), admitted_(admitted) {
    token_ = backing_->admit(admitted);
  }
  grant(grant &&other) noexcept
      : backing_(std::move(other.backing_)), initialized_(other.initialized_),
        admitted_(other.admitted_), token_(other.token_) {}
  grant(const grant &) = delete;
  grant &operator=(const grant &) = delete;
  ~grant() { complete(); }
  const std::byte *data() const { return backing_->address; }
  std::size_t initialized() const { return initialized_; }
  std::size_t admitted() const { return admitted_; }
  void complete() {
    if (backing_) {
      backing_->complete(token_);
      backing_.reset();
    }
  }

private:
  std::shared_ptr<mapping> backing_;
  std::size_t initialized_, admitted_, token_;
};

std::byte byte_value(std::size_t object, std::size_t offset) {
  return std::byte((object * 37 + (offset / 4) * 19 + (offset % 4) * 71) & 255);
}
void initialize(std::byte *data, std::size_t object, std::size_t begin,
                std::size_t end) {
  for (auto offset = begin; offset < end; ++offset)
    data[offset] = byte_value(object, offset);
  asm volatile("" : : "r"(data) : "memory");
}
std::uint64_t verify(const std::byte *data, std::size_t object, std::size_t bytes) {
  std::uint64_t checksum = 0;
  for (std::size_t offset = 0; offset < bytes; ++offset) {
    require(data[offset] == byte_value(object, offset),
            "grown prefix corrupted or uninitialized");
    checksum += std::to_integer<unsigned char>(data[offset]);
  }
  return checksum;
}

/// A stable test identity is an index into this owner, independent of payload
/// binding. Owner operations are serialized. Grants model admitted raw reads,
/// not concurrent admission/publication or a production descriptor format.
/// Movable bindings need indirection or a unique caller updating its direct
/// head; they are not interchangeable with shared palette-C direct heads.
class objects {
public:
  objects(candidate choice, std::size_t count, std::size_t maximum = 2 * mib)
      : choice_(choice), counts_(std::make_shared<counters>()), records_(count),
        page_(static_cast<std::size_t>(sysconf(_SC_PAGESIZE))),
        maximum_(round_up(maximum, page_)) {}
  std::size_t page() const { return page_; }
  std::size_t count() const { return records_.size(); }
  counters stats() const { return *counts_; }
  std::size_t initialized(std::size_t identity) const {
    return records_.at(identity).initialized;
  }
  std::size_t capacity(std::size_t identity) const {
    const auto &object = records_.at(identity);
    return object.backing ? object.backing->mapped : 0;
  }
  const std::byte *data(std::size_t identity) const {
    return records_.at(identity).backing->address;
  }
  std::size_t initialized_total() const {
    std::size_t result = 0;
    for (const auto &object : records_)
      result += object.initialized;
    return result;
  }
  std::size_t capacity_total() const {
    std::size_t result = 0;
    for (const auto &object : records_)
      if (object.backing)
        result += object.backing->mapped;
    return result;
  }
  std::size_t retained_mapped() const {
    return counts_->current_mapped - capacity_total();
  }
  grant borrow(std::size_t identity, std::size_t admitted = 0) {
    const auto &object = records_.at(identity);
    require(object.backing != nullptr, "borrow of absent object");
    if (!admitted)
      admitted = object.initialized;
    require(admitted <= object.backing->mapped,
            "admitted window exceeds prepared capacity");
    return grant(object.backing, std::min(object.initialized, admitted), admitted);
  }
  void guard_initial_mapping(std::size_t identity) {
    auto &object = records_.at(identity);
    require(choice_.kind == placement::remap_binding && !object.backing,
            "growth guard requires an absent mremap object");
    object.guard_initial = true;
  }
  void fail_metadata_allocation(std::size_t ordinal) { failure_at_ = ordinal; }
  std::pair<const std::byte *, std::size_t> last_raw_span() const {
    return {last_raw_address_, last_raw_bytes_};
  }
  bool grow(std::size_t identity, std::size_t requested) {
    auto &object = records_.at(identity);
    require(requested >= object.initialized, "growth cannot shrink readable prefix");
    if (requested == object.initialized)
      return true;
    ++counts_->growth_calls;
    if (requested > maximum_) {
      ++counts_->blocked_growths;
      return false;
    }
    const auto needed = round_up(requested, page_);
    if (!object.backing) {
      if (choice_.kind == placement::stable_window)
        object.backing = reserve(maximum_);
      else if (object.guard_initial)
        object.backing = allocate_with_guard(needed, object.guard);
      else
        object.backing = allocate(needed);
    }
    auto &old = object.backing;
    const bool overlap = old->borrows && old->max_admitted > object.initialized;
    auto next_capacity = std::max(needed, old->mapped);
    if (needed > old->mapped && choice_.capacity == capacity_policy::geometric)
      next_capacity = std::min(maximum_, std::max(needed, old->mapped * 2));
    if (choice_.kind == placement::stable_window) {
      if (overlap) {
        ++counts_->blocked_growths;
        return false;
      }
      if (needed > old->mapped) {
        ++counts_->mprotect_calls;
        require(mprotect(old->address + old->mapped, next_capacity - old->mapped,
                         PROT_READ | PROT_WRITE) == 0,
                "window preparation failed");
        prepare_pages(old->address, old->mapped, next_capacity);
        counts_->current_mapped += next_capacity - old->mapped;
        old->mapped = next_capacity;
        counts_->peak();
      }
    } else if (needed > old->mapped || overlap) {
      if (choice_.kind == placement::remap_binding && !old->borrows) {
#ifdef __linux__
        const auto previous = old->address;
        const auto previous_capacity = old->mapped;
        ++counts_->mremap_calls;
        void *next = mremap(previous, previous_capacity, next_capacity,
                            MREMAP_MAYMOVE);
        require(next != MAP_FAILED, "mremap growth failed");
        old->address = static_cast<std::byte *>(next);
        old->reserved = next_capacity;
        old->mapped = next_capacity;
        counts_->current_va += next_capacity - previous_capacity;
        counts_->current_mapped += next_capacity - previous_capacity;
        counts_->moves += old->address != previous;
        counts_->peak();
        prepare_pages(old->address, previous_capacity, next_capacity);
#else
        throw std::runtime_error("mremap is unavailable on this platform");
#endif
      } else {
        if (choice_.kind == placement::remap_binding && old->borrows)
          ++counts_->held_copy_fallbacks;
        auto next = allocate(next_capacity);
        std::memcpy(next->address, old->address, object.initialized);
        counts_->copied_bytes += object.initialized;
        ++counts_->moves;
        object.backing = std::move(next);
      }
    }
    // Logical readability publishes only after backing and appended bytes exist.
    initialize(object.backing->address, identity, object.initialized, requested);
    counts_->initialized_bytes += requested - object.initialized;
    object.initialized = requested;
    return true;
  }
  /// Used only by the within-page benchmark after all byte users end. Existing
  /// prepared bytes remain mapped; subsequent append still initializes its tail.
  void rewind(std::size_t identity, std::size_t prefix) {
    auto &object = records_.at(identity);
    require(object.backing && !object.backing->borrows &&
                prefix <= object.initialized,
            "rewind requires quiescent initialized storage");
    object.initialized = prefix;
  }
  void release(std::size_t identity) {
    auto &object = records_.at(identity);
    object.initialized = 0;
    object.backing.reset();
    object.guard.reset();
  }
  void close() {
    for (std::size_t identity = 0; identity < count(); ++identity)
      release(identity);
  }

private:
  struct object {
    std::shared_ptr<mapping> backing, guard;
    std::size_t initialized{};
    bool guard_initial{};
  };
  candidate choice_;
  std::shared_ptr<counters> counts_;
  std::vector<object> records_;
  std::size_t page_, maximum_;
  std::size_t failure_at_{};
  std::byte *last_raw_address_{};
  std::size_t last_raw_bytes_{};
  void metadata_allocation() {
    if (failure_at_ && !--failure_at_)
      throw std::bad_alloc();
  }
  std::shared_ptr<mapping> map(std::size_t reserved, int protection,
                             std::size_t prepared) {
    ++counts_->mmap_calls;
    void *address = mmap(nullptr, reserved, protection,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    require(address != MAP_FAILED, "virtual backing reservation failed");
    last_raw_address_ = static_cast<std::byte *>(address);
    last_raw_bytes_ = reserved;
    pending_mapping pending(counts_, last_raw_address_, reserved, prepared);
    metadata_allocation();
    auto result = std::make_shared<mapping>(counts_, pending.address,
                                             reserved, prepared);
    pending.transfer();
    return result;
  }
  void prepare_pages(std::byte *address, std::size_t begin, std::size_t end) {
    for (auto offset = begin; offset < end; offset += page_) {
      *reinterpret_cast<volatile std::byte *>(address + offset) = std::byte{0};
      ++counts_->page_touches;
    }
  }
  std::shared_ptr<mapping> reserve(std::size_t bytes) {
    return map(bytes, PROT_NONE, 0);
  }
  std::shared_ptr<mapping> allocate(std::size_t bytes) {
    auto result = map(bytes, PROT_READ | PROT_WRITE, bytes);
    prepare_pages(result->address, 0, bytes);
    return result;
  }
  std::shared_ptr<mapping> allocate_with_guard(
      std::size_t bytes, std::shared_ptr<mapping> &guard) {
    ++counts_->mmap_calls;
    void *address = mmap(nullptr, bytes + page_, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    require(address != MAP_FAILED, "guarded backing reservation failed");
    auto *base = static_cast<std::byte *>(address);
    last_raw_address_ = base;
    last_raw_bytes_ = bytes + page_;
    pending_mapping pending(counts_, base, bytes + page_, 0);
    metadata_allocation();
    auto result = std::make_shared<mapping>(counts_, base, bytes, 0);
    pending.transfer_prefix(bytes);
    metadata_allocation();
    auto next_guard = std::make_shared<mapping>(counts_, base + bytes, page_, 0);
    pending.transfer();
    ++counts_->mprotect_calls;
    require(mprotect(base, bytes, PROT_READ | PROT_WRITE) == 0,
            "guarded initial preparation failed");
    result->mapped = bytes;
    counts_->current_mapped += bytes;
    counts_->peak();
    prepare_pages(base, 0, bytes);
    guard = std::move(next_guard);
    return result;
  }
};

void require_unmapped(const objects &memory) {
  const auto [address, bytes] = memory.last_raw_span();
  unsigned char resident;
  for (std::size_t offset = 0; offset < bytes; offset += memory.page()) {
    errno = 0;
    require(mincore(const_cast<std::byte *>(address + offset), memory.page(),
                    &resident) == -1 && errno == ENOMEM,
            "failed metadata adoption left an OS mapping behind");
  }
}

void native_checks() {
  std::size_t checked = 0;
  for (const auto choice : candidates()) {
    objects memory(choice, 3, mib);
    const auto page = memory.page();
    for (std::size_t identity = 0; identity < 3; ++identity)
      require(memory.grow(identity, kib + identity * 7), "initial growth failed");
    const auto neighbor = memory.data(1);
    const auto neighbor_capacity = memory.capacity(1);
    for (const auto size : {page - 1, page, page + 1, page * 3 - 1,
                           page * 3, page * 3 + 1, mib}) {
      require(memory.grow(0, size), "near-page or multi-page growth failed");
      verify(memory.data(0), 0, size);
      require(memory.data(1) == neighbor && memory.capacity(1) == neighbor_capacity,
              "independent growth moved another allocation");
      verify(memory.data(1), 1, kib + 7);
      verify(memory.data(2), 2, kib + 14);
      ++checked;
    }
    const auto before = memory.data(0);
    require(!memory.grow(0, mib + 1) && memory.data(0) == before &&
                memory.initialized(0) == mib,
            "maximum failure changed the object binding");
    verify(memory.data(0), 0, mib);
    verify(memory.data(1), 1, kib + 7);
    memory.close();
    require(memory.stats().current_va == 0 && memory.stats().current_mapped == 0,
            "unborrowed mappings remain after close");

    objects held(choice, 1, mib);
    require(held.grow(0, 47), "held initial growth failed");
    auto padded = held.borrow(0, 64);
    std::array<std::byte, 64> image;
    std::memcpy(image.data(), padded.data(), image.size());
    const auto old_address = padded.data();
    const bool grew = held.grow(0, 50);
    if (choice.kind == placement::stable_window)
      require(!grew && held.initialized(0) == 47,
              "stable append wrote within an admitted old window");
    else
      require(grew && held.data(0) != old_address && held.retained_mapped() == page,
              "movable append did not retain the admitted old image");
    require(padded.initialized() == 47 && padded.admitted() == 64 &&
                std::memcmp(padded.data(), image.data(), image.size()) == 0,
            "held padding or initialized prefix changed");
    padded.complete();
    require(held.retained_mapped() == 0, "completed old grant still retains backing");
    if (!grew)
      require(held.grow(0, 50), "released padded window still blocks growth");
    auto prefix = held.borrow(0, 50);
    const auto prefix_address = prefix.data();
    require(held.grow(0, page + 1), "append beyond admitted prefix failed");
    verify(prefix.data(), 0, 50);
    verify(held.data(0), 0, page + 1);
    if (choice.kind == placement::stable_window)
      require(held.data(0) == prefix_address && held.retained_mapped() == 0,
              "stable growth changed the raw prefix binding");
    else
      require(held.data(0) != prefix_address && held.retained_mapped() == page,
              "borrowed old prefix was not retained across growth");
    held.release(0);
    verify(prefix.data(), 0, 50);
    require(held.stats().current_mapped != 0,
            "identity release revoked a held raw view");
    prefix.complete();
    require(held.stats().current_va == 0 && held.stats().current_mapped == 0,
            "last completion did not release retained backing");
    checked += 5;

    objects ordered(choice, 1, mib);
    require(ordered.grow(0, 47), "out-of-order grant setup failed");
    auto wide = ordered.borrow(0, 64);
    auto narrow = ordered.borrow(0, 47);
    const auto binding = ordered.data(0);
    wide.complete();
    require(ordered.grow(0, 50) && ordered.data(0) == binding,
            "completed wider grant still causes blocking or relocation");
    verify(narrow.data(), 0, narrow.initialized());
    narrow.complete();
    auto partial = ordered.borrow(0, 16);
    require(partial.admitted() == 16 && partial.initialized() == 16 &&
                ordered.grow(0, 51) && ordered.data(0) == binding,
            "partial grant exposes bytes beyond its admitted window");
    verify(partial.data(), 0, 16);
    ordered.release(0);
    partial.complete();
    require(ordered.stats().current_va == 0,
            "out-of-order grant completion leaked backing");
    checked += 2;

    objects failed(choice, 2);
    require(failed.grow(1, 47), "metadata failure neighbor setup failed");
    const auto neighbor_before = failed.data(1);
    const auto state_before = failed.stats();
    failed.fail_metadata_allocation(1);
    bool rejected = false;
    try {
      failed.grow(0, kib);
    } catch (const std::bad_alloc &) {
      rejected = true;
    }
    require(rejected && failed.initialized(0) == 0 && failed.capacity(0) == 0 &&
                failed.stats().current_va == state_before.current_va &&
                failed.stats().current_mapped == state_before.current_mapped &&
                failed.data(1) == neighbor_before,
            "metadata failure changed an existing binding or leaked ownership");
    require_unmapped(failed);
    verify(failed.data(1), 1, 47);
    require(failed.grow(0, kib), "metadata failure did not permit retry");
    failed.close();
    ++checked;
  }
#ifdef __linux__
  objects forced({placement::remap_binding, capacity_policy::exact_pages}, 2);
  forced.guard_initial_mapping(0);
  require(forced.grow(0, kib) && forced.grow(1, kib + 1),
          "forced-remap setup failed");
  const auto previous = forced.data(0);
  const auto neighbor = forced.data(1);
  require(forced.grow(0, forced.page() + 1) && forced.data(0) != previous &&
              forced.stats().mremap_calls == 1 && forced.stats().moves == 1,
          "owned guard did not force mremap movement");
  verify(forced.data(0), 0, forced.page() + 1);
  require(forced.data(1) == neighbor, "forced remap moved an unrelated object");
  verify(forced.data(1), 1, kib + 1);
  forced.close();
  require(forced.stats().current_va == 0 && forced.stats().current_mapped == 0,
          "forced-remap guard leaked");
  ++checked;
  for (const auto ordinal : {1U, 2U}) {
    objects failed({placement::remap_binding, capacity_policy::exact_pages}, 1);
    failed.guard_initial_mapping(0);
    failed.fail_metadata_allocation(ordinal);
    bool rejected = false;
    try {
      failed.grow(0, kib);
    } catch (const std::bad_alloc &) {
      rejected = true;
    }
    require(rejected && failed.initialized(0) == 0 && failed.capacity(0) == 0 &&
                failed.stats().current_va == 0 && failed.stats().current_mapped == 0,
            "partial guarded adoption leaked a mapping");
    require_unmapped(failed);
    require(failed.grow(0, kib), "guarded metadata failure did not permit retry");
    failed.close();
    ++checked;
  }
#endif
  std::cout << "virtual growth checks passed: cases=" << checked
            << " page_bytes=" << static_cast<std::size_t>(sysconf(_SC_PAGESIZE))
            << " mremap_available="
#ifdef __linux__
            << 1
#else
            << 0
#endif
            << '\n';
}

struct options {
  double ms{20};
  unsigned repetitions{3};
  bool check{}, reverse{};
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
      throw std::invalid_argument("unknown or incomplete virtual-growth option");
  }
  require(std::isfinite(result.ms) && result.ms > 0 && result.repetitions,
          "positive duration and repetitions required");
  return result;
}
struct usage {
  long minor{}, major{};
};
usage process_faults() {
  rusage result;
  require(getrusage(RUSAGE_SELF, &result) == 0, "process fault counters unavailable");
  return {result.ru_minflt, result.ru_majflt};
}
struct event {
  std::size_t identity, requested;
};
std::vector<event> growth_history(std::size_t count) {
  constexpr std::array<std::size_t, 22> stages{
      kib, kib + 1, 2048, 4095, 4096, 4097, 8193, 12289, 16385, 32769, 49153,
      65535, 65536, 65537, 131073, 196609, 262143, 262144, 262145, 524289,
      786433, mib};
  std::vector<std::size_t> next(count);
  std::vector<event> result;
  std::uint64_t entropy = 0x243f6a8885a308d3ULL;
  while (result.size() < count * stages.size()) {
    entropy ^= entropy << 13;
    entropy ^= entropy >> 7;
    entropy ^= entropy << 17;
    auto identity = entropy % count;
    while (next[identity] == stages.size())
      identity = (identity + 1) % count;
    result.push_back({identity, stages[next[identity]++]});
  }
  return result;
}
struct measurements {
  double ready_ns{};
  std::size_t cycles{}, growths{};
  std::uint64_t checksum{};
  counters totals;
  long minor{}, major{};
  std::size_t initialized{}, capacity{}, retained{}, window{};
};
void accumulate(measurements &result, const counters &before, const counters &after) {
  result.totals.current_va = after.current_va;
  result.totals.current_mapped = after.current_mapped;
  result.totals.peak_va = std::max(result.totals.peak_va, after.peak_va);
  result.totals.peak_mapped = std::max(result.totals.peak_mapped, after.peak_mapped);
  result.totals.mmap_calls += after.mmap_calls - before.mmap_calls;
  result.totals.mprotect_calls += after.mprotect_calls - before.mprotect_calls;
  result.totals.mremap_calls += after.mremap_calls - before.mremap_calls;
  result.totals.munmap_calls += after.munmap_calls - before.munmap_calls;
  result.totals.page_touches += after.page_touches - before.page_touches;
  result.totals.copied_bytes += after.copied_bytes - before.copied_bytes;
  result.totals.initialized_bytes += after.initialized_bytes - before.initialized_bytes;
  result.totals.growth_calls += after.growth_calls - before.growth_calls;
  result.totals.moves += after.moves - before.moves;
  result.totals.held_copy_fallbacks += after.held_copy_fallbacks - before.held_copy_fallbacks;
  result.totals.blocked_growths += after.blocked_growths - before.blocked_growths;
}
void final_state(measurements &result, const objects &memory) {
  result.initialized = memory.initialized_total();
  result.capacity = memory.capacity_total();
  result.retained = memory.retained_mapped();
  for (std::size_t identity = 0; identity < memory.count(); ++identity)
    result.checksum += verify(memory.data(identity), identity,
                              memory.initialized(identity));
}
measurements interleaved(candidate choice, const options &config) {
  constexpr std::size_t count = 16, window = 2 * mib;
  const auto events = growth_history(count);
  measurements result;
  result.window = choice.kind == placement::stable_window ? window : 0;
  do {
    objects memory(choice, count, window);
    const auto before = memory.stats();
    const auto faults_before = process_faults();
    const auto start = clock_type::now();
    for (const auto item : events)
      require(memory.grow(item.identity, item.requested), "timed growth blocked");
    const auto finish = clock_type::now();
    const auto faults_after = process_faults();
    result.ready_ns += std::chrono::duration<double, std::nano>(finish - start).count();
    result.minor += faults_after.minor - faults_before.minor;
    result.major += faults_after.major - faults_before.major;
    accumulate(result, before, memory.stats());
    final_state(result, memory);
    require(result.initialized == count * mib && result.capacity == count * mib &&
                !result.retained,
            "interleaved candidates reached different end states");
    result.growths += events.size();
    ++result.cycles;
    memory.close();
    require(memory.stats().current_va == 0, "timed interleaved close leaked VA");
  } while (result.ready_ns < config.ms * 1e6);
  require(result.totals.initialized_bytes == result.cycles * count * mib,
          "interleaved initialization accounting mismatch");
  require(result.totals.growth_calls == result.growths,
          "interleaved growth-call accounting mismatch");
  return result;
}
measurements within_page(candidate choice, const options &config) {
  constexpr std::size_t count = 64, base = kib, steps = 64, append = 16;
  objects memory(choice, count);
  for (std::size_t identity = 0; identity < count; ++identity)
    require(memory.grow(identity, base), "within-page preparation blocked");
  measurements result;
  result.window = choice.kind == placement::stable_window ? 2 * mib : 0;
  do {
    for (std::size_t identity = 0; identity < count; ++identity)
      memory.rewind(identity, base);
    const auto before = memory.stats();
    const auto faults_before = process_faults();
    const auto start = clock_type::now();
    for (std::size_t step = 1; step <= steps; ++step)
      for (std::size_t offset = 0; offset < count; ++offset) {
        const auto identity = (offset + step) % count;
        require(memory.grow(identity, base + step * append),
                "within-page append blocked");
      }
    const auto finish = clock_type::now();
    const auto faults_after = process_faults();
    result.ready_ns += std::chrono::duration<double, std::nano>(finish - start).count();
    result.minor += faults_after.minor - faults_before.minor;
    result.major += faults_after.major - faults_before.major;
    const auto after = memory.stats();
    require(after.mmap_calls == before.mmap_calls &&
                after.mprotect_calls == before.mprotect_calls &&
                after.mremap_calls == before.mremap_calls &&
                after.copied_bytes == before.copied_bytes,
            "within-page append unexpectedly changes backing");
    accumulate(result, before, after);
    final_state(result, memory);
    result.growths += count * steps;
    ++result.cycles;
  } while (result.ready_ns < config.ms * 1e6);
  require(result.totals.initialized_bytes == result.cycles * count * steps * append,
          "within-page initialization accounting mismatch");
  require(result.totals.growth_calls == result.growths,
          "within-page growth-call accounting mismatch");
  memory.close();
  require(memory.stats().current_va == 0, "within-page close leaked VA");
  return result;
}
void report(candidate choice, const char *history, unsigned repetition,
            const measurements &result) {
  const auto &counts = result.totals;
  std::cout << placement_name(choice.kind) << ',' << capacity_name(choice.capacity)
            << ',' << history << ',' << repetition << ','
            << static_cast<std::size_t>(sysconf(_SC_PAGESIZE)) << ','
            << result.window << ',' << result.ready_ns / result.growths << ','
            << result.ready_ns / result.cycles << ',' << result.cycles << ','
            << result.growths << ',' << result.checksum << ',' << result.initialized
            << ',' << result.initialized << ',' << result.capacity << ','
            << counts.current_va << ',' << counts.current_mapped << ','
            << result.retained << ',' << counts.peak_va << ',' << counts.peak_mapped
            << ',' << counts.copied_bytes << ',' << counts.initialized_bytes << ','
            << counts.page_touches << ',' << counts.mmap_calls << ','
            << counts.mprotect_calls << ',' << counts.mremap_calls << ','
            << counts.munmap_calls << ',' << counts.moves << ','
            << counts.held_copy_fallbacks << ',' << result.minor << ',' << result.major
            << '\n';
}
} // namespace

int main(int argc, char **argv) {
  try {
    const auto config = parse(argc, argv);
    if (config.check) {
      native_checks();
      return 0;
    }
    auto choices = candidates();
    if (config.reverse)
      std::reverse(choices.begin(), choices.end());
    std::cout << "placement,capacity_policy,history,rep,native_page_bytes,"
                 "test_window_bytes,ready_ns_per_growth,ready_ns_per_cycle,cycles,"
                 "growth_calls,checksum,requested_bytes,logical_readable_bytes,"
                 "prepared_capacity_bytes,current_reserved_va_bytes,current_rw_mapped_bytes,"
                 "retained_old_mapped_bytes,peak_reserved_va_bytes,peak_rw_mapped_bytes,"
                 "copied_bytes,appended_initialized_bytes,explicit_page_touches,"
                 "timed_mmap_calls,timed_mprotect_calls,timed_mremap_calls,"
                 "timed_munmap_calls,payload_moves,held_copy_fallbacks,"
                 "process_minor_faults,process_major_faults\n";
    std::cout << std::setprecision(10);
    for (const auto choice : choices)
      for (unsigned repetition = 0; repetition < config.repetitions; ++repetition) {
        if (!config.reverse) {
          report(choice, "interleaved_16x1MiB", repetition, interleaved(choice, config));
          report(choice, "within_page_64x16B_append", repetition, within_page(choice, config));
        } else {
          report(choice, "within_page_64x16B_append", repetition, within_page(choice, config));
          report(choice, "interleaved_16x1MiB", repetition, interleaved(choice, config));
        }
      }
  } catch (const std::exception &failure) {
    std::cerr << "virtual growth probe: " << failure.what() << '\n';
    return 1;
  }
}
