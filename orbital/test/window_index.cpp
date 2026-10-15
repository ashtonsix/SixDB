#include <orbital/preparation.h>
#include "internal.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

using namespace orbital;
using namespace orbital::allocation;

namespace {
bool fail_protection{};
void* failed_protection{};
std::atomic<bool> fail_unmap{};
std::atomic<void*> failed_unmap{};
std::array<std::atomic<void*>, 4> observed_mappings{};
std::array<std::atomic<unsigned>, 4> observed_unmaps{};

std::size_t page_size() { return static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)); }

void check(bool condition, const char* detail,
           std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), detail);
    std::abort();
  }
}

template<class T> T take(Result<T> value,
                        std::source_location where = std::source_location::current()) {
  check(bool(value), "operation refused", where);
  return std::move(*value);
}

struct Fixture {
  Ref<Arena> arena;
  worker::Lane lane;
  Pool* pool{};
  explicit Fixture() : arena(take(open_arena(512 * page_size()))) {
    check(bool(worker::adopt(lane)), "adopt lane");
    pool = take(add_pool(*arena, lane));
  }
  template<class Step> void until(Step step) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!step()) {
      check(std::chrono::steady_clock::now() < deadline, "operation did not settle");
      worker::poll(lane, 32);
      std::this_thread::yield();
    }
  }
  ~Fixture() {
    until([&] {
      const auto progress = close(*pool);
      check(progress.kind() != Progress::Kind::failed, "close failed");
      return progress.kind() == Progress::Kind::done;
    });
    check(arena->charged_bytes.load() == 0, "close retained a charge");
    check(bool(remove_pool(*pool)), "remove closed Pool");
    worker::relinquish(lane);
  }
};

void mixed_reservations() {
  Fixture f;
  const auto page = page_size();
  std::array<Allocation, 4> values;
  std::array<std::byte*, 4> addresses;
  constexpr std::array reservations{7u, 2u, 5u, 17u};
  for (std::size_t i = 0; i < values.size(); ++i) {
    values[i] = take(allocate(*f.pool, {page, reservations[i] * page, 64}));
    addresses[i] = values[i].address;
  }
  release(values[0]); release(values[2]);
  // Force rollover; check candidate order rather than the counter representation.
  f.pool->internal->idle_order = std::numeric_limits<std::uint64_t>::max();
  release(values[3]); release(values[1]);
  // Newest ready wins across reservation classes; size proximity is not policy.
  values[0] = take(allocate(*f.pool, {page, 3 * page, 64}));
  values[1] = take(allocate(*f.pool, {page, 3 * page, 64}));
  values[2] = take(allocate(*f.pool, {page, 6 * page, 64}));
  values[3] = take(allocate(*f.pool, {page, page, 64}));
  check(values[0].address == addresses[3] && values[1].address == addresses[2] &&
        values[2].address == addresses[0] && values[3].address == addresses[1],
        "reservation filtering changed LIFO ready ranking or selected live backing");
}

void alignment_filter() {
  Fixture f;
  const auto page = page_size(), alignment = 64 * page;
  std::array<Allocation, 8> values;
  std::byte* expected{};
  for (std::size_t i = 0; i < values.size(); ++i) {
    // Explicitly aligned older candidates ensure the later request is feasible;
    // ordinary mappings may also happen to satisfy it on a particular host.
    values[i] = take(allocate(*f.pool, {page, (i + 1) * page,
        i % 3 == 0 ? alignment : std::size_t{64}}));
    if (reinterpret_cast<std::uintptr_t>(values[i].address) % alignment == 0)
      expected = values[i].address;
  }
  for (auto& value : values) release(value);
  auto chosen = take(allocate(*f.pool, {page, page, alignment}));
  check(chosen.address == expected, "alignment filtering lost newest eligible ready Window");
}

