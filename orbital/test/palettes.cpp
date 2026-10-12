#include "finite_qualification.h"
#include <orbital/allocation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

using namespace orbital;
using namespace orbital::allocation::detail;

namespace {
bool observe_allocations{};
std::array<std::size_t, 2> observed_allocations{};
std::size_t observed_count{};

void check(bool value, const char* reason,
    std::source_location where = std::source_location::current()) {
  if (value) return;
  std::fprintf(stderr, "palettes:%u: %s\n", where.line(), reason);
  std::abort();
}

void check_count(std::size_t actual, std::size_t expected, std::size_t page,
    const char* reason, std::source_location where = std::source_location::current()) {
  if (actual == expected) return;
  std::fprintf(stderr, "palettes:%u: %s (page=%zu, actual=%zu, expected=%zu)\n",
      where.line(), reason, page, actual, expected);
  std::abort();
}

template<class T> T take(Result<T> result, const char* reason) {
  check(bool(result), reason);
  return std::move(*result);
}

void done(Result<void> result, const char* reason) { check(bool(result), reason); }

void invalid(std::size_t native) {
  const auto rejected = [](FiniteConfig config) {
    const auto value = FiniteGroup::create(config);
    check(!value && value.error() == Error::invalid_argument, "invalid fixed palette geometry accepted");
  };
  rejected({static_cast<Palette>(42), 256});
  rejected({Palette::a, 48});
  const auto rejected_granule = [](std::size_t page) {
    const auto value = FiniteQualification::create({Palette::a,256}, page);
    check(!value && value.error() == Error::invalid_argument, "invalid qualification granule accepted");
  };
  rejected_granule(native + 1);
  if (native > 1) rejected_granule(native / 2);
  rejected_granule(std::size_t{1} << 30);
}

void last_page_vm(FiniteGroup& group, std::size_t page, std::size_t pages, std::size_t host) {
  check_count(group.native_page(), page, page, "qualified page granule");
  check_count(group.host_page(), host, page, "underlying host page granule");
  check_count(group.reserved_bytes(), pages * page + (page > host ? page : 0), page,
      "payload reservation with alignment slack");
  auto* first = group.page_address(0);
  auto* last = group.page_address(pages - 1);
  const auto base = reinterpret_cast<std::uintptr_t>(first);
  check_count(base % page, 0, page, "qualified payload alignment");
  check_count(reinterpret_cast<std::uintptr_t>(last) - base, (pages - 1) * page, page,
      "last payload page offset");
  check(!group.page_address(pages) && !group.mark_mapped({pages,1}),
      "qualified payload accepts a page beyond the reservation");

  // Touch every host page within the final geometry page. A wrong granule or
  // missing alignment slack must fail here, rather than pass counter-only checks.
  check(::mprotect(first, page, PROT_READ | PROT_WRITE) == 0 &&
      ::mprotect(last, page, PROT_READ | PROT_WRITE) == 0, "protect qualified reservation endpoints");
  std::memset(first, 0x31, page);
  std::memset(last, 0x5a, page);
  done(group.mark_mapped({0,1}), "publish first qualified page");
  done(group.mark_mapped({pages-1,1}), "publish last qualified page");
  check_count(group.mapped_pages(), 2, page, "qualified mapped-page charge");
  done(group.begin_trim({pages-1,1}), "reserve last qualified page discard");
  check(::madvise(last, page, MADV_DONTNEED) == 0 && ::mprotect(last, page, PROT_NONE) == 0,
      "discard/protect complete last qualified page");
  done(group.finish_trim({pages-1,1}, true), "publish qualified page discard");
  check_count(group.mapped_pages(), 1, page, "qualified discard refunds one geometry page");
  check(::mprotect(last, page, PROT_READ | PROT_WRITE) == 0, "reopen last qualified page");
  check(std::all_of(last,last+page,[](auto byte){return byte==std::byte{0};}) &&
      std::all_of(first,first+page,[](auto byte){return byte==std::byte{0x31};}),
      "qualified discard leaves nonzero host subpages or alters another page");
  done(group.mark_mapped({pages-1,1}), "publish reopened qualified page");
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
  struct Selection { std::size_t page, bytes, alignment; Palette expected; };
  const std::array<Selection,11> selections{{
      {4096,128,16,Palette::a}, {4096,192,16,Palette::a}, {4096,193,16,Palette::b},
      {4096,128,64,Palette::b}, {16384,128,16,Palette::a}, {16384,192,16,Palette::a},
      {16384,193,16,Palette::b}, {16384,128,64,Palette::b}, {65536,16,16,Palette::b},
      {65536,128,16,Palette::b}, {65536,192,16,Palette::b}}};
  for (const auto& example : selections)
    check(preferred_palette(example.page, example.bytes, example.alignment) == example.expected,
        "fresh Group selector does not implement the chosen page profile");
  struct Profile { std::size_t page, control, payload_pages; };
  // Each B class reserves 256 slots: pages = sum(ceil(256 * capacity / page)).
  // B capacities: 256,320,384,448,512,640,768,896,1024,1280,1536,1792,2048,2560,3072.
  // For 16/64 KiB, substitute 1088 and 2176 for 1024 and 2048.
  // Control = align8(256 bindings + 16 grids * 62 B + pages) + 3 * ceil(pages/64) * 8.
  // A grid has one identity mask and two sets of 15 class masks, all uint16_t;
  // page state is one byte; the three bitmaps are owner scratch, membership and success.
  // FiniteGroup itself is a separate charge, verified against the heap request below.
  const std::array profiles{
      Profile{4096,2776,1096},   // pages=sum(capacity/16); control=2344 + 3*18*8.
      Profile{16384,1648,277},   // pages=sum(capacity/64); control=align8(1525) + 3*5*8.
      Profile{65536,1376,73}};  // pages=sum(ceil(capacity/256)); control=align8(1321) + 3*2*8.
  for (const auto& profile : profiles) {
    const auto page = profile.page;
    if (page < host) continue;
    observed_count = 0;
    observe_allocations = true;
    auto geometry = take(FiniteQualification::create({Palette::b,256}, page),
        "create default B profile");
    done(geometry->prepare(), "prepare default B profile");
    observe_allocations = false;
    check_count(geometry->control_bytes(), profile.control, page, "default B control charge");
    check_count(observed_count, 2, page, "default B metadata heap block count");
    check_count(observed_allocations[0], sizeof(FiniteGroup), page, "FiniteGroup heap request");
    check_count(observed_allocations[1], profile.control, page, "default B control heap request");
    check(geometry->class_count() == 15 && geometry->slots_per_grid() == 16 &&
        geometry->capacity(8) == (page >= 16384 ? 1088 : 1024) &&
        geometry->capacity(12) == (page >= 16384 ? 2176 : 2048),
        "default B has the wrong large-page capacities or bit partition");
    check_count(geometry->payload_pages(), profile.payload_pages, page,
        "default B sparse reservation page count");
    check(geometry->capacity(take(geometry->class_for(1025), "classify lower shifted boundary")) ==
        (page >= 16384 ? 1088 : 1280) &&
        geometry->capacity(take(geometry->class_for(2050), "classify upper shifted boundary")) ==
        (page >= 16384 ? 2176 : 2560), "default B lookup ignores the shifted class boundaries");
    last_page_vm(*geometry, page, profile.payload_pages, host);
  }
  namespace alloc = orbital::allocation;
  struct Request { std::size_t bytes, alignment, expected_capacity; };
  const std::array requests{
      Request{128,16,host == 65536 ? 256u : 128u},
      Request{2050,16,host >= 16384 ? 2176u : 2560u},
      Request{1025,64,host >= 16384 ? 1088u : 1280u}};
  for (const auto& request : requests) {
    auto arena = take(alloc::open_arena(32 * 1024 * 1024), "open native profile Arena");
    worker::Lane lane;
    done(worker::adopt(lane), "adopt native profile owner");
    auto* pool = take(alloc::add_pool(*arena, lane), "add fresh native profile Pool");
    auto value = take(alloc::allocate(*pool, {request.bytes,0,request.alignment,0,
        alloc::Initialization::zero}), "allocate through native profile");
    check(alloc::capacity(value) == request.expected_capacity && value.size == request.bytes &&
        reinterpret_cast<std::uintptr_t>(value.address) % request.alignment == 0 &&
        (host != 65536 || reinterpret_cast<std::uintptr_t>(value.address) % 64 == 0),
        "public fresh allocation does not match native profile capacity/alignment");
    check(std::all_of(value.address,value.address+value.size,[](auto byte){return byte==std::byte{0};}),
        "native profile lost requested initialization");
    alloc::release(value);
    close_pool(*pool);
  }
}

std::uint64_t native_budget(std::uint64_t limit, bool pressure) {
  namespace alloc = orbital::allocation;
  const auto page = std::size_t(::sysconf(_SC_PAGESIZE));
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
      before.mapped_bytes == page && before.metadata_bytes > 0,
      "native metadata/page charge does not match physical inventory");
  if (pressure) {
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
  invalid(host);
  profiles(host);
  const auto charged = native_budget(128 * 1024 * 1024, false);
  check(native_budget(charged + host - 1, true) == charged, "native charge changed between identical fixtures");
  std::printf("palettes: native %zu-byte profile, class boundaries, alignment and exact budget checks passed\n", host);
}
