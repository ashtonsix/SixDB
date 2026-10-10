#include "../source/workbench/spikes/orbital-mvcc/metadata/composed/backend.h"
#include "../source/workbench/spikes/orbital-mvcc/performance/oracle.h"
#include <allocation/internal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <source_location>

namespace c = orbital_mvcc::composed;
namespace p = orbital_versioned_performance;
namespace v = orbital_bench::versioned;
namespace cs = compact_shape;
namespace oa = orbital::allocation;
using namespace orbital_bench;

template<class T> void check(const T& value,
    std::source_location where = std::source_location::current()) {
  if (static_cast<bool>(value)) return;
  std::fprintf(stderr, "check failed: %s:%u\n", where.file_name(), where.line());
  std::abort();
}

enum class Choice { keep, discard, promote };
const char* name(Choice choice) {
  switch (choice) {
    case Choice::keep: return "keep";
    case Choice::discard: return "discard";
    case Choice::promote: return "promote";
  }
  std::abort();
}
const char* name(v::Format format) { return format == v::Format::rows ? "rows" : "columns"; }
std::size_t parts(v::Format format) { return format == v::Format::rows ? 1 : 4; }

// Test-only mutable view of an actually mutable Backend; no private access or
// changed production composition is needed for the two experimental controls.
c::Catalog& catalog(c::Inline& owner) { return const_cast<c::Catalog&>(owner.catalog()); }

struct Image {
  std::size_t parts{}, body_bytes{}, capacity_bytes{}, metadata_bytes{};
  v::Format format{};
};

Image image(c::Body body) {
  Image result;
  result.parts = body.selected.part_count(); result.format = body.format();
  result.metadata_bytes = cs::footprint(body.selected.realization).requested_bytes;
  check(body.selected.realization->owners == orbital::PartOwners::independent_allocation);
  for (std::size_t i = 0; i < result.parts; ++i) {
    result.body_bytes += p::need(cs::geometry(body.selected.shape(), i).extent(body.size()));
    const auto* use = orbital::object_detail::allocated_bodies(body.selected.realization)[i].owner;
    const auto* route = oa::detail::route(*use);
    if (route->finite) {
      const auto* group = static_cast<const oa::detail::Group*>(route->context);
      result.capacity_bytes += group->geometry->bound_capacity({use->identity()});
    } else {
      result.capacity_bytes += static_cast<const oa::detail::Window*>(route->context)->prepared;
    }
  }
  return result;
}

struct Snapshot {
  c::Accounting allocation;
  compact_history::Statistics logical;
  Image primary;
  std::optional<Image> alternative;
};

Snapshot observe(c::Inline& owner) {
  Snapshot result;
  {
    auto scope = owner.read();
    result.primary = image(owner.body(scope, 0));
    auto selected = scope.bind_alternative(owner.corpus().images[0].address.object);
    if (selected) {
      // Material is reached through the existing Body; this captures the same
      // selected alternative, not a second physical image.
      result.alternative = image(owner.body(scope, 0, true));
    } else check(selected.error() == orbital::Error::absent);
  }
  result.allocation = owner.accounting(); result.logical = owner.statistics();
  check(result.logical.objects == 1 && result.logical.versions == 1 && result.logical.scopes == 0);
  check(result.logical.alternative_count == bool(result.alternative));
  return result;
}

