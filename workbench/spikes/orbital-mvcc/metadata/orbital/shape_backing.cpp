// Requested-state accounting with actual immutable Tuple/SeriesPack payloads.
// Fixture source arrays/Schema are temporary caller state, not backend metadata.
#include "candidates/shape_policy.h"
#include "shape/native_fixture.h"
#include "../../../../../orbital/src/allocation/internal.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <source_location>
#include <vector>

namespace ch = compact_history;
namespace cs = compact_shape;
namespace cd = compact_descriptor;
namespace v = orbital_bench::versioned;
namespace oa = orbital::allocation;

template<class T> void check(const T& condition,
    std::source_location where = std::source_location::current()) {
  if (static_cast<bool>(condition)) return;
  std::fprintf(stderr, "check failed: %s:%u\n", where.file_name(), where.line());
  std::abort();
}

template<class T> T take(orbital::Result<T> result) {
  check(result); return std::move(*result);
}

struct External {
  ch::Memory owners, payload;
  std::size_t bodies{};
};

struct DropBody {
  External* external;
  std::size_t bytes, alignment;
  void operator()(const void* address) const noexcept {
    external->payload.deallocate(const_cast<void*>(address), bytes, alignment);
    --external->bodies;
  }
};

std::shared_ptr<const void> body(External& external, std::size_t bytes,
    std::size_t alignment) {
  auto* address = external.payload.allocate(bytes, alignment);
  ++external.bodies;
  return {address, DropBody{&external, bytes, alignment},
      std::pmr::polymorphic_allocator<std::byte>(&external.owners)};
}

constexpr std::size_t records = 3;
orbital::ObjectId id(std::size_t object) { return {79, object + 1}; }

struct PhysicalUses { std::size_t active{}; };
PhysicalUses uses(const oa::Pool* pool) {
  PhysicalUses result;
  if (!pool) return result;
  const auto observe = [&](const oa::Storage& storage) {
    if (storage.users.load(std::memory_order_relaxed)) {
      ++result.active;
    }
  };
  for (const auto& group : pool->internal->groups)
    for (const auto& storage : group->uses.slots) observe(storage);
  for (const auto& window : pool->internal->windows) observe(window->uses[0]);
  return result;
}