void fallback_and_uncertain() {
  Fixture f;
  const auto page = page_size();
  auto nearer = take(allocate(*f.pool, {3 * page, 16 * page, 64}));
  auto smaller = take(allocate(*f.pool, {page, 8 * page, 64}));
  auto* expected = nearer.address;
  release(nearer); release(smaller);
  auto grown = take(allocate(*f.pool, {4 * page, 4 * page, 64}));
  check(grown.address == expected, "fallback did not minimize extra backing across reservations");
  release(grown);

  // A partially protected mapping is charged, but not confirmed ready. An
  // earlier ready candidate must beat this newer zero-extra-cost candidate.
  fail_protection = true;
  auto failed = allocate(*f.pool, {4 * page, 32 * page, 64});
  check(!failed && failed.error() == Error::io && !fail_protection,
        "partial mprotect failure was not injected");
  auto* older_uncertain = failed_protection;
  fail_protection = true;
  failed = allocate(*f.pool, {4 * page, 64 * page, 64});
  check(!failed && failed.error() == Error::io && !fail_protection,
        "second partial protection failure was not injected");
  auto* newer_uncertain = failed_protection;
  auto ready = take(allocate(*f.pool, {4 * page, 4 * page, 64}));
  check(ready.address == expected, "uncertain backing displaced confirmed ready backing");
  const auto charged = f.arena->charged_bytes.load();
  auto retried = take(allocate(*f.pool, {4 * page, 32 * page, 64, 0, Initialization::zero}));
  check(retried.address == newer_uncertain, "equal-cost uncertain fallback lost LIFO ranking");
  auto remaining = take(allocate(*f.pool, {4 * page, 32 * page, 64}));
  check(remaining.address == older_uncertain, "uncertain retry left duplicate or missing eligibility");
  check(f.arena->charged_bytes.load() == charged, "uncertain retry charged backing twice");
  for (auto byte : bytes(retried)) check(byte == std::byte{0}, "retry omitted initialization");
}

void preparation_withdrawal() {
  Fixture f;
  const auto page = page_size();
  auto older = take(allocate(*f.pool, {page, 4 * page, 64}));
  auto newest = take(allocate(*f.pool, {page, 8 * page, 64}));
  auto* older_address = older.address;
  auto* newest_address = newest.address;
  release(older); release(newest);
  worker::Lane preparer;
  std::atomic<bool> ready{}, proceed{};
  std::thread helper([&] {
    check(bool(worker::adopt(preparer)), "adopt preparer Lane");
    ready.store(true, std::memory_order_release);
    while (!proceed.load(std::memory_order_acquire)) std::this_thread::yield();
    while (worker::pending(preparer)) worker::poll(preparer, 32);
    worker::relinquish(preparer);
  });
  f.until([&] { return ready.load(std::memory_order_acquire); });
  Preparation pending;
  check(bool(prepare(*f.pool, pending, {page, page, 64}, preparer)), "start preparation");
  auto ordinary = take(allocate(*f.pool, {page, page, 64}));
  check(ordinary.address == older_address, "pending preparation remained allocation-eligible");
  cancel(pending);
  proceed.store(true, std::memory_order_release);
  f.until([&] { return pending.state != Preparation::State::pending; });
  helper.join();
  check(pending.state == Preparation::State::cancelled, "cancelled preparation delivered storage");
  auto returned = take(allocate(*f.pool, {page, page, 64}));
  check(returned.address == newest_address, "settled cancelled preparation did not restore backing");
}

void failed_trim_reentry() {
  Fixture f;
  const auto page = page_size();
  auto value = take(allocate(*f.pool, {page, 4 * page, 64}));
  auto* original = value.address;
  release(value);
  failed_unmap = nullptr;
  fail_unmap = true;
  check(trim_cold(*f.pool, 0).kind() == Progress::Kind::waiting, "trim did not start");
  auto during_trim = take(allocate(*f.pool, {page, 4 * page, 64}));
  check(during_trim.address != original, "pending trim remained allocation-eligible");
  f.until([&] {
    const auto progress = trim_cold(*f.pool, 0);
    check(progress.kind() != Progress::Kind::done, "injected trim failure was lost");
    return progress.kind() == Progress::Kind::failed;
  });
  check(failed_unmap.load() == original, "failed trim did not target fixture Window");
  auto retry = take(allocate(*f.pool, {page, 4 * page, 64}));
  check(retry.address == original, "failed trim did not restore allocation eligibility");
  auto other = take(allocate(*f.pool, {page, 4 * page, 64}));
  check(other.address != retry.address, "restored Window was selected twice");
}

