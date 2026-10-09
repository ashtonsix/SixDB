// Actual Stage1 allocator state behind encoded shared-shape collections.
// Probe-only heap observation; no Store implementation or production change.
#include "shape/shape.h"
#include "candidates/common.h"
#include "retention/checks.h"
#include "internal.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <pthread.h>

namespace heap_probe {
struct Record { void* address; std::size_t bytes, usable; unsigned tag; };
// Observation storage belongs to this diagnostic, not the measured runtime.
Record records[4096]{};
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
std::atomic<unsigned> tag{};
struct Totals { std::size_t bytes{}, usable{}, blocks{}; };

void* allocate(std::size_t bytes, std::size_t alignment) {
  void* result = nullptr;
  if (alignment > alignof(std::max_align_t)) {
    if (posix_memalign(&result, alignment, bytes ? bytes : 1)) throw std::bad_alloc();
  } else result = std::malloc(bytes ? bytes : 1);
  if (!result) throw std::bad_alloc();
  if (const auto selected = tag.load(std::memory_order_relaxed)) {
    pthread_mutex_lock(&mutex);
    std::size_t slot = 0;
    while (slot < std::size(records) && records[slot].address) ++slot;
    if (slot == std::size(records)) std::abort();
    records[slot] = {result, bytes, malloc_usable_size(result), selected};
    pthread_mutex_unlock(&mutex);
  }
  return result;
}

void deallocate(void* address) noexcept {
  if (!address) return;
  pthread_mutex_lock(&mutex);
  for (auto& record : records)
    if (record.address == address) { record = {}; break; }
  pthread_mutex_unlock(&mutex);
  std::free(address);
}

Totals totals(unsigned selected) {
  Totals result;
  pthread_mutex_lock(&mutex);
  for (const auto& record : records) if (record.address && record.tag == selected) {
    result.bytes += record.bytes; result.usable += record.usable; ++result.blocks;
  }
  pthread_mutex_unlock(&mutex);
  return result;
}

void distribution(unsigned selected) {
  pthread_mutex_lock(&mutex);
  std::printf("{\"phase\":\"setup_heap_distribution\",\"requested_blocks\":[");
  bool first = true;
  for (const auto& record : records) if (record.address && record.tag == selected) {
    std::printf("%s%zu", first ? "" : ",", record.bytes); first = false;
  }
  std::puts("]}");
  pthread_mutex_unlock(&mutex);
}

struct Mark {
  explicit Mark(unsigned selected) { CHECK(tag.exchange(selected) == 0); }
  ~Mark() { tag.store(0); }
};
} // namespace heap_probe

