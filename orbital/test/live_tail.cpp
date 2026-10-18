#include <orbital/allocation.h>
#include <orbital/instantiate/representation.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using namespace orbital;
namespace alloc = orbital::allocation;
using Clock = std::chrono::steady_clock;
constexpr auto timeout = std::chrono::seconds(10);

void check(bool condition, const char* reason,
    std::source_location where = std::source_location::current()) {
  if (condition) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), reason);
  std::abort();
}

template<class T> T take(Result<T> result,
    std::source_location where = std::source_location::current()) {
  check(result.has_value(), "unexpected operation refusal", where);
  return std::move(*result);
}

template<class Predicate> void await(Predicate ready, const char* reason) {
  const auto deadline = Clock::now() + timeout;
  while (!ready()) {
    check(Clock::now() < deadline, reason);
    std::this_thread::yield();
  }
}

std::size_t page_size() { return static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)); }

std::byte pattern(std::size_t offset) { return std::byte((offset * 29 + 41) % 251); }

void fill(alloc::Allocation& value) {
  for (std::size_t offset = 0; offset < value.size; ++offset) value.address[offset] = pattern(offset);
}

void verify(const std::byte* address, std::size_t amount, std::size_t offset = 0) {
  for (std::size_t index = 0; index < amount; ++index)
    check(address[index] == pattern(index + offset), "retained bytes changed");
}

std::size_t resident(std::byte* address, std::size_t amount) {
  check(amount % page_size() == 0, "unaligned residency query");
  std::vector<unsigned char> pages(amount / page_size());
  check(::mincore(address, amount, pages.data()) == 0, "retained VA was unmapped");
  std::size_t count{};
  for (const auto state : pages) count += (state & 1) != 0;
  return count;
}

bool readable(const std::byte* address) {
  auto* maps = std::fopen("/proc/self/maps", "r");
  check(maps != nullptr, "open process mapping inventory");
  char line[512], rights[5];
  unsigned long first{}, last{};
  const auto target = reinterpret_cast<std::uintptr_t>(address);
  bool result{}, found{};
  while (std::fgets(line, sizeof(line), maps)) {
    if (std::sscanf(line, "%lx-%lx %4s", &first, &last, rights) != 3) continue;
    if (target < first || target >= last) continue;
    found = true; result = rights[0] == 'r'; break;
  }
  std::fclose(maps);
  check(found, "retained address space disappeared");
  return result;
}

template<class Step> Progress finish(worker::Lane& lane, Step step) {
  Progress result = Progress::yield();
  await([&] {
    result = step();
    if (result.kind() == Progress::Kind::done || result.kind() == Progress::Kind::failed) return true;
    worker::poll(lane, 32);
    return false;
  }, "allocation service failed to progress");
  return result;
}

struct Fixture {
  Ref<alloc::Arena> arena;
  worker::Lane lane;
  alloc::Pool* pool{};
  Fixture() : arena(take(alloc::open_arena(32 * 1024 * 1024))) {
    check(worker::adopt(lane).has_value(), "adopt owner Lane");
    pool = take(alloc::add_pool(*arena, lane));
  }
  void trim(std::uint64_t keep = 0) {
    check(finish(lane, [&] { return alloc::trim_cold(*pool, keep); }).kind() == Progress::Kind::done,
        "unexpected trim failure");
  }
  ~Fixture() {
    check(finish(lane, [&] { return alloc::close(*pool); }).kind() == Progress::Kind::done,
        "Pool failed to close");
    check(arena->charged_bytes.load() == 0 && !worker::pending(lane), "close retained charge or Messages");
    worker::relinquish(lane);
  }
};

enum class Call { discard, protect };
enum class Action { fail, partial, pause };
struct Hook {
  std::mutex mutex;
  std::condition_variable changed;
  std::thread::id owner;
  Call call{};
  Action action{};
  void* address{};
  int rights{};
  bool owner_call{}, armed{}, entered{}, resume{}, finished{};
} hook;
thread_local bool observe_vm{};
thread_local unsigned observed_vm_calls{};

void arm(Call call, Action action, void* address, int rights = PROT_NONE, bool owner_call = false) {
  std::lock_guard lock(hook.mutex);
  check(!hook.armed && (!hook.entered || hook.finished), "previous VM hook still active");
  hook.owner = std::this_thread::get_id();
  hook.call = call; hook.action = action; hook.address = address; hook.rights = rights;
  hook.owner_call = owner_call; hook.armed = true;
  hook.entered = hook.resume = hook.finished = false;
}

