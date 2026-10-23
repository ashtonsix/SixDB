#include "finite_qualification.h"
#include <orbital/allocation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace orbital;
using namespace orbital::allocation::detail;

namespace {
bool observe_allocations{};
std::array<std::size_t, 2> observed_allocations{};
std::size_t observed_count{};

void check(bool value, const char* reason,
    std::source_location where = std::source_location::current()) {
  if (value) return;
  std::fprintf(stderr, "classes:%u: %s\n", where.line(), reason);
  std::abort();
}

void check_count(std::size_t actual, std::size_t expected, std::size_t page,
    const char* reason, std::source_location where = std::source_location::current()) {
  if (actual == expected) return;
  std::fprintf(stderr, "classes:%u: %s (page=%zu, actual=%zu, expected=%zu)\n",
      where.line(), reason, page, actual, expected);
  std::abort();
}

template<class T> T take(Result<T> result, const char* reason) {
  check(bool(result), reason);
  return std::move(*result);
}

void done(Result<void> result, const char* reason) { check(bool(result), reason); }

// Independent capacity oracle for public requests: smallest listed capacity
// covering the request, filling a native page with 256 Homes and preserving alignment.
std::size_t expected_capacity(std::size_t page, std::size_t bytes, std::size_t alignment) {
  constexpr std::array<std::size_t, 26> capacities{
      16,32,48,64,80,96,112,128,160,192,224,256,320,384,448,512,640,768,896,1024,1280,1536,1792,2048,2560,3072};
  for (auto capacity : capacities)
    if (capacity >= bytes && capacity * 256 >= page && capacity % alignment == 0) return capacity;
  return 0;
}

void last_page_vm(FiniteGroup& group, std::size_t page, std::size_t host) {
  const auto pages = group.payload_pages();
  check_count(group.native_page(), page, page, "qualified page granule");
  check_count(group.host_page(), host, page, "underlying host page granule");
  check_count(group.reserved_bytes(), pages * page + (page > host ? page : 0), page,
      "payload reservation with alignment slack");
  auto* first = group.page_address(0);
  auto* last = group.page_address(pages - 1);
  check_count(reinterpret_cast<std::uintptr_t>(first) % page, 0, page, "qualified payload alignment");
  check(!group.page_address(pages) && !group.mark_mapped({pages, 1}),
      "qualified payload accepts a page beyond the reservation");
  // Touch every host page within the final geometry page. A wrong granule or
  // missing alignment slack must fail here, rather than pass counter-only checks.
  check(::mprotect(first, page, PROT_READ | PROT_WRITE) == 0 &&
      ::mprotect(last, page, PROT_READ | PROT_WRITE) == 0, "protect qualified reservation endpoints");
  std::memset(first, 0x31, page);
  std::memset(last, 0x5a, page);
  done(group.mark_mapped({0, 1}), "publish first qualified page");
  done(group.mark_mapped({pages - 1, 1}), "publish last qualified page");
  check_count(group.mapped_pages(), pages == 1 ? 1 : 2, page, "qualified mapped-page charge");
  if (pages > 1) {
    auto membership = group.trim_membership();
    membership[(pages - 1) / 64] |= std::uint64_t{1} << ((pages - 1) % 64);
    done(group.begin_trim(), "reserve last qualified page discard");
    check(::madvise(last, page, MADV_DONTNEED) == 0 && ::mprotect(last, page, PROT_NONE) == 0,
        "discard/protect complete last qualified page");
    done(group.finish_trim({pages - 1, 1}, true), "publish qualified page discard");
    membership[(pages - 1) / 64] = 0;
    check_count(group.mapped_pages(), 1, page, "qualified discard refunds one geometry page");
    check(::mprotect(last, page, PROT_READ | PROT_WRITE) == 0, "reopen last qualified page");
    check(std::all_of(last, last + page, [](auto byte) { return byte == std::byte{0}; }) &&
        std::all_of(first, first + page, [](auto byte) { return byte == std::byte{0x31}; }),
        "qualified discard leaves nonzero host subpages or alters another page");
  }
  done(group.retire(), "retire qualified reservation");
}

void close_pool(orbital::allocation::Pool& pool) {
  namespace alloc = orbital::allocation;
  auto& lane = *pool.lane;
  auto& arena = *pool.arena;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    const auto result = alloc::close(pool);
    check(result.kind() != Progress::Kind::failed, "native profile close failed");
    if (result.kind() == Progress::Kind::done) break;
    check(std::chrono::steady_clock::now() < deadline, "native profile close timed out");
    worker::poll(lane, 1024);
    std::this_thread::yield();
  }
  check(arena.charged_bytes.load() == 0, "native profile retirement left charged storage");
  done(alloc::remove_pool(pool), "remove closed native profile Pool");
  worker::relinquish(lane);
}