void* operator new(std::size_t bytes) { return heap_probe::allocate(bytes, alignof(std::max_align_t)); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void* operator new(std::size_t bytes, std::align_val_t alignment) { return heap_probe::allocate(bytes, std::size_t(alignment)); }
void* operator new[](std::size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
  try { return ::operator new(bytes); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  try { return ::operator new[](bytes); } catch (...) { return nullptr; }
}
void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  try { return ::operator new(bytes, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  try { return ::operator new[](bytes, alignment); } catch (...) { return nullptr; }
}
void operator delete(void* address) noexcept { heap_probe::deallocate(address); }
void operator delete[](void* address) noexcept { ::operator delete(address); }
void operator delete(void* address, std::size_t) noexcept { ::operator delete(address); }
void operator delete[](void* address, std::size_t) noexcept { ::operator delete(address); }
void operator delete(void* address, std::align_val_t) noexcept { ::operator delete(address); }
void operator delete[](void* address, std::align_val_t) noexcept { ::operator delete(address); }
void operator delete(void* address, std::size_t, std::align_val_t) noexcept { ::operator delete(address); }
void operator delete[](void* address, std::size_t, std::align_val_t) noexcept { ::operator delete(address); }
void operator delete(void* address, const std::nothrow_t&) noexcept { ::operator delete(address); }
void operator delete[](void* address, const std::nothrow_t&) noexcept { ::operator delete(address); }
void operator delete(void* address, std::align_val_t, const std::nothrow_t&) noexcept { ::operator delete(address); }
void operator delete[](void* address, std::align_val_t, const std::nothrow_t&) noexcept { ::operator delete(address); }

namespace oa = orbital::allocation;
namespace ad = orbital::allocation::detail;
namespace cs = compact_shape;
namespace cd = compact_descriptor;
namespace v = orbital_bench::versioned;
constexpr std::size_t objects = 64;

template<class T> T need(orbital::Result<T> result) {
  CHECK(result); return std::move(*result);
}
void need(orbital::Result<void> result) { CHECK(result); }

std::array<std::size_t, 4> offsets(const v::Shape& physical, bool grouped) {
  std::array<std::size_t, 4> result{};
  std::size_t next = 0;
  if (grouped) for (std::size_t i = 0; i < physical.part_count; ++i) {
    const auto alignment = physical.parts[i].alignment;
    next = (next + alignment - 1) & ~(alignment - 1);
    result[i] = next; next += physical.parts[i].body_bytes;
  }
  return result;
}

struct Fixture {
  orbital::worker::Lane lane;
  orbital::Ref<oa::Arena> arena;
  oa::Pool* pool{};
  std::size_t stack{}, guard{};
  Fixture() {
    need(orbital::worker::adopt(lane));
    heap_probe::Mark setup(1);
    arena = need(oa::open_arena(1ull << 30));
    pool = need(oa::add_pool(*arena, lane));
    pthread_attr_t attributes;
    CHECK(!pthread_getattr_np(arena->state->reclaimer.native.native_handle(), &attributes));
    CHECK(!pthread_attr_getstacksize(&attributes, &stack));
    CHECK(!pthread_attr_getguardsize(&attributes, &guard));
    CHECK(!pthread_attr_destroy(&attributes));
  }
  void settle(bool closing) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (;;) {
      const auto progress = closing ? oa::close(*pool) : oa::trim_cold(*pool, 0);
      CHECK(progress.kind() != orbital::Progress::Kind::failed);
      if (progress.kind() == orbital::Progress::Kind::done) return;
      orbital::worker::poll(lane, 64);
      CHECK(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
  }
  ~Fixture() {
    settle(true); need(oa::remove_pool(*pool)); arena.reset();
    orbital::worker::relinquish(lane);
  }
};

struct Uses { std::size_t active, users, capacity, held; };
struct Observation {
  std::size_t records{}, encoded_body_bytes{};
  unsigned part_count{};
  std::array<std::size_t, 4> part_bytes{};
};
Uses snapshot(const char* phase, Fixture& fixture, const Observation& physical,
    bool grouped, compact_version_probe::Memory& metadata) {
  const auto stats = oa::statistics(*fixture.pool);
  const auto setup = heap_probe::totals(1), inventory = heap_probe::totals(2);
  const auto& pool = *fixture.pool->internal;
  std::size_t controls = 0, storage_slots = 0, active = 0, users = 0, capacity = 0, held = 0;
  for (const auto& group : pool.groups) {
    controls += group->geometry->control_bytes(); storage_slots += group->uses.size();
    for (const auto& storage : group->uses) {
      const auto count = storage.users.load(std::memory_order_relaxed);
      CHECK(count < ad::returning);
      if (!count) continue;
      ++active; users += count; held += storage.held_extent;
      capacity += group->geometry->bound_capacity({storage.identity});
    }
  }
  for (const auto& window : pool.windows) {
    ++storage_slots;
    const auto count = window->use.users.load(std::memory_order_relaxed);
    CHECK(count < ad::returning);
    if (count) { ++active; users += count; held += window->use.held_extent; capacity += window->reserved; }
  }
  std::printf("{\"phase\":\"%s\",\"objects\":%zu,\"records\":%zu,\"parts\":%u,"
      "\"owners\":\"%s\",\"encoded_body_bytes_per_object\":%zu,"
      "\"setup_heap_requested\":%zu,\"setup_heap_usable\":%zu,\"setup_heap_blocks\":%zu,"
      "\"inventory_heap_requested\":%zu,\"inventory_heap_usable\":%zu,\"inventory_heap_blocks\":%zu,"
      "\"control_mapping_bytes\":%zu,\"stage1_metadata_requested\":%zu,"
      "\"lane_record_bytes\":%zu,\"reclaimer_stack_va_bytes\":%zu,\"reclaimer_guard_va_bytes\":%zu,"
      "\"charged_metadata_bytes\":%llu,\"mapped_body_bytes\":%llu,\"idle_body_bytes\":%llu,"
      "\"reserved_body_and_binding_va_bytes\":%llu,\"arena_charged_bytes\":%llu,"
      "\"groups\":%zu,\"windows\":%zu,\"storage_record_capacity\":%zu,"
      "\"active_storage_records\":%zu,\"physical_users\":%zu,\"active_home_capacity\":%zu,"
      "\"full_held_extent_bytes\":%zu,\"shared_shape_and_realization_requested\":%zu}\n",
      phase, objects, physical.records, physical.part_count, grouped ? "grouped" : "independent", physical.encoded_body_bytes,
      setup.bytes, setup.usable, setup.blocks, inventory.bytes, inventory.usable, inventory.blocks,
      controls, setup.bytes + inventory.bytes + controls, sizeof(fixture.lane), fixture.stack, fixture.guard,
      static_cast<unsigned long long>(stats.metadata_bytes), static_cast<unsigned long long>(stats.mapped_bytes),
      static_cast<unsigned long long>(stats.idle_bytes), static_cast<unsigned long long>(stats.reserved_bytes),
      static_cast<unsigned long long>(fixture.arena->charged_bytes.load()), pool.groups.size(), pool.windows.size(),
      storage_slots, active, users, capacity, held, metadata.live);
  return {active, users, capacity, held};
}

std::uint64_t exported_value(cs::Export* escaped, bool columns, std::size_t row) {
  const auto address = cs::view(escaped).address();
  if (columns) {
    const auto source = v::sp::dense_source<v::Column<20>>{
        static_cast<const std::uint8_t*>(static_cast<const void*>(address)), cs::records(escaped)};
    return v::sp::bind_decoder<std::uint64_t>(source).get_unchecked(row);
  }
  return cs::tuple_law(escaped).read_unchecked<2>(
      static_cast<const v::tp::byte*>(static_cast<const void*>(address)) + row * v::tuple_bytes);
}

void profile(v::Format format, std::size_t records, bool grouped) {
  Fixture fixture;
  compact_version_probe::Memory metadata;
  Observation observed{.records = records};
  std::size_t extent = 0;
  std::array<cs::Realization*, objects> realizations{};
  cs::Shape* shared_shape = nullptr;
  {
    // Schema, source descriptors, writable aliases and decoded-oracle scratch
    // are construction-only. They end before resident and escape observations.
    v::Schema schema;
    const auto physical = need(v::shape(schema, format, records));
    const auto layout = offsets(physical, grouped);
    extent = layout[physical.part_count - 1] + physical.parts[physical.part_count - 1].body_bytes;
    observed.part_count = physical.part_count;
    for (std::size_t i = 0; i < physical.part_count; ++i) {
      observed.part_bytes[i] = physical.parts[i].body_bytes;
      observed.encoded_body_bytes += physical.parts[i].body_bytes;
    }
    snapshot("setup", fixture, observed, grouped, metadata);
    if (format == v::Format::rows && records == 3 && !grouped) heap_probe::distribution(1);
    for (std::size_t object = 0; object < objects; ++object) {
      std::array<oa::Allocation, 4> allocations{};
      std::array<v::WritePart, 4> writable{};
      std::array<cd::PartSource, 4> proofs{};
      std::array<cs::BodySource, 4> bodies{};
      for (std::size_t i = 0; i < (grouped ? 1 : physical.part_count); ++i) {
        heap_probe::Mark inventory(2);
        allocations[i] = need(oa::allocate(*fixture.pool, {
            .bytes = grouped ? extent : physical.parts[i].body_bytes,
            .alignment = physical.parts[i].alignment}));
      }
      for (std::size_t i = 0; i < physical.part_count; ++i) {
        auto& allocation = allocations[grouped ? 0 : i];
        const auto body = orbital::MutableBytes{allocation.address + layout[i], physical.parts[i].body_bytes};
        writable[i] = {physical.parts[i].id, body};
        proofs[i] = {physical.parts[i].id, body, physical.parts[i].interpretation, 0, physical.parts[i].alignment, {}};
        bodies[i] = {physical.parts[i].id, body, {}, &allocation};
      }
      std::array<orbital_bench::Row, 64> rows{};
      for (std::size_t row = 0; row < records; ++row)
        rows[row] = {row * 2 + 1, {row * 2 + 3, std::uint32_t(3000 + object * 64 + row), std::uint32_t(row + 7)}};
      v::KernelCosts costs;
      need(v::construct_into(schema, physical, std::span(writable).first(physical.part_count),
          std::span(rows).first(records), costs));
      if (!shared_shape) shared_shape = need(cs::prove_fixture(metadata, schema, {73, 1},
          physical.interpretation, std::span(proofs).first(physical.part_count)));
      realizations[object] = need(cs::capture(metadata,
          {shared_shape, records, grouped ? cs::Owners::grouped_allocation : cs::Owners::independent_allocation},
          std::span(bodies).first(physical.part_count)));
      for (auto& allocation : allocations) CHECK(!allocation);
      const cs::Selection selected{realizations[object]};
      std::array<v::ReadPart, 4> readable{};
      for (std::size_t i = 0; i < physical.part_count; ++i)
        readable[i] = {physical.parts[i].id,
            {need(selected.part(i)).address(), physical.parts[i].body_bytes}, physical.parts[i].interpretation};
      auto decoded = need(v::ReadData::bind(schema, physical.interpretation,
          std::span(readable).first(physical.part_count)));
      for (std::size_t row = 0; row < records; ++row) CHECK(need(decoded.row(row)) == rows[row]);
    }
  }
  const auto collected = snapshot("collection", fixture, observed, grouped, metadata);
  CHECK(collected.active == objects * (grouped ? 1 : observed.part_count));
  CHECK(collected.users == collected.active && collected.held == objects * (grouped ? extent : observed.encoded_body_bytes));
  // This standalone ownership census has no catalog. Its internal fixture call
  // supplies a diagnostic binding; it is not a public logical-selection API.
  auto* escaped = need(cs::detail::retain_part({realizations[0]}, format == v::Format::columns ? 2 : 0,
      {{0, 1}, 1}, cs::Interpretation::shared_shape));
  for (auto* realization : realizations) cs::release(realization);
  cs::release(shared_shape);
  CHECK(exported_value(escaped, format == v::Format::columns, 0) == 3000);
  CHECK(exported_value(escaped, format == v::Format::columns, records - 1) == 3000 + records - 1);
  const auto escaped_uses = snapshot("one_part_escape", fixture, observed, grouped, metadata);
  const auto selected_part = format == v::Format::columns ? 2 : 0;
  CHECK(escaped_uses.active == 1 && escaped_uses.users == 1);
  CHECK(escaped_uses.held == (grouped ? extent : observed.part_bytes[selected_part]));
  fixture.settle(false);
  snapshot("one_part_escape_after_idle_trim", fixture, observed, grouped, metadata);
  CHECK(oa::close(*fixture.pool).kind() != orbital::Progress::Kind::done);
  cs::release(escaped); CHECK(metadata.live == 0);
  fixture.settle(true);
  snapshot("closed", fixture, observed, grouped, metadata);
}

int main() {
  for (const auto records : {3u, 64u})
    for (const auto format : {v::Format::rows, v::Format::columns})
      for (const auto grouped : {false, true}) {
        profile(format, records, grouped);
        CHECK(heap_probe::totals(1).bytes == 0 && heap_probe::totals(2).bytes == 0);
      }
}