struct Intercept { bool selected{}; Action action{}; };

Intercept before(Call call, void* address, int rights = PROT_NONE) {
  if (observe_vm) ++observed_vm_calls;
  std::unique_lock lock(hook.mutex);
  if (!hook.armed || hook.call != call || hook.address != address ||
      (call == Call::protect && hook.rights != rights) ||
      hook.owner_call != (hook.owner == std::this_thread::get_id())) return {};
  hook.armed = false; hook.entered = true;
  hook.changed.notify_all();
  if (hook.action == Action::pause)
    check(hook.changed.wait_until(lock, Clock::now() + timeout, [] { return hook.resume; }),
        "owner did not release paused helper");
  return {true, hook.action};
}

void after(Intercept intercepted) {
  if (!intercepted.selected) return;
  std::lock_guard lock(hook.mutex);
  hook.finished = true; hook.changed.notify_all();
}

void wait_entered() {
  std::unique_lock lock(hook.mutex);
  check(hook.changed.wait_until(lock, Clock::now() + timeout, [] { return hook.entered; }),
      "helper missed VM hook");
}

void resume_helper() {
  std::lock_guard lock(hook.mutex);
  hook.resume = true; hook.changed.notify_all();
}

void wait_finished() {
  std::unique_lock lock(hook.mutex);
  check(hook.changed.wait_until(lock, Clock::now() + timeout, [] { return hook.finished; }),
      "helper did not finish VM hook");
}

void invalid_requests() {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(alloc::allocate(*fixture.pool, {2 * page, 8 * page, 64, 32}));
  fill(value);
  const auto charge = fixture.arena->charged_bytes.load();
  auto* address = value.address;
  auto invalid = [&](std::size_t body, std::size_t padding) {
    auto result = alloc::relinquish_tail(value, body, padding);
    check(!result && result.error() == Error::invalid_argument, "invalid relinquishment accepted");
    check(value.address == address && value.size == 2 * page && value.readable_padding == 32 &&
        fixture.arena->charged_bytes.load() == charge, "invalid relinquishment changed original storage");
  };
  invalid(2 * page + 1, 0);
  invalid(2 * page, 33);
  invalid(page, std::numeric_limits<std::size_t>::max());
  std::thread foreign([&] {
    const auto result = alloc::relinquish_tail(value, page, 0);
    check(!result && result.error() == Error::conflict, "foreign relinquishment accepted");
  });
  foreign.join();
  verify(address, 2 * page);
  alloc::Allocation empty;
  check(!alloc::relinquish_tail(empty, 0, 0), "empty Allocation relinquishment accepted");
}

void historical_padding_and_continuing_prefix() {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(alloc::allocate(*fixture.pool,
      {4 * page, 16 * page, 64, 4 * page, alloc::Initialization::zero}));
  fill(value);
  auto* address = value.address;
  const auto reservation = alloc::reserved_bytes(value);
  auto prefix = take(alloc::hold(value, {17, 96}));
  auto wide = take(alloc::hold(value, {7 * page + 17, 31}));
  auto duplicate = take(alloc::retain(wide));
  check(alloc::grow(value, {4 * page, 0, 64, 0}).has_value(), "padding reduction through grow failed");
  const auto charge = fixture.arena->charged_bytes.load();
  observed_vm_calls = 0; observe_vm = true;
  const auto relinquished = alloc::relinquish_tail(value, page - 8, 24);
  observe_vm = false;
  check(relinquished && !observed_vm_calls, "logical relinquishment performed a VM syscall");
  check(value.size == page - 8 && value.readable_padding == 24 && value.address == address &&
      alloc::reserved_bytes(value) == reservation, "relinquishment changed address or VA");
  auto continuing = take(alloc::hold(value, {page - 8, 24}));
  check(!alloc::hold(value, {page + 15, 2}), "new Hold crossed relinquished extent");
  fixture.trim();
  check(fixture.arena->charged_bytes.load() == charge && alloc::capacity(value) == 8 * page,
      "historic padding Hold was ignored");
  alloc::release(wide);
  fixture.trim();
  check(alloc::capacity(value) == 8 * page, "original release forgot its duplicate Hold");
  auto moved = std::move(duplicate);
  std::thread foreign([owned = std::move(moved)]() mutable {
    for (const auto byte : alloc::bytes(owned)) check(byte == std::byte{0}, "historic padding was damaged");
    alloc::release(owned);
  });
  foreign.join();
  fixture.trim();
  check(value.address == address && alloc::reserved_bytes(value) == reservation &&
      alloc::capacity(value) == 2 * page, "wide Hold retirement did not free tail under continuing readers");
  check(fixture.arena->charged_bytes.load() == charge - 6 * page, "live tail refund was not page exact");
  verify(address, page + 16);
  verify(prefix.address, prefix.footprint.size, prefix.footprint.offset);
  verify(continuing.address, continuing.footprint.size, continuing.footprint.offset);
  check(resident(address + 2 * page, 6 * page) == 0, "discarded live tail stayed resident");
  check(readable(address) && !readable(address + 2 * page), "live-tail protection boundary is wrong");
}