void profiles(std::size_t host) {
  for (std::size_t page : {4096, 16384, 65536}) {
    if (page < host) continue;
    for (unsigned cls = first_class(page); cls < finite_classes; ++cls) {
      observed_count = 0;
      observe_allocations = true;
      auto geometry = take(FiniteQualification::create(cls, page), "create class profile");
      done(geometry->prepare(), "prepare class profile");
      observe_allocations = false;
      // Control = align8(pages) page states + three trim bitmaps of ceil(pages/64) words.
      const auto pages = (256 * finite_capacities[cls] + page - 1) / page;
      const auto control = (pages + 7) / 8 * 8 + 3 * ((pages + 63) / 64) * 8;
      check_count(geometry->payload_pages(), pages, page, "sparse reservation page count");
      check_count(geometry->control_bytes(), control, page, "control charge");
      check_count(observed_count, 2, page, "metadata heap block count");
      check_count(observed_allocations[0], sizeof(FiniteGroup), page, "FiniteGroup heap request");
      check_count(observed_allocations[1], control, page, "control heap request");
      last_page_vm(*geometry, page, host);
    }
  }
  namespace alloc = orbital::allocation;
  struct Request { std::size_t bytes, alignment; };
  const std::array requests{Request{128,16}, Request{48,32}, Request{64,64}, Request{128,128},
      Request{2050,16}, Request{1025,64}, Request{300,256}, Request{17,1024}};
  for (const auto& request : requests) {
    auto arena = take(alloc::open_arena(32 * 1024 * 1024), "open native profile Arena");
    worker::Lane lane;
    done(worker::adopt(lane), "adopt native profile owner");
    auto* pool = take(alloc::add_pool(*arena, lane), "add fresh native profile Pool");
    auto value = take(alloc::allocate(*pool, {request.bytes, 0, request.alignment, 0,
        alloc::Initialization::zero}), "allocate through native profile");
    check(alloc::capacity(value) == expected_capacity(host, request.bytes, request.alignment) &&
        value.size == request.bytes && reinterpret_cast<std::uintptr_t>(value.address) % request.alignment == 0,
        "public allocation does not match the class capacity/alignment oracle");
    check(std::all_of(value.address, value.address + value.size, [](auto byte) { return byte == std::byte{0}; }),
        "native profile lost requested initialization");
    alloc::release(value);
    close_pool(*pool);
  }
}

