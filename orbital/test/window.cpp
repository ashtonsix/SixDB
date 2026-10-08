#include <orbital/allocation.h>
#include "../src/allocation/internal.h"
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
bool fail_preparation{};
void* failed_address{};

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
  explicit Fixture(std::uint64_t limit = 16 * 1024 * 1024) : arena(take(open_arena(limit))) {
    check(bool(worker::adopt(lane)), "adopt lane");
    pool = take(add_pool(*arena, lane));
  }
  template<class Step> void finish(Step step) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto progress = step();
      check(progress.kind() != Progress::Kind::failed, "unexpected reclamation failure");
      if (progress.kind() == Progress::Kind::done) return;
      check(std::chrono::steady_clock::now() < deadline, "reclamation did not progress");
      worker::poll(lane, 32);
      std::this_thread::yield();
    }
  }
  ~Fixture() {
    finish([&] { return close(*pool); });
    check(arena->charged_bytes.load() == 0, "close retained a budget charge");
    worker::relinquish(lane);
  }
};

void arm() { fail_preparation = true; failed_address = nullptr; }

void failed(Result<void> value) {
  check(!value && value.error() == Error::io && !fail_preparation && failed_address,
        "foreground partial preparation failure did not reach caller");
}

void trim_all(Fixture& fixture) {
  fixture.finish([&] { return trim_cold(*fixture.pool, 0); });
  const auto inventory = statistics(*fixture.pool);
  check(inventory.mapped_bytes == 0 && inventory.reserved_bytes == 0 &&
        inventory.metadata_bytes == 0 && fixture.arena->charged_bytes.load() == 0,
        "trim0 stranded idle Window mappings or metadata");
}

void fresh_preparation() {
  Fixture fixture;
  const auto page = page_size();
  const auto alignment = page * 4;
  const AllocationRequest request{page * 2, page * 8, alignment, 0, Initialization::zero};
  arm();
  auto refused = allocate(*fixture.pool, request);
  check(!refused, "fresh preparation unexpectedly succeeded");
  failed(std::unexpected(refused.error()));
  const auto retained = fixture.arena->charged_bytes.load();
  const auto inventory = statistics(*fixture.pool);
  check(inventory.mapped_bytes == 0 && inventory.uncertain_bytes == page * 2 && inventory.metadata_bytes > 0 &&
        retained == inventory.uncertain_bytes + inventory.metadata_bytes,
        "failed preparation lost conservative backing charge");
  check(inventory.reserved_bytes == page * 8 + alignment - page,
        "reserved VA excludes owned alignment slack");
  auto retry = take(allocate(*fixture.pool, request));
  check(retry.address == failed_address && reinterpret_cast<std::uintptr_t>(retry.address) % alignment == 0,
        "retry lost aligned owned mapping");
  check(fixture.arena->charged_bytes.load() == retained, "retry charged uncertain preparation twice");
  for (const auto byte : bytes(retry)) check(byte == std::byte{0}, "retry zero initialization");
  release(retry);
  trim_all(fixture);
}

void reused_preparation() {
  Fixture fixture;
  const auto page = page_size();
  auto original = take(allocate(*fixture.pool, {page * 2, page * 8, 64}));
  auto* address = original.address;
  release(original);
  arm();
  auto refused = allocate(*fixture.pool, {page * 4, page * 8, 64});
  check(!refused, "idle reuse preparation unexpectedly succeeded");
  failed(std::unexpected(refused.error()));
  const auto charged = fixture.arena->charged_bytes.load();
  auto retry = take(allocate(*fixture.pool, {page * 4, page * 8, 64, 0, Initialization::zero}));
  check(retry.address == address && fixture.arena->charged_bytes.load() == charged,
        "idle retry lost backing or double charged");
  for (const auto byte : bytes(retry)) check(byte == std::byte{0}, "idle retry initialization");
  release(retry);
  trim_all(fixture);
}

void active_growth() {
  Fixture fixture;
  const auto page = page_size();
  auto value = take(allocate(*fixture.pool, {page * 2, page * 8, 64, 0, Initialization::zero}));
  std::memset(value.address, 0x5d, value.size);
  auto captured = take(hold(value, {0, 128}));
  auto* original = value.address;
  arm();
  failed(grow(value, {page * 4, page * 8, 64, 0, Initialization::zero}));
  check(value.address == original && value.size == page * 2 && capacity(value) == page * 2,
        "failed growth changed original allocation");
  for (const auto byte : bytes(captured)) check(byte == std::byte{0x5d}, "failed growth damaged retained bytes");
  const auto charged = fixture.arena->charged_bytes.load();
  check(bool(grow(value, {page * 4, page * 8, 64, 0, Initialization::zero})), "growth retry failed");
  check(value.address == original && fixture.arena->charged_bytes.load() == charged,
        "growth retry changed address or double charged");
  for (std::size_t byte = 0; byte < page * 2; ++byte)
    check(value.address[byte] == std::byte{0x5d}, "growth lost original data");
  for (std::size_t byte = page * 2; byte < value.size; ++byte)
    check(value.address[byte] == std::byte{0}, "growth did not initialize appended bytes");
  release(captured); release(value);
  trim_all(fixture);
}