void tracking_refusal_and_endpoint_bin() {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(alloc::allocate(*fixture.pool, {8 * page, 16 * page, 64}));
  fill(value);
  const auto before = alloc::statistics(*fixture.pool);
  const auto charge = fixture.arena->charged_bytes.load();
  const auto limit = fixture.arena->byte_limit;
  fixture.arena->byte_limit = charge;
  const auto refused = alloc::hold(value, {0, 32});
  check(!refused && refused.error() == Error::capacity &&
      fixture.arena->charged_bytes.load() == charge &&
      alloc::statistics(*fixture.pool).metadata_bytes == before.metadata_bytes,
      "first-Hold tracking refusal changed use or metadata accounting");
  fixture.arena->byte_limit = limit;
  auto reader = take(alloc::hold(value, {2 * page, 1}));
  const auto tracked = fixture.arena->charged_bytes.load();
  check(tracked > charge && alloc::statistics(*fixture.pool).metadata_bytes > before.metadata_bytes,
      "reader tracking omitted its byte charge");
  auto duplicate = take(alloc::retain(reader));
  check(fixture.arena->charged_bytes.load() == tracked, "duplicate Hold allocated another tracking table");
  check(alloc::relinquish_tail(value, page).has_value(), "tracked source relinquishment refused");
  fixture.trim();
  check(alloc::capacity(value) == 4 * page && fixture.arena->charged_bytes.load() == tracked - 4 * page,
      "non-power-of-two endpoint did not retain the conservative four-page bin");
  verify(reader.address, 1, 2 * page);
  alloc::release(reader);
  fixture.trim();
  check(alloc::capacity(value) == 4 * page, "first release lost duplicate endpoint retention");
  alloc::release(duplicate);
  fixture.trim();
  check(alloc::capacity(value) == page && fixture.arena->charged_bytes.load() == tracked - 7 * page,
      "retired endpoint or refused Hold stranded live-tail backing");
}

void zero_extent_and_regrowth() {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(alloc::allocate(*fixture.pool, {4 * page, 8 * page, 64}));
  fill(value);
  auto* address = value.address;
  auto held = take(alloc::hold(value, {0, page}));
  const auto charge = fixture.arena->charged_bytes.load();
  check(alloc::relinquish_tail(value, 0).has_value(), "zero writer extent refused");
  fixture.trim();
  check(alloc::capacity(value) == page && bool(value) && value.size == 0 &&
      alloc::reserved_bytes(value) == 8 * page, "zero writer lost held prefix or reservation");
  verify(held.address, page);
  alloc::release(held);
  fixture.trim();
  check(alloc::capacity(value) == 0 && !readable(address) && resident(address, 4 * page) == 0 &&
      fixture.arena->charged_bytes.load() == charge - 4 * page, "zero extent retained physical backing");
  check(alloc::grow(value, {page, 8 * page, 64}).has_value(), "regrow after complete backing discard failed");
  check(value.address == address && alloc::capacity(value) == page &&
      fixture.arena->charged_bytes.load() == charge - 3 * page, "regrow moved VA or charged incorrectly");
  std::memset(address, 0x6b, page);
  for (auto byte : alloc::bytes(value)) check(byte == std::byte{0x6b}, "regrown body is not writable");
}