void emit(c::Inline& owner, Choice choice, v::Format initial, const char* phase,
    std::size_t owners, std::size_t targets, std::size_t exported_parts) {
  const auto s = observe(owner);
  const auto& a = s.allocation;
  const auto catalog_parts = s.primary.parts + (s.alternative ? s.alternative->parts : 0);
  const auto catalog_capacity = s.primary.capacity_bytes + (s.alternative ? s.alternative->capacity_bytes : 0);
  const auto catalog_body = s.primary.body_bytes + (s.alternative ? s.alternative->body_bytes : 0);
  const auto catalog_metadata = s.primary.metadata_bytes + (s.alternative ? s.alternative->metadata_bytes : 0);
  std::printf("{\"choice\":\"%s\",\"initial\":\"%s\",\"phase\":\"%s\",\"cut\":%llu,"
      "\"primary_format\":\"%s\",\"alternative_format\":\"%s\",\"objects\":%zu,\"versions\":%zu,"
      "\"scopes\":%zu,\"catalog_images\":%zu,\"catalog_parts\":%zu,\"catalog_body_bytes\":%zu,"
      "\"catalog_capacity_bytes\":%zu,\"catalog_representation_metadata\":%zu,\"external_owners\":%zu,"
      "\"conversion_targets\":%zu,\"exported_parts\":%zu,\"active_allocations\":%zu,\"live_capacity_bytes\":%zu,"
      "\"held_extent_bytes\":%s,\"pmr_live\":%zu,\"pmr_peak\":%zu,\"pmr_calls\":%zu,"
      "\"shared_shape_bytes\":%zu,\"pool_metadata\":%llu,\"pool_mapped\":%llu,\"pool_idle\":%llu,"
      "\"pool_reserved\":%llu,\"pool_setup_and_inventory\":%zu,\"arena_charge\":%llu}\n",
      name(choice), name(initial), phase, static_cast<unsigned long long>(owner.cut()), name(s.primary.format),
      s.alternative ? name(s.alternative->format) : "absent", s.logical.objects, s.logical.versions,
      s.logical.scopes, std::size_t{1} + bool(s.alternative), catalog_parts, catalog_body, catalog_capacity,
      catalog_metadata, owners, targets, exported_parts, a.active_allocations, a.live_capacity_bytes,
      a.held_extent_bytes ? std::to_string(*a.held_extent_bytes).c_str() : "null", a.metadata_live, a.metadata_peak,
      a.metadata_calls, a.shared_shape_bytes, static_cast<unsigned long long>(a.pool.metadata_bytes),
      static_cast<unsigned long long>(a.pool.mapped_bytes), static_cast<unsigned long long>(a.pool.idle_bytes),
      static_cast<unsigned long long>(a.pool.reserved_bytes), a.pool_setup_and_inventory_bytes,
      static_cast<unsigned long long>(a.arena_charge));
}

void verify(c::Inline& owner, const p::Golden& oracle, v::Format primary,
    std::optional<v::Format> alternative, std::uint64_t& comparisons) {
  {
    auto scope = owner.read();
    auto body = owner.body(scope, 0);
    check(body.format() == primary);
    for (const auto& [key, record] : oracle) { check(body.get(key) == record); ++comparisons; }
    if (alternative) {
      const auto converted = owner.body(scope, 0, true);
      check(converted.format() == *alternative);
      for (const auto& [key, record] : oracle) { check(converted.get(key) == record); ++comparisons; }
    } else {
      const auto missing = scope.bind_alternative(owner.corpus().images[0].address.object);
      check(!missing && missing.error() == orbital::Error::absent);
      // This fallback belongs to the existing composed caller, not the catalog.
      check(owner.body(scope, 0, true).format() == primary);
    }
  }
  for (auto family : {"point-hit", "point-miss", "dependent-16", "scan-16"}) {
    const auto trace = make_trace(family, 8192, 7, 8132);
    auto direct = owner.bodies<1>();
    auto cached = owner.bodies<64>();
    for (const auto& op : trace.operations) {
      Outcome native;
      direct.consume_into(0, op, native);
      check(native == p::oracle(oracle, op));
      check(cached.body(0).consume(op) == p::oracle(oracle, op)); comparisons += 2;
    }
  }
}

void choose(c::Inline& owner, Choice choice, orbital::ObjectId id) {
  if (choice == Choice::discard) check(catalog(owner).discard_alternative(id));
  if (choice == Choice::promote) check(catalog(owner).promote_alternative(id));
}

struct PartRelease { void operator()(cs::Export* part) const { cs::release(part); } };
using Export = std::unique_ptr<cs::Export, PartRelease>;