void idle_inventory() {
  Fixture fixture;
  const auto page = page_size();
  auto retained = take(allocate(*fixture.pool, {page * 2, page * 8, 64}));
  auto warm = take(allocate(*fixture.pool, {page * 2, page * 8, 64}));
  auto cold = take(allocate(*fixture.pool, {page * 2, page * 8, 64}));
  auto* warm_address = warm.address;
  auto* cold_address = cold.address;
  retained.address[0] = std::byte{0x3b};
  auto reader = take(hold(retained, {0, 1}));
  release(retained);
  release(warm, {Temperature::warm});
  release(cold, {Temperature::cold});
  auto reused = take(allocate(*fixture.pool, {page * 2, page * 8, 64}));
  check(reused.address == warm_address || reused.address == cold_address,
        "idle selection reused retained live storage instead of available backing");
  auto* queued_address = reused.address;
  auto* other_address = queued_address == warm_address ? cold_address : warm_address;
  std::thread foreign([owned = std::move(reused)]() mutable { release(owned); });
  foreign.join();
  check(worker::pending(fixture.lane), "foreign final return did not queue");
  auto before_return = take(allocate(*fixture.pool, {page * 2, page * 8, 64}));
  check(before_return.address == other_address, "queued physical return entered idle inventory before final queue touch");
  worker::poll(fixture.lane, 32);
  auto after_return = take(allocate(*fixture.pool, {page * 2, page * 8, 64}));
  check(after_return.address == queued_address, "completed foreign return did not enter idle inventory");
  check(reader.address[0] == std::byte{0x3b}, "idle inventory reused retained physical bytes");
  release(reader); release(before_return); release(after_return);
  trim_all(fixture);
}

void prepared_backing_before_hint(bool fully_prepared) {
  const auto page = page_size();
  Fixture fixture(16 * page);
  auto small = take(allocate(*fixture.pool, {page, 4 * page, 64}));
  auto large = take(allocate(*fixture.pool,
      {(fully_prepared ? 4 : 3) * page - 16, 4 * page, 64, 16}));
  auto neighbor = take(allocate(*fixture.pool, {9 * page, 9 * page, 64}));
  std::memset(neighbor.address, 0x6d, neighbor.size);
  auto* original = large.address;
  release(small, {Temperature::warm});
  release(large, {Temperature::cold});
  const auto charged = fixture.arena->charged_bytes.load();
  check(fixture.arena->byte_limit - charged < 3 * page,
        "fixture permits preparing the smaller Window");
  auto reused = take(allocate(*fixture.pool,
      {4 * page - 16, 4 * page, 64, 16, Initialization::zero}));
  check(reused.address == original &&
      fixture.arena->charged_bytes.load() == charged + (fully_prepared ? 0 : page),
        "selection stranded feasible prepared backing or charged it again");
  for (std::size_t index = 0; index < reused.size + reused.readable_padding; ++index)
    check(reused.address[index] == std::byte{0}, "prepared reuse omitted initialization/padding");
  for (const auto byte : bytes(neighbor)) check(byte == std::byte{0x6d}, "reuse damaged live neighbor");
  release(reused); release(neighbor);
  trim_all(fixture);
}

void partial_hint(bool foreign) {
  const auto page = page_size();
  Fixture fixture(16 * page);
  auto warm = take(allocate(*fixture.pool, {2 * page, 0, 64}));
  auto hot = take(allocate(*fixture.pool, {2 * page, 0, 64}));
  auto* original = hot.address;
  auto held = take(hold(hot, {0, 1}));
  const auto access = std::chrono::steady_clock::now();
  release(warm, {Temperature::warm});
  release(hot, {.temperature = Temperature::hot, .last_access = access});
  ReleaseHint partial;
  partial.last_access = access + std::chrono::nanoseconds(1);
  if (foreign) {
    std::thread releaser([owned = std::move(held), partial]() mutable { release(owned, partial); });
    releaser.join();
    while (worker::pending(fixture.lane)) worker::poll(fixture.lane, 32);
  } else release(held, partial);
  fixture.finish([&] { return trim_cold(*fixture.pool, 2 * page); });
  auto survivor = take(allocate(*fixture.pool, {2 * page, 0, 64}));
  check(survivor.address == original, "timestamp-only advice erased hot temperature during reclamation");
  auto alternative = take(allocate(*fixture.pool, {2 * page, 0, 64}));
  auto capture = take(hold(survivor, {0, 1}));
  auto& advice = static_cast<orbital::allocation::detail::Window*>(
      survivor.storage->domain->context)->hint;
  release(survivor);
  // A stale complete record cannot replace the newer per-field advice.
  release(capture, {.temperature = Temperature::cold, .last_access = access});
  release(alternative, {.temperature = Temperature::hot});
  auto selected = take(allocate(*fixture.pool, {2 * page, 0, 64}));
  check(advice.temperature == Temperature::hot &&
      advice.last_access == partial.last_access,
        "partial/stale advice erased fields of the newer release hint");
  release(selected);
  trim_all(fixture);
}
} // namespace

extern "C" int __real_mprotect(void*, std::size_t, int);

extern "C" int __wrap_mprotect(void* address, std::size_t length, int protection) {
  if (fail_preparation && protection == (PROT_READ | PROT_WRITE)) {
    check(length >= page_size() * 2, "fixture did not reach multi-page preparation");
    fail_preparation = false;
    failed_address = address;
    check(__real_mprotect(address, page_size(), protection) == 0, "partial native protection failed");
    errno = EIO;
    return -1;
  }
  return __real_mprotect(address, length, protection);
}

int main() {
  fresh_preparation();
  reused_preparation();
  active_growth();
  idle_inventory();
  prepared_backing_before_hint(true); prepared_backing_before_hint(false);
  partial_hint(false); partial_hint(true);
  std::puts("window: partial foreground preparation, aligned ownership, growth retry and metadata retirement pass");
}