void finite_neighbors() {
  Fixture fixture;
  const auto page = page_size();
  auto first = take(alloc::allocate(*fixture.pool, {64, 0, 16}));
  auto neighbor = take(alloc::allocate(*fixture.pool, {64, 0, 16}));
  check(reinterpret_cast<std::uintptr_t>(first.address) / page ==
      reinterpret_cast<std::uintptr_t>(neighbor.address) / page, "finite fixture does not share a native page");
  std::memset(first.address, 0x35, 64);
  std::memset(neighbor.address, 0x6e, 64);
  auto held = take(alloc::hold(first, {0, 64}));
  const auto before = alloc::statistics(*fixture.pool);
  const auto charge = fixture.arena->charged_bytes.load();
  check(alloc::relinquish_tail(first, 16, 8).has_value(), "finite logical relinquishment refused");
  fixture.trim();
  check(alloc::statistics(*fixture.pool).mapped_bytes == before.mapped_bytes &&
      fixture.arena->charged_bytes.load() == charge, "subpage relinquishment reclaimed a live shared page");
  for (auto byte : alloc::bytes(held)) check(byte == std::byte{0x35}, "finite historical Hold damaged");
  for (auto byte : alloc::bytes(neighbor)) check(byte == std::byte{0x6e}, "finite neighbor damaged");
}

void paused_helper_and_release() {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(alloc::allocate(*fixture.pool, {8 * page, 16 * page, 64}));
  fill(value);
  auto* address = value.address;
  auto prefix = take(alloc::hold(value, {0, 64}));
  check(alloc::relinquish_tail(value, page).has_value(), "relinquish before paused helper failed");
  const auto charge = fixture.arena->charged_bytes.load();
  arm(Call::discard, Action::pause, address + page);
  observed_vm_calls = 0; observe_vm = true;
  const auto started = alloc::trim_cold(*fixture.pool, 0);
  observe_vm = false;
  check(started.kind() == Progress::Kind::waiting && !observed_vm_calls,
      "tail trim blocked owner on VM work");
  wait_entered();
  check(alloc::capacity(value) == page && fixture.arena->charged_bytes.load() == charge,
      "pending helper exposed capacity or refunded unsettled bytes");
  check(alloc::grow(value, {page, 0, 64}).has_value(), "growth below dispatched cut refused");
  const auto refused = alloc::grow(value, {2 * page, 0, 64});
  check(!refused && refused.error() == Error::conflict && value.size == page && value.address == address,
      "growth raced the dispatched discard");
  auto late = take(alloc::hold(value, {16, 16}));
  check(!alloc::hold(value, {page - 1, 2}), "pending reclaim permitted a new tail reader");
  alloc::release(value);
  auto neighbor = take(alloc::allocate(*fixture.pool, {8 * page, 16 * page, 64}));
  check(neighbor.address != address, "pending live-tail record entered idle reuse");
  const auto before_reply = fixture.arena->charged_bytes.load();
  check(alloc::close(*fixture.pool).kind() == Progress::Kind::waiting, "close ignored pending helper");
  resume_helper(); wait_finished();
  await([&] { return worker::pending(fixture.lane); }, "helper did not publish its owner reply");
  check(fixture.arena->charged_bytes.load() == before_reply, "helper applied refund before owner reply");
  worker::poll(fixture.lane, 32);
  verify(prefix.address, prefix.footprint.size);
  verify(late.address, late.footprint.size, late.footprint.offset);
  check(!readable(address + page) && resident(address + page, 7 * page) == 0,
      "completed helper did not reclaim physical tail");
  check(alloc::close(*fixture.pool).kind() == Progress::Kind::waiting, "close forgot retained prefix readers");
  alloc::release(prefix); alloc::release(late); alloc::release(neighbor);
}

struct Control {
  std::size_t size{};
  bool valid(std::pmr::memory_resource& memory) const noexcept {
    return &memory == std::pmr::new_delete_resource();
  }
  std::pmr::memory_resource* resource() const noexcept { return std::pmr::new_delete_resource(); }
  std::size_t part_count() const noexcept { return 1; }
  PartId id(std::size_t) const noexcept { return {7, 9}; }
  Result<std::size_t> extent(std::size_t) const noexcept { return size; }
  std::uint32_t padding(std::size_t) const noexcept { return 0; }
  std::size_t alignment(std::size_t) const noexcept { return 64; }
  void retain() const noexcept {}
  void release() const noexcept {}
};