void independent_growth() {
  Fixture f;
  const auto page = page_size();
  std::array<Allocation, 64> values;
  for (std::size_t i = 0; i < values.size(); ++i) {
    values[i] = take(allocate(*f.pool, {page, page, 8}));
    std::memset(values[i].address, static_cast<int>(i + 1), page);
  }
  auto reader = take(hold(values[0], {0, page}));
  auto* original = values[0].address;
  auto refused = grow(values[0], {2 * page, 2 * page, 8});
  check(!refused && refused.error() == Error::conflict && values[0].address == original &&
        values[0].size == page, "retained Hold did not block relocation without mutation");
  release(reader);
  for (std::size_t i = 0; i < values.size(); ++i) {
    check(bool(grow(values[i], {2 * page, 2 * page, 8})), "cohort growth failed");
    for (std::size_t byte = 0; byte < page; ++byte)
      check(values[i].address[byte] == std::byte(i + 1), "cohort relocation lost old bytes");
    std::memset(values[i].address + page, static_cast<int>(i + 1), page);
  }
  const auto charge = f.arena->charged_bytes.load();
  original = values[0].address;
  refused = grow(values[0], {2 * f.arena->byte_limit, 0, 8});
  check(!refused && refused.error() == Error::capacity && values[0].address == original &&
        values[0].size == 2 * page && f.arena->charged_bytes.load() == charge,
        "budget refusal changed live storage or accounting");
  for (std::size_t i = 0; i < values.size(); ++i)
    for (auto byte : bytes(values[i]))
      check(byte == std::byte(i + 1), "cohort operation aliased or damaged another live buffer");
}

void last_hold_teardown() {
  const auto page = page_size();
  auto external = take(open_arena(128 * page));
  worker::Lane lane;
  check(bool(worker::adopt(lane)), "adopt teardown Lane");
  auto& pool = *take(add_pool(*external, lane));
  std::array<Allocation, 4> values;
  constexpr std::array reservations{3u, 7u, 3u, 11u};
  for (std::size_t i = 0; i < values.size(); ++i) {
    values[i] = take(allocate(pool, {page, reservations[i] * page, 64}));
    observed_unmaps[i] = 0;
    observed_mappings[i] = values[i].address;
  }
  std::memset(values[1].address, 0x6b, page);
  auto last = take(hold(values[1], {0, page}));
  // Idle-list order differs from owning-vector order, including a shared size key.
  for (const auto i : std::array{2u, 0u, 3u, 1u}) release(values[i]);
  external.reset();
  for (const auto& count : observed_unmaps)
    check(count.load() == 0, "external Arena release reclaimed physically retained inventory");
  for (const auto byte : bytes(last)) check(byte == std::byte{0x6b}, "last Hold lost readable bytes");
  release(last); // Destroys Arena, Pool, the owning vector and its shared index.
  for (std::size_t i = 0; i < values.size(); ++i) {
    check(observed_unmaps[i].load() == 1, "final Hold did not unmap every Window exactly once");
    observed_mappings[i] = nullptr;
  }
  check(!worker::pending(lane), "final Hold teardown left an undrainable Message");
  worker::relinquish(lane);
}
} // namespace

extern "C" int __real_mprotect(void*, std::size_t, int);
extern "C" int __real_munmap(void*, std::size_t);

extern "C" int __wrap_mprotect(void* address, std::size_t length, int protection) {
  if (fail_protection && protection == (PROT_READ | PROT_WRITE)) {
    fail_protection = false;
    failed_protection = address;
    check(length >= 2 * page_size(), "fixture did not reach multi-page protection");
    check(__real_mprotect(address, page_size(), protection) == 0, "partial protection failed");
    errno = EIO;
    return -1;
  }
  return __real_mprotect(address, length, protection);
}

extern "C" int __wrap_munmap(void* address, std::size_t length) {
  if (fail_unmap.exchange(false)) {
    failed_unmap = address;
    errno = EIO;
    return -1;
  }
  const auto result = __real_munmap(address, length);
  if (result == 0)
    for (std::size_t i = 0; i < observed_mappings.size(); ++i)
      if (observed_mappings[i].load() == address) ++observed_unmaps[i];
  return result;
}

int main() {
  mixed_reservations(); alignment_filter(); fallback_and_uncertain();
  preparation_withdrawal(); failed_trim_reentry(); independent_growth();
  last_hold_teardown();
  std::puts("window index: candidate ranking, failure re-entry, exclusive preparation and growth pass");
}