// Interleaved material/directory sizes fill their own class Groups. Mapping
// runs may map pages ahead; trim returns exactly those idle pages.
void mixed_placement(std::size_t page, bool directory_first) {
  namespace alloc = orbital::allocation;
  auto arena = take(alloc::open_arena(64 * 1024 * 1024), "open mixed placement Arena");
  worker::Lane lane;
  done(worker::adopt(lane), "adopt mixed placement owner");
  auto* pool = take(alloc::add_pool(*arena, lane), "add mixed placement Pool");
  constexpr std::size_t groups = 512, materials_per_group = 16;
  std::vector<alloc::Allocation> values;
  values.reserve(groups * (materials_per_group + 1) + 1);
  std::set<std::uintptr_t> covered_pages;
  const auto append = [&](std::size_t bytes) {
    auto value = take(alloc::allocate(*pool, {.bytes = bytes, .alignment = 64}),
        "allocate mixed material/directory");
    const auto capacity = alloc::capacity(value);
    check_count(capacity, expected_capacity(page, bytes, 64), page, "reuse changed the selected capacity");
    const auto address = reinterpret_cast<std::uintptr_t>(value.address);
    check(address % 64 == 0, "mixed placement lost explicit alignment");
    for (auto first = address / page, last = (address + capacity - 1) / page; first <= last; ++first)
      covered_pages.insert(first);
    std::memset(value.address, values.size() % 251, bytes);
    values.push_back(std::move(value));
  };
  if (directory_first) append(1088);
  for (std::size_t group = 0; group < groups; ++group) {
    for (std::size_t row = 0; row < materials_per_group; ++row) append(64);
    append(1088);
  }
  const auto mapped = alloc::statistics(*pool).mapped_bytes;
  check(mapped >= covered_pages.size() * page, "mapped accounting misses live pages");
  for (;;) {
    const auto progress = alloc::trim_cold(*pool, 0);
    check(progress.kind() != Progress::Kind::failed, "mixed placement trim failed");
    if (progress.kind() == Progress::Kind::done) break;
    worker::poll(lane, 1024);
    std::this_thread::yield();
  }
  const auto inventory = alloc::statistics(*pool);
  check_count(inventory.mapped_bytes, covered_pages.size() * page, page,
      "after trim, mapped accounting disagrees with the live capacity-page union");
  check_count(arena->charged_bytes.load(), inventory.metadata_bytes + inventory.mapped_bytes,
      page, "mixed placement lost native charge");
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto& value = values[index];
    check(std::all_of(value.address, value.address + value.size,
        [index](auto byte) { return byte == std::byte(index % 251); }),
        "later mixed placement overwrote live material/directory bytes");
  }
  values.clear();
  close_pool(*pool);
}

void ready_reuse(std::size_t page) {
  namespace alloc = orbital::allocation;
  auto arena = take(alloc::open_arena(1024 * 1024), "open ready reuse Arena");
  worker::Lane lane;
  done(worker::adopt(lane), "adopt ready reuse owner");
  auto* pool = take(alloc::add_pool(*arena, lane), "add ready reuse Pool");
  auto first = take(alloc::allocate(*pool, {.bytes = 200, .alignment = 64}), "map a Home");
  auto* address = first.address;
  alloc::release(first);
  const auto before = alloc::statistics(*pool);
  const auto charged = arena->charged_bytes.load();
  auto again = take(alloc::allocate(*pool, {.bytes = 256, .alignment = 64}), "reuse the same class");
  const auto after = alloc::statistics(*pool);
  check(again.address == address && before.mapped_bytes == after.mapped_bytes &&
      before.metadata_bytes == after.metadata_bytes, "same-class request did not reuse the ready Home");
  check_count(arena->charged_bytes.load(), charged, page, "ready reuse admitted new physical charge");
  alloc::release(again);
  close_pool(*pool);
}

// Pages mapped ahead of a Home are optional: a budget covering only the live
// Homes' own pages maps exactly those, and an ample budget maps runs ahead.
void ahead_under_pressure(std::size_t page) {
  namespace alloc = orbital::allocation;
  struct Outcome { alloc::Statistics inventory; std::size_t covered; };
  const auto run = [&](std::uint64_t limit) {
    auto arena = take(alloc::open_arena(limit), "open ahead Arena");
    worker::Lane lane;
    done(worker::adopt(lane), "adopt ahead owner");
    auto* pool = take(alloc::add_pool(*arena, lane), "add ahead Pool");
    std::vector<alloc::Allocation> values;
    std::set<std::uintptr_t> pages;
    // Enough 3072-byte Homes to need at least three native pages.
    for (std::size_t i = 0; i < 3 * page / 3072 + 1; ++i) {
      values.push_back(take(alloc::allocate(*pool, {3000, 0, 16}), "allocate large finite Homes"));
      const auto address = reinterpret_cast<std::uintptr_t>(values.back().address);
      for (auto p = address / page; p <= (address + 2999) / page; ++p) pages.insert(p);
      std::memset(values.back().address, 0x2c, 3000);
    }
    const auto inventory = alloc::statistics(*pool);
    check(arena->charged_bytes.load() == inventory.metadata_bytes + inventory.mapped_bytes,
        "ahead mapping lost exact charge");
    values.clear();
    close_pool(*pool);
    return Outcome{inventory, pages.size() * page};
  };
  const auto ample = run(64 * 1024 * 1024);
  check(ample.inventory.mapped_bytes > ample.covered, "ample budget did not map a run ahead of the Homes");
  const auto tight = run(ample.inventory.metadata_bytes + ample.covered);
  check_count(tight.inventory.mapped_bytes, tight.covered, page, "tight budget mapped beyond the live Homes' pages");
}