void capture_during_reclamation() {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(alloc::allocate(*fixture.pool, {8 * page, 16 * page, 64}));
  fill(value);
  auto* address = value.address;
  auto prefix = take(alloc::hold(value, {0, 32}));
  check(alloc::relinquish_tail(value, page).has_value(), "capture source relinquishment refused");
  arm(Call::discard, Action::pause, address + page);
  check(alloc::trim_cold(*fixture.pool, 0).kind() == Progress::Kind::waiting, "capture trim did not start");
  wait_entered();
  const auto source = PartSource{{7, 9}, alloc::bytes(value), {}, &value};
  auto* representation = take(capture_representation(*std::pmr::new_delete_resource(), Control{page},
      PartOwners::independent_allocation, std::span{&source, 1}));
  check(!value, "capture did not transfer source Allocation");
  const auto selected = take(RepresentationSelection<Control>{representation}.part(std::size_t{0}));
  alloc::release(prefix);
  resume_helper(); wait_finished();
  fixture.trim();
  check(selected.address() == address, "reclamation changed captured Part address");
  verify(selected.address(), page);
  check(alloc::statistics(*fixture.pool).mapped_bytes == page,
      "captured writer extent prevented physical tail reclaim or was discarded itself");
  check(alloc::close(*fixture.pool).kind() == Progress::Kind::waiting, "close ignored captured Allocation use");
  orbital::release(representation);
}

void helper_failure(Call call, Action action) {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(alloc::allocate(*fixture.pool, {8 * page, 16 * page, 64}));
  fill(value);
  auto* address = value.address;
  auto prefix = take(alloc::hold(value, {0, 64}));
  check(alloc::relinquish_tail(value, page).has_value(), "failure source relinquishment refused");
  const auto charge = fixture.arena->charged_bytes.load();
  arm(call, action, address + page);
  const auto result = finish(fixture.lane, [&] { return alloc::trim_cold(*fixture.pool, 0); });
  check(result.kind() == Progress::Kind::failed && result.failure() == Error::io,
      "VM failure was hidden or incorrectly reported as success");
  wait_finished();
  check(value.address == address && value.size == page && alloc::reserved_bytes(value) == 16 * page &&
      alloc::capacity(value) == page && fixture.arena->charged_bytes.load() == charge,
      "failed trim lost reservation, original extent or conservative charge");
  verify(address, page);
  check(readable(address), "failed trim protected useful bytes");
  check(alloc::statistics(*fixture.pool).uncertain_bytes == 7 * page,
      "failed trim omitted its unsettled tail from accounting");
  fixture.trim();
  check(fixture.arena->charged_bytes.load() == charge - 7 * page &&
      resident(address + page, 7 * page) == 0 && !readable(address + page),
      "retry failed to settle and refund the same tail once");
  fixture.trim();
  check(fixture.arena->charged_bytes.load() == charge - 7 * page, "retry refunded tail twice");
}

void regrowth_failure_and_budget() {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(alloc::allocate(*fixture.pool, {8 * page, 16 * page, 64}));
  fill(value);
  auto* address = value.address;
  check(alloc::relinquish_tail(value, page).has_value(), "regrowth source relinquishment refused");
  fixture.trim();
  const auto charge = fixture.arena->charged_bytes.load();
  const auto limit = fixture.arena->byte_limit;
  fixture.arena->byte_limit = charge;
  const auto refused = alloc::grow(value, {4 * page, 0, 64});
  check(!refused && refused.error() == Error::capacity && value.size == page && value.address == address &&
      fixture.arena->charged_bytes.load() == charge, "budget refusal damaged retained allocation");
  fixture.arena->byte_limit = limit;
  arm(Call::protect, Action::partial, address + page, PROT_READ | PROT_WRITE, true);
  const auto failed = alloc::grow(value, {4 * page, 0, 64});
  check(!failed && failed.error() == Error::io && value.size == page && alloc::capacity(value) == page &&
      fixture.arena->charged_bytes.load() == charge + 3 * page,
      "partial restoration lost the original extent or uncertain charge");
  verify(address, page);
  check(alloc::grow(value, {4 * page, 0, 64}).has_value() && value.address == address &&
      fixture.arena->charged_bytes.load() == charge + 3 * page,
      "restoration retry moved VA or charged uncertain pages twice");
  verify(address, page);
  std::memset(address + page, 0x6d, 3 * page);
  for (std::size_t offset = page; offset < 4 * page; ++offset)
    check(address[offset] == std::byte{0x6d}, "restored tail is not writable");
}