template<class Store> struct Fixture {
  ch::Memory metadata;
  External external;
  Store store{metadata};
  oa::Pool* pool{};
  v::Format format;
  cs::Shape* proof{};
  bool grouped, writer_basis_held;
  unsigned parts{};
  std::size_t logical_body_bytes{};
  std::size_t realization_bytes{}, owner_bytes{}, shape_bytes{}, input_capacity{};
  std::uint64_t cut{};

  Fixture(v::Format format, bool grouped, bool writer_basis_held, oa::Pool* pool)
      : pool(pool), format(format), grouped(grouped), writer_basis_held(writer_basis_held) {}

  ~Fixture() { cs::release(proof); }

  typename Store::Owner encode(const v::Schema& schema, const v::Shape& physical,
      std::size_t object, std::uint64_t version) {
    std::array<std::size_t, 4> offsets{};
    std::size_t total{}, alignment = 1;
    for (std::size_t i = 0; i < physical.part_count; ++i) {
      const auto& part = physical.parts[i];
      alignment = std::max(alignment, part.alignment);
      total = (total + part.alignment - 1) / part.alignment * part.alignment;
      offsets[i] = total;
      total += part.body_bytes + part.readable_padding;
    }
    std::shared_ptr<const void> group;
    std::array<oa::Allocation, 4> allocations;
    if (grouped) {
      if (pool) allocations[0] = take(oa::allocate(*pool, {.bytes = total, .alignment = alignment}));
      else group = body(external, total, alignment);
    }
    std::array<v::WritePart, 4> writable{};
    std::array<cd::PartSource, 4> sources{};
    std::array<cs::BodySource, 4> bodies{};
    for (std::size_t i = 0; i < physical.part_count; ++i) {
      const auto& part = physical.parts[i];
      std::shared_ptr<const void> owner;
      oa::Allocation* allocation{};
      if (pool) {
        allocation = &allocations[grouped ? 0 : i];
        if (!grouped) *allocation = take(oa::allocate(*pool,
            {.bytes = part.body_bytes, .alignment = part.alignment,
             .readable_padding = part.readable_padding}));
      } else owner = grouped ? group : body(external,
          part.body_bytes + part.readable_padding, part.alignment);
      auto* address = (pool ? allocation->address : static_cast<const std::byte*>(owner.get())) +
          (grouped ? offsets[i] : 0);
      orbital::Bytes bytes{address, part.body_bytes};
      writable[i] = {part.id, {const_cast<std::byte*>(address), part.body_bytes}};
      sources[i] = {part.id, bytes, part.interpretation,
          part.readable_padding, part.alignment, owner};
      bodies[i] = {part.id, bytes, std::move(owner), allocation};
    }
    std::array<orbital_bench::Row, records> rows;
    for (std::size_t i = 0; i < records; ++i)
      rows[i] = {100 + i, {200 + i,
          static_cast<std::uint32_t>(version * 1000 + object * 10 + i),
          static_cast<std::uint32_t>(400 + i)}};
    v::KernelCosts costs;
    check(v::construct_into(schema, physical,
        std::span(writable).first(physical.part_count), rows, costs));
    if (!proof) {
      proof = take(cs::prove_fixture(metadata, schema, {83, 1},
          physical.interpretation, std::span(sources).first(physical.part_count)));
      shape_bytes = cs::shape_bytes(proof);
    }
    const auto owners = pool ? (grouped ? cs::Owners::grouped_allocation : cs::Owners::independent_allocation) :
        (grouped ? cs::Owners::grouped_shared : cs::Owners::independent_shared);
    auto result = take(store.capture({proof, records, owners},
        std::span(bodies).first(physical.part_count)));
    const auto footprint = cs::footprint(result.selection().realization);
    realization_bytes = footprint.requested_bytes;
    owner_bytes = footprint.owner_bytes;
    return result;
  }

  void publish(std::size_t objects) {
    // Construction controls end here. Resident reports and escaping Parts must
    // work from their admitted proof, without an uncharged retained Schema.
    v::Schema schema;
    auto physical = take(v::shape(schema, format, records));
    parts = physical.part_count;
    logical_body_bytes = 0;
    for (std::size_t i = 0; i < parts; ++i) logical_body_bytes += physical.parts[i].body_bytes;
    std::optional<typename Store::Scope> basis;
    basis.emplace(store);
    std::vector<typename Store::Change> changes;
    changes.reserve(objects);
    for (std::size_t object = 0; object < objects; ++object)
      changes.push_back({id(object), encode(schema, physical, object, cut + 1)});
    input_capacity = changes.capacity() * sizeof(typename Store::Change);
    typename Store::Prepared prepared(store);
    check(store.prepare(*basis, changes, prepared));
    // Prepared has its own exact Core pin. Source reads have ended;
    // this arm does not keep an otherwise unnecessary writer cut discoverable.
    if (!writer_basis_held) basis.reset();
    check(store.install(prepared));
    ++cut;
  }

  std::uint64_t read(cs::Selection selection, std::size_t row) const {
    check(selection && selection.records() == records);
    if (format == v::Format::rows) {
      const auto* first = static_cast<const v::tp::byte*>(
          static_cast<const void*>(take(selection.part(std::size_t{0})).address()));
      return cs::tuple_law(selection.shape()).read_unchecked<2>(first + row * v::tuple_bytes);
    }
    auto source = v::sp::dense_source<v::Column<20>>{
        static_cast<const std::uint8_t*>(static_cast<const void*>(
            take(selection.part(std::size_t{2})).address())), records};
    return v::sp::bind_decoder<std::uint64_t>(source).get_unchecked(row);
  }

  void report(const char* backend, const char* phase, std::size_t objects) const {
    const auto stats = store.statistics();
    const auto realizations = stats.versions * realization_bytes;
    const auto catalog = metadata.live - shape_bytes - realizations;
    std::size_t object_capacity{}, debt_capacity{};
    if constexpr (requires { stats.object_directory_capacity_bytes; })
      object_capacity = stats.object_directory_capacity_bytes;
    if constexpr (requires { stats.debt_capacity_bytes; }) debt_capacity = stats.debt_capacity_bytes;
    const auto physical = pool ? oa::statistics(*pool) : oa::Statistics{};
    const auto storage = uses(pool);
    std::printf("{\"backend\":\"%s\",\"phase\":\"%s\",\"parts\":%u,\"objects\":%zu,"
        "\"owners\":\"%s-%s\",\"writer_basis\":\"%s\",\"requested_metadata\":%zu,\"metadata_peak\":%zu,"
        "\"catalog_total\":%zu,\"core\":%zu,\"entry_record_bytes\":%zu,"
        "\"object_directory_capacity\":%zu,\"debt_capacity\":%zu,\"history_capacity\":%zu,"
        "\"primary_registry_capacity\":%zu,\"versions\":%zu,\"realization_total\":%zu,"
        "\"realization_header\":%zu,\"address_slots\":%zu,\"physical_owner_slots\":%zu,"
        "\"shared_shape\":%zu,\"external_owner_control_requested\":%zu,"
        "\"logical_body_bytes\":%zu,\"external_payload_requested\":%zu,\"external_bodies\":%zu,"
        "\"metadata_resource_record\":%zu,\"scope_record\":%zu,\"input_change_peak_capacity\":%zu,"
        "\"pool_metadata_charged\":%llu,\"pool_mapped_body\":%llu,\"pool_reserved_va\":%llu,"
        "\"pool_idle_body\":%llu,\"active_storages\":%zu}\n",
        backend, phase, parts, objects, grouped ? "grouped" : "independent",
        pool ? "allocation" : "shared",
        writer_basis_held ? "through-install" : "closed-before-install",
        metadata.live, metadata.peak, catalog, stats.core_bytes, stats.entry_bytes,
        object_capacity, debt_capacity, stats.history_capacity_bytes,
        stats.descriptor_directory_capacity_bytes, stats.versions, realizations,
        stats.versions * 24, stats.versions * parts * 8,
        stats.versions * owner_bytes, shape_bytes, external.owners.live,
        stats.versions * logical_body_bytes, external.payload.live, external.bodies,
        sizeof(metadata), sizeof(typename Store::Scope), input_capacity,
        static_cast<unsigned long long>(physical.metadata_bytes),
        static_cast<unsigned long long>(physical.mapped_bytes),
        static_cast<unsigned long long>(physical.reserved_bytes),
        static_cast<unsigned long long>(physical.idle_bytes), storage.active);
  }
};