std::uint64_t native_budget(std::uint64_t limit, bool pressure) {
  namespace alloc = orbital::allocation;
  auto arena = take(alloc::open_arena(limit), "open native budget");
  worker::Lane lane;
  done(worker::adopt(lane), "adopt native budget owner");
  auto* pool = take(alloc::add_pool(*arena, lane), "add native budget Pool");
  auto value = take(alloc::allocate(*pool, {64, 0, 16}), "allocate native tiny bytes");
  std::memset(value.address, 0x73, value.size);
  auto retained = take(alloc::hold(value, {0, value.size}), "retain native allocation");
  alloc::release(value);
  const auto before = alloc::statistics(*pool);
  const auto initial_charge = arena->charged_bytes.load();
  check(arena->charged_bytes.load() == before.metadata_bytes + before.mapped_bytes &&
      before.mapped_bytes > 0 && before.metadata_bytes > 0,
      "native metadata/page charge does not match physical inventory");
  if (pressure) {
    const auto page = std::size_t(::sysconf(_SC_PAGESIZE));
    const auto refused = alloc::allocate(*pool, {page / 4 * 3, 0, 16});
    check(!refused && refused.error() == Error::capacity, "allocation admitted an unaffordable backing page");
    const auto after = alloc::statistics(*pool);
    check(arena->charged_bytes.load() <= limit &&
        arena->charged_bytes.load() == after.metadata_bytes + after.mapped_bytes &&
        retained.address[0] == std::byte{0x73} && retained.address[63] == std::byte{0x73},
        "budget refusal lost physical charge or retained bytes");
  }
  alloc::release(retained);
  close_pool(*pool);
  return initial_charge;
}
} // namespace

extern "C" void* __real__ZnwmRKSt9nothrow_t(std::size_t, const std::nothrow_t&) noexcept;
extern "C" void* __real__ZnwmSt11align_val_tRKSt9nothrow_t(
    std::size_t, std::align_val_t, const std::nothrow_t&) noexcept;
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(std::size_t bytes, const std::nothrow_t& tag) noexcept {
  if (observe_allocations) {
    check(observed_count < observed_allocations.size(), "unexpected nothrow metadata allocation");
    observed_allocations[observed_count++] = bytes;
  }
  return __real__ZnwmRKSt9nothrow_t(bytes, tag);
}

extern "C" void* __wrap__ZnwmSt11align_val_tRKSt9nothrow_t(
    std::size_t bytes, std::align_val_t alignment, const std::nothrow_t& tag) noexcept {
  if (observe_allocations) {
    check(observed_count < observed_allocations.size(), "unexpected aligned geometry allocation");
    observed_allocations[observed_count++] = bytes;
  }
  return __real__ZnwmSt11align_val_tRKSt9nothrow_t(bytes, alignment, tag);
}

int main() {
  const auto host = std::size_t(::sysconf(_SC_PAGESIZE));
  profiles(host);
  mixed_placement(host, false);
  mixed_placement(host, true);
  ready_reuse(host);
  ahead_under_pressure(host);
  const auto charged = native_budget(128 * 1024 * 1024, false);
  check(native_budget(charged + host - 1, true) == charged, "native charge changed between identical fixtures");
  std::printf("classes: native %zu-byte profile, class capacities, alignment and exact budget checks passed\n", host);
}