void keep_floor() {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(alloc::allocate(*fixture.pool, {8 * page, 16 * page, 64}));
  fill(value);
  check(alloc::relinquish_tail(value, page).has_value(), "floor source relinquishment refused");
  const auto charge = fixture.arena->charged_bytes.load();
  fixture.trim(8 * page);
  check(alloc::capacity(value) == 8 * page && fixture.arena->charged_bytes.load() == charge,
      "keep floor discarded backing that should remain available");
  fixture.trim(3 * page);
  const auto mapped = alloc::statistics(*fixture.pool).mapped_bytes;
  check(mapped >= 4 * page && mapped <= 8 * page &&
      charge - fixture.arena->charged_bytes.load() == 8 * page - mapped,
      "live-tail trim violated idle-payload floor or exact refund");
  fixture.trim();
  check(alloc::capacity(value) == page && fixture.arena->charged_bytes.load() == charge - 7 * page,
      "zero floor did not reclaim the remaining eligible tail");
  verify(value.address, page);
}

void implicit_arena_teardown() {
  const auto page = page_size();
  worker::Lane lane;
  check(worker::adopt(lane).has_value(), "adopt implicit-teardown Lane");
  auto arena = take(alloc::open_arena(16 * 1024 * 1024));
  auto* pool = take(alloc::add_pool(*arena, lane));
  auto value = take(alloc::allocate(*pool, {8 * page, 16 * page, 64}));
  fill(value);
  auto* address = value.address;
  auto prefix = take(alloc::hold(value, {0, 32}));
  check(alloc::relinquish_tail(value, page).has_value(), "teardown source relinquishment refused");
  arm(Call::discard, Action::pause, address + page);
  check(alloc::trim_cold(*pool, 0).kind() == Progress::Kind::waiting, "teardown trim did not start");
  wait_entered();
  alloc::release(value); arena.reset();
  verify(prefix.address, prefix.footprint.size);
  resume_helper(); wait_finished();
  await([&] { return worker::pending(lane); }, "teardown helper reply missing");
  while (worker::pending(lane)) worker::poll(lane, 32);
  verify(prefix.address, prefix.footprint.size);
  alloc::release(prefix);
  unsigned char state{};
  errno = 0;
  check(::mincore(address, page, &state) == -1 && errno == ENOMEM,
      "last physical use failed to destroy Arena and reserved VA");
  check(!worker::pending(lane), "implicit teardown left an unserviceable Message");
  worker::relinquish(lane);
}
} // namespace

extern "C" int __real_madvise(void*, std::size_t, int) noexcept;
extern "C" int __real_mprotect(void*, std::size_t, int) noexcept;

extern "C" int __wrap_madvise(void* address, std::size_t amount, int advice) noexcept {
  const auto intercepted = advice == MADV_DONTNEED ? before(Call::discard, address) : Intercept{};
  int result{};
  if (intercepted.selected && intercepted.action != Action::pause) {
    if (intercepted.action == Action::partial)
      check(__real_madvise(address, page_size(), advice) == 0, "partial discard setup failed");
    errno = EIO; result = -1;
  } else result = __real_madvise(address, amount, advice);
  after(intercepted);
  return result;
}

extern "C" int __wrap_mprotect(void* address, std::size_t amount, int rights) noexcept {
  const auto intercepted = before(Call::protect, address, rights);
  int result{};
  if (intercepted.selected && intercepted.action != Action::pause) {
    if (intercepted.action == Action::partial)
      check(__real_mprotect(address, page_size(), rights) == 0, "partial protection setup failed");
    errno = EIO; result = -1;
  } else result = __real_mprotect(address, amount, rights);
  after(intercepted);
  return result;
}

int main() {
  invalid_requests();
  historical_padding_and_continuing_prefix();
  tracking_refusal_and_endpoint_bin();
  zero_extent_and_regrowth();
  finite_neighbors();
  paused_helper_and_release();
  capture_during_reclamation();
  helper_failure(Call::discard, Action::fail);
  helper_failure(Call::discard, Action::partial);
  helper_failure(Call::protect, Action::fail);
  helper_failure(Call::protect, Action::partial);
  regrowth_failure_and_budget();
  keep_floor();
  implicit_arena_teardown();
  std::puts("{\"case\":\"live-tail-bytes-holds-va-refund-failure-and-lifetime\",\"passed\":true}");
}