void run(v::Format initial, Choice choice) {
  p::Corpus corpus(8192, 1, 8132);
  p::Golden oracle;
  for (const auto& row : corpus.images[0].rows) oracle.emplace(row.key, row.record);
  std::uint64_t comparisons{};
  c::Inline owner(corpus, 64 * 1024 * 1024, initial);
  const auto id = corpus.images[0].address.object;
  const auto cut = owner.cut();
  const auto opposite = initial == v::Format::rows ? v::Format::columns : v::Format::rows;
  const auto original = observe(owner);
  check(original.allocation.active_allocations == parts(initial));
  check(original.primary.body_bytes > 65535 || initial == v::Format::columns);
  emit(owner, choice, initial, "initial", 0, 0, 0);

  c::Catalog::Owner original_owner;
  std::optional<c::Catalog::ConversionTarget> original_target;
  Export original_part;
  std::vector<std::byte> original_part_bytes;
  {
    auto basis = owner.read();
    auto bound = p::need(catalog(owner).bound(basis, id));
    original_owner = c::Catalog::Owner::retain(bound.selection().realization);
    original_target.emplace(p::need(catalog(owner).conversion(basis, id)));
    original_part.reset(p::need(bound.export_part(0)));
    const auto size = p::need(cs::geometry(bound.selection().shape(), 0).extent(bound.selection().records()));
    const auto* bytes = cs::view(original_part.get()).address();
    original_part_bytes.assign(bytes, bytes + size);
  }
  const auto check_part = [&] {
    const auto association = cs::binding(original_part.get());
    check(association.object == id && association.version == cut);
    check(std::memcmp(cs::view(original_part.get()).address(), original_part_bytes.data(), original_part_bytes.size()) == 0);
  };

  check(owner.convert(0, opposite));
  verify(owner, oracle, initial, opposite, comparisons);
  emit(owner, choice, initial, "opposite_adopted", 1, 1, 1);
  check(owner.accounting().active_allocations == parts(initial) + parts(opposite));
  {
    auto old = owner.read();
    const auto old_primary = owner.body(old, 0);
    const auto old_alternative = owner.body(old, 0, true);
    if (choice == Choice::promote) {
      auto refused = catalog(owner).promote_alternative(id);
      check(!refused && refused.error() == orbital::Error::unavailable);
    }
    if (choice == Choice::discard) {
      choose(owner, choice, id);
      // Previously selected primary and alternative still denote exact bytes.
      for (const auto& [key, record] : oracle) {
        check(old_primary.get(key) == record && old_alternative.get(key) == record); comparisons += 2;
      }
      check(old_primary.format() == initial && old_alternative.format() == opposite);
      check(owner.accounting().active_allocations == parts(initial) + parts(opposite));
      auto late = owner.read();
      check(owner.body(late, 0).format() == initial);
      auto absent = late.bind_alternative(id); check(!absent && absent.error() == orbital::Error::absent);
    }
  }
  if (choice != Choice::discard) choose(owner, choice, id);
  check(owner.cut() == cut); check_part();
  const auto intermediate_primary = choice == Choice::promote ? opposite : initial;
  verify(owner, oracle, intermediate_primary,
      choice == Choice::keep ? std::optional{opposite} : std::nullopt, comparisons);
  emit(owner, choice, initial, "opposite_choice", 1, 1, 1);
  check(owner.accounting().active_allocations ==
      (choice == Choice::discard ? parts(initial) : parts(initial) + parts(opposite)));

  c::Catalog::Owner final_owner;
  {
    auto current = owner.read();
    auto converted = p::need(owner.prepare_conversion(current, 0, initial));
    final_owner = converted.replacement;
    // Original physical source still authorizes this exact unchanged logical
    // version, including after the experiment promoted a different primary.
    check(catalog(owner).adopt(*original_target, final_owner));
  }
  emit(owner, choice, initial, "roundtrip_adopted", 2, 1, 1);
  verify(owner, oracle, intermediate_primary, initial, comparisons);
  choose(owner, choice, id);
  check(owner.cut() == cut); check_part();
  verify(owner, oracle, initial,
      choice == Choice::keep ? std::optional{initial} : std::nullopt, comparisons);
  emit(owner, choice, initial, "roundtrip_choice", 2, 1, 1);
  check(owner.accounting().active_allocations == 2 * parts(initial));

  final_owner = {};
  emit(owner, choice, initial, "final_external_owner_released", 1, 1, 1);
  check(owner.accounting().active_allocations ==
      (choice == Choice::discard ? parts(initial) : 2 * parts(initial)));
  original_owner = {};
  emit(owner, choice, initial, "original_external_owner_released", 0, 1, 1);
  check(owner.accounting().active_allocations ==
      (choice == Choice::discard ? parts(initial) : 2 * parts(initial)));
  original_target.reset();
  check_part();
  emit(owner, choice, initial, "original_target_released", 0, 0, 1);
  check(owner.accounting().active_allocations ==
      (choice == Choice::promote ? parts(initial) + 1 :
       choice == Choice::keep ? 2 * parts(initial) : parts(initial)));
  original_part.reset();
  owner.drain(); owner.poll(65536);
  const auto final = observe(owner);
  check(final.allocation.active_allocations == (choice == Choice::keep ? 2 * parts(initial) : parts(initial)));
  check(final.allocation.live_capacity_bytes == final.primary.capacity_bytes +
      (final.alternative ? final.alternative->capacity_bytes : 0));
  verify(owner, oracle, initial,
      choice == Choice::keep ? std::optional{initial} : std::nullopt, comparisons);
  emit(owner, choice, initial, "only_catalog_owners", 0, 0, 0);
  std::printf("{\"choice\":\"%s\",\"initial\":\"%s\",\"comparisons\":%llu,\"passed\":true}\n",
      name(choice), name(initial), static_cast<unsigned long long>(comparisons));
}

int main() {
  for (auto initial : {v::Format::rows, v::Format::columns})
    for (auto choice : {Choice::keep, Choice::discard, Choice::promote}) run(initial, choice);
  std::puts("{\"passed\":true,\"cases\":6,\"timings\":false,\"teardown_checked\":true}");
}