template<class Store> void run(const char* backend, v::Format format,
    bool grouped, std::size_t objects, bool writer_basis_held, bool native) {
  std::optional<cs::probe::NativeFixture> allocator;
  if (native) { allocator.emplace(); check(allocator->open()); }
  Fixture<Store> fixture(format, grouped, writer_basis_held, allocator ? allocator->pool : nullptr);
  fixture.publish(objects);
  fixture.store.drain();
  fixture.report(backend, "initial", objects);
  std::optional<typename Store::Scope> old;
  old.emplace(fixture.store);
  for (unsigned publication = 1; publication < 8; ++publication) {
    fixture.publish(objects);
    fixture.store.drain();
  }
  {
    typename Store::Scope current(fixture.store);
    for (std::size_t object = 0; object < objects; ++object)
      for (std::size_t row = 0; row < records; ++row) {
        check(fixture.read(old->bind(id(object)), row) == 1000 + object * 10 + row);
        check(fixture.read(current.bind(id(object)), row) == 8000 + object * 10 + row);
      }
  }
  fixture.report(backend, "old-held", objects);
  old.reset(); fixture.store.drain();
  fixture.report(backend, "old-released", objects);
  cs::Export* escaped{};
  const auto selected_part = format == v::Format::rows ? 0U : 2U;
  {
    typename Store::Scope current(fixture.store);
    auto exact = take(fixture.store.bound(current, id(0)));
    escaped = take(exact.export_part(selected_part, cs::Interpretation::selected_copy));
  }
  cs::release(std::exchange(fixture.proof, nullptr));
  fixture.store = Store{};
  if (native) check(uses(fixture.pool).active == 1);
  else check(fixture.external.bodies == 1);
  check(cs::binding(escaped).object == id(0) && cs::binding(escaped).version == 8);
  const auto* address = cs::view(escaped).address();
  if (format == v::Format::rows)
    check(cs::tuple_law(escaped).read_unchecked<2>(
        static_cast<const v::tp::byte*>(static_cast<const void*>(address))) == 8000);
  else {
    auto source = v::sp::dense_source<v::Column<20>>{
        static_cast<const std::uint8_t*>(static_cast<const void*>(address)), records};
    check(v::sp::bind_decoder<std::uint64_t>(source).get_unchecked(0) == 8000);
  }
  const auto footprint = cs::footprint(escaped);
  check(fixture.metadata.live == footprint.requested_bytes + footprint.shared_shape_bytes);
  const auto logical = take(cs::geometry(escaped).extent(records));
  const auto physical = native ? allocator->statistics() : oa::Statistics{};
  const auto storage = uses(fixture.pool);
  std::printf("{\"backend\":\"%s\",\"phase\":\"narrow-escape\",\"parts\":%u,\"objects\":%zu,"
      "\"owners\":\"%s-%s\",\"writer_basis\":\"%s\",\"requested_metadata\":%zu,\"shared_shape\":%zu,"
      "\"external_owner_control_requested\":%zu,\"selected_logical_body_bytes\":%llu,"
      "\"external_payload_requested\":%zu,\"external_bodies\":%zu,\"pool_metadata_charged\":%llu,"
      "\"pool_mapped_body\":%llu,\"pool_reserved_va\":%llu,\"pool_idle_body\":%llu,"
      "\"active_storages\":%zu}\n",
      backend, fixture.parts, objects, grouped ? "grouped" : "independent",
      native ? "allocation" : "shared",
      writer_basis_held ? "through-install" : "closed-before-install",
      fixture.metadata.live, footprint.shared_shape_bytes, fixture.external.owners.live,
      static_cast<unsigned long long>(logical), fixture.external.payload.live, fixture.external.bodies,
      static_cast<unsigned long long>(physical.metadata_bytes),
      static_cast<unsigned long long>(physical.mapped_bytes),
      static_cast<unsigned long long>(physical.reserved_bytes),
      static_cast<unsigned long long>(physical.idle_bytes), storage.active);
  cs::release(escaped);
  check(!fixture.metadata.live && !fixture.external.owners.live && !fixture.external.payload.live);
  if (native) { check(uses(fixture.pool).active == 0); check(allocator->close()); }
}

int main() {
  for (const auto objects : {1U, 64U})
    for (const auto format : {v::Format::rows, v::Format::columns})
      for (bool grouped : {false, true}) {
        for (bool basis_held : {false, true})
          for (bool native : {false, true}) {
            run<ch::ShapeStore32>("shape32-hash", format, grouped, objects, basis_held, native);
            run<ch::ShapeStore16>("shape16-hash", format, grouped, objects, basis_held, native);
            run<ch::CompactShapeStore16>("shape16-directory", format, grouped, objects, basis_held, native);
            run<ch::InlineShapeStore16>("shape16-inline", format, grouped, objects, basis_held, native);
          }
      }
}
