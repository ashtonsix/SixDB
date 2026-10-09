#include "shape.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <source_location>
#include <thread>
#include <vector>

namespace cs = compact_shape;
namespace cd = compact_descriptor;
namespace v = orbital_bench::versioned;

template<class T>
void check(const T& condition, std::source_location where = std::source_location::current()) {
  if (condition) return;
  std::fprintf(stderr, "check failed: %s:%u\n", where.file_name(), where.line());
  std::abort();
}
template<class T> T take(orbital::Result<T> result) { check(result); return std::move(*result); }

struct Memory : std::pmr::memory_resource {
  std::size_t live{}, peak{}, calls{}, limit = SIZE_MAX;
  void* do_allocate(std::size_t amount, std::size_t alignment) override {
    if (amount > limit - live) throw std::bad_alloc();
    auto* result = std::pmr::new_delete_resource()->allocate(amount, alignment);
    live += amount; peak = std::max(peak, live); ++calls;
    return result;
  }
  void do_deallocate(void* value, std::size_t amount, std::size_t alignment) override {
    check(live >= amount); live -= amount;
    std::pmr::new_delete_resource()->deallocate(value, amount, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

struct Body {
  Memory* memory;
  std::byte* data;
  std::size_t size, alignment;
  std::size_t* destroyed;
  Body(Memory& m, std::size_t bytes, std::size_t align, std::size_t& count)
      : memory(&m), data(bytes ? static_cast<std::byte*>(m.allocate(bytes, align)) : nullptr),
        size(bytes), alignment(align), destroyed(&count) { if (size) std::memset(data, 0, size); }
  ~Body() { if (size) memory->deallocate(data, size, alignment); ++*destroyed; }
  orbital::Bytes bytes() const { return {data, size}; }
};
struct Dependency {
  std::size_t* destroyed;
  ~Dependency() { ++*destroyed; }
};

std::vector<std::byte> series_description(std::uint64_t count, unsigned width = 8) {
  auto encoded = v::sp::encode_descriptor({width, 0, v::sp::geometry::local, count, {width, 0, 0}});
  check(encoded);
  std::vector<std::byte> result(encoded->size());
  std::memcpy(result.data(), encoded->data(), result.size());
  return result;
}

void series_retention(std::size_t parts, cs::Interpretation policy) {
  Memory metadata, external;
  std::size_t destroyed{}, dependency_destroyed{};
  auto dependency = std::allocate_shared<Dependency>(std::pmr::polymorphic_allocator<Dependency>(&external), &dependency_destroyed);
  auto description = series_description(3);
  const auto original_description = description;
  std::vector<cd::PartSource> sources;
  for (std::size_t i = 0; i < parts; ++i) {
    auto body = std::allocate_shared<Body>(std::pmr::polymorphic_allocator<Body>(&external), external, 8, 1, destroyed);
    body->data[0] = std::byte(i); body->data[1] = std::byte(i + 1); body->data[2] = std::byte(i + 2);
    sources.push_back({{73, i}, body->bytes(), description, 0, 1, body});
  }
  auto* shape = take(cs::prove_series(metadata, {0x713, 8}, sources, dependency));
  check(cs::shape_bytes(shape) == 56 + 32 * parts);
  std::vector<cs::BodySource> bodies;
  for (const auto& part : sources) bodies.push_back({part.id, part.body, part.backing, nullptr});
  auto* realization = take(cs::capture(metadata, {shape, parts ? 3U : 0U}, bodies));
  check(cs::footprint(realization).requested_bytes == 24 + 24 * parts);
  cs::release(shape);
  dependency.reset();
  std::fill(description.begin(), description.end(), std::byte{0xff});
  cs::Selection selected{realization};
  cs::Export* exported = nullptr;
  if (parts) {
    check(selected.part(parts - 1));
    exported = take(cs::detail::retain_part(selected, parts - 1, {{1, 9}, UINT64_C(1) << 48}, policy));
  }
  sources.clear(); bodies.clear();
  check(!destroyed);
  check(!dependency_destroyed);
  cs::release(realization);
  if (parts) {
    check(destroyed == parts - 1);
    check(!dependency_destroyed);
    check(cs::binding(exported).object == (orbital::ObjectId{1, 9}));
    check(cs::binding(exported).version == (UINT64_C(1) << 48));
    check(cs::geometry(exported).id == (orbital::PartId{73, parts - 1}));
    check(cs::records(exported) == 3 && cs::geometry(exported).extent(3) == 8);
    const auto reconstructed = take(cs::series_interpretation(exported));
    check(reconstructed.size() == original_description.size());
    for (std::size_t i = 0; i < reconstructed.size(); ++i)
      check(reconstructed[i] == std::to_integer<std::uint8_t>(original_description[i]));
    auto source = v::sp::dense_source<v::Column<8>>{
        static_cast<const std::uint8_t*>(static_cast<const void*>(cs::view(exported).address())), 3};
    check(v::sp::bind_decoder<std::uint64_t>(source).get_unchecked(1) == parts);
    const auto cost = cs::footprint(exported);
    const auto total = cost.requested_bytes + cost.shared_shape_bytes;
    check(metadata.live == total);
    check(total == (policy == cs::Interpretation::shared_shape ? 80 + 56 + 32 * parts : 168));
    std::printf("series Parts=%zu escape=%s requested=%zu shared=%zu total=%zu external=%zu\n",
        parts, policy == cs::Interpretation::shared_shape ? "shared" : "selected-copy",
        cost.requested_bytes, cost.shared_shape_bytes, total, external.live);
    cs::release(exported);
  }
  check(destroyed == parts && dependency_destroyed == 1 && !metadata.live && !external.live);
}

void fixture_proof(v::Format format, std::size_t count) {
  Memory metadata, external;
  std::size_t destroyed{};
  cs::Shape* proof{};
  std::vector<cs::BodySource> bodies;
  std::vector<cd::PartSource> source;
  std::vector<orbital_bench::Row> rows;
  for (std::size_t i = 0; i < count; ++i) rows.push_back({100 + i, {200 + i, std::uint32_t(300 + i), std::uint32_t(400 + i)}});
  {
    v::Schema schema;
    auto shape = take(v::shape(schema, format, count));
    std::vector<v::WritePart> writable;
    for (std::size_t i = 0; i < shape.part_count; ++i) {
      const auto& part = shape.parts[i];
      auto owner = std::allocate_shared<Body>(std::pmr::polymorphic_allocator<Body>(&external),
          external, part.body_bytes, part.alignment, destroyed);
      writable.push_back({part.id, {owner->data, owner->size}});
      source.push_back({part.id, owner->bytes(), part.interpretation, 0, part.alignment, owner});
      bodies.push_back({part.id, owner->bytes(), owner, nullptr});
    }
    v::KernelCosts costs;
    check(v::construct_into(schema, shape, writable, rows, costs));
    proof = take(cs::prove_fixture(metadata, schema, {18, 29}, shape.interpretation, source));
    check(cs::shape_bytes(proof) == (format == v::Format::rows ? 227 : 184));
    auto corrupt = shape.parts[shape.part_count - 1].interpretation;
    corrupt[0] ^= std::byte{1};
    auto bad_source = source;
    bad_source.back().interpretation = corrupt;
    check(!cs::prove_fixture(metadata, schema, {18, 29}, shape.interpretation, bad_source));
    auto parent = shape.interpretation;
    parent[16] ^= std::byte{1};
    check(!cs::prove_fixture(metadata, schema, {18, 29}, parent, source));
  } // Full Schema and serialized descriptions end; proof owns needed controls.
  auto* realization = take(cs::capture(metadata, {proof, count}, bodies));
  cs::release(proof);
  source.clear(); bodies.clear();
  cs::Selection selected{realization};
  check(selected.records() == count);
  if (format == v::Format::rows && count) {
    const auto* first = static_cast<const v::tp::byte*>(static_cast<const void*>(take(selected.part(0)).address()));
    const auto& law = cs::tuple_law(selected.shape());
    check(law.read_unchecked<0>(first + (count - 1) * v::tuple_bytes) == rows.back().key);
    check(law.read_unchecked<2>(first + (count - 1) * v::tuple_bytes) == rows.back().record.value);
  }
  if (!count) check(selected.part(0) && take(selected.part(0)).address() == nullptr);
  auto* exported = take(cs::detail::retain_part(selected, 0, {{7, 1}, 3}, cs::Interpretation::selected_copy));
  cs::release(realization);
  if (format == v::Format::rows && count) {
    const auto* first = static_cast<const v::tp::byte*>(static_cast<const void*>(cs::view(exported).address()));
    check(cs::tuple_law(exported).read_unchecked<1>(first) == rows.front().record.next);
    check(cs::tuple_interpretation(exported).size() == 75);
  }
  cs::release(exported);
  check(!metadata.live && !external.live);
}

void groups_and_refusals() {
  Memory metadata, external;
  std::size_t destroyed{};
  auto description = series_description(3);
  auto group = std::allocate_shared<Body>(std::pmr::polymorphic_allocator<Body>(&external), external, 32, 1, destroyed);
  std::array<cd::PartSource, 4> source;
  std::array<cs::BodySource, 4> bodies;
  std::shared_ptr<const void> alias(group, nullptr);
  for (std::size_t i = 0; i < 4; ++i) {
    source[i] = {{1, i}, {group->data + i * 8, 8}, description, 0, 1, alias};
    bodies[i] = {source[i].id, source[i].body, alias, nullptr};
  }
  auto* shape = take(cs::prove_series(metadata, {3, 4}, source));
  auto* value = take(cs::capture(metadata, {shape, 3, cs::Owners::grouped_shared}, bodies));
  check(cs::footprint(value).requested_bytes == 72);
  const auto before_invalid_export = metadata.live;
  check(!cs::detail::retain_part({value}, 0, {}, static_cast<cs::Interpretation>(99)));
  check(metadata.live == before_invalid_export);
  auto independent = std::make_shared<unsigned>(2);
  bodies[3].backing = independent;
  check(!cs::capture(metadata, {shape, 3, cs::Owners::grouped_shared}, bodies));
  bodies[3].backing = alias;
  Memory foreign;
  check(!cs::capture(foreign, {shape, 3}, bodies) && !foreign.live);
  metadata.limit = metadata.live;
  check(!cs::capture(metadata, {shape, 3}, bodies));
  check(!cs::detail::retain_part({value}, 2, {{7, 8}, 9}));
  metadata.limit = SIZE_MAX;
  auto mismatch = description;
  mismatch[8] = std::byte{4};
  source[3].interpretation = mismatch;
  check(!cs::prove_series(metadata, {3, 4}, source));
  source[3].interpretation = description;
  source[3].id = source[2].id;
  check(!cs::prove_series(metadata, {3, 4}, source));
  cs::release(value); cs::release(shape);
  source = {}; bodies = {}; alias.reset(); group.reset();
  check(!metadata.live && !external.live && destroyed == 1);
  const cs::Geometry full{{}, 1, 0, 1, 0, 0};
  check(full.extent(UINT64_MAX) == UINT64_MAX);
  auto overflow = full; overflow.tile_bytes = 2;
  check(!overflow.extent(UINT64_MAX));
}

void allocated_ownership(bool grouped, unsigned parts = 4) {
  Memory metadata;
  auto description = series_description(3);
  orbital::worker::Lane lane;
  check(orbital::worker::adopt(lane));
  auto arena = take(orbital::allocation::open_arena(8 * 1024 * 1024));
  auto* pool = take(orbital::allocation::add_pool(*arena, lane));
  std::vector<orbital::allocation::Allocation> allocations;
  allocations.reserve(grouped ? 1 : parts);
  for (unsigned i = 0; i < (grouped ? 1 : parts); ++i)
    allocations.push_back(take(orbital::allocation::allocate(*pool,
        {.bytes = grouped ? parts * 8U : 8U, .alignment = 1, .initialization = orbital::allocation::Initialization::zero})));
  std::vector<cd::PartSource> source(parts);
  std::vector<cs::BodySource> bodies(parts);
  for (unsigned i = 0; i < parts; ++i) {
    auto& allocation = allocations[grouped ? 0 : i];
    auto body = orbital::Bytes{allocation.address + (grouped ? i * 8 : 0), 8};
    source[i] = {{93, i}, body, description, 0, 1, {}};
    bodies[i] = {source[i].id, body, {}, &allocation};
  }
  auto* shape = take(cs::prove_series(metadata, {8, 9}, source));
  metadata.limit = metadata.live;
  auto refused = cs::capture(metadata, {shape, 3,
      grouped ? cs::Owners::grouped_allocation : cs::Owners::independent_allocation}, bodies);
  check(!refused);
  for (const auto& allocation : allocations) check(allocation);
  metadata.limit = SIZE_MAX;
  auto* value = take(cs::capture(metadata, {shape, 3,
      grouped ? cs::Owners::grouped_allocation : cs::Owners::independent_allocation}, bodies));
  for (const auto& allocation : allocations) check(!allocation);
  check(cs::footprint(value).requested_bytes == 24 + parts * (grouped ? 8 : 16) + (grouped ? 8 : 0));
  cs::release(shape);
  auto* escaped = take(cs::detail::retain_part({value}, parts - 1, {{1, 7}, 1}, cs::Interpretation::selected_copy));
  cs::release(value);
  check(cs::view(escaped).address()[0] == std::byte{0});
  cs::release(escaped);
  check(!metadata.live);
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (orbital::allocation::close(*pool).kind() != orbital::Progress::Kind::done) {
    check(std::chrono::steady_clock::now() < end);
    orbital::worker::poll(lane, 1024); std::this_thread::yield();
  }
  check(arena->charged_bytes.load() == 0);
  arena = {};
  orbital::worker::relinquish(lane);
}

void allocated_late_refusal() {
  Memory metadata;
  orbital::worker::Lane local, other;
  check(orbital::worker::adopt(local));
  check(orbital::worker::adopt(other));
  auto arena = take(orbital::allocation::open_arena(8 * 1024 * 1024));
  auto* first_pool = take(orbital::allocation::add_pool(*arena, local));
  auto* second_pool = take(orbital::allocation::add_pool(*arena, other));
  auto first = take(orbital::allocation::allocate(*first_pool, {.bytes = 8, .alignment = 1}));
  auto second = take(orbital::allocation::allocate(*second_pool, {.bytes = 8, .alignment = 1}));
  orbital::worker::relinquish(other);
  auto description = series_description(3);
  const std::array<cd::PartSource, 2> source{{
      {{1, 1}, orbital::allocation::bytes(first), description, 0, 1, {}},
      {{1, 2}, orbital::allocation::bytes(second), description, 0, 1, {}}}};
  const std::array<cs::BodySource, 2> bodies{{
      {source[0].id, source[0].body, {}, &first},
      {source[1].id, source[1].body, {}, &second}}};
  auto* shape = take(cs::prove_series(metadata, {7, 9}, source));
  const auto before = metadata.live;
  auto refused = cs::capture(metadata, {shape, 3, cs::Owners::independent_allocation}, bodies);
  check(!refused && refused.error() == orbital::Error::conflict);
  check(first && second && metadata.live == before);
  check(orbital::worker::adopt(other));
  auto* accepted = take(cs::capture(metadata, {shape, 3, cs::Owners::independent_allocation}, bodies));
  check(!first && !second);
  cs::release(accepted); cs::release(shape);
  check(!metadata.live);
  for (const auto pair : {std::pair{first_pool, &local}, std::pair{second_pool, &other}}) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (orbital::allocation::close(*pair.first).kind() != orbital::Progress::Kind::done) {
      check(std::chrono::steady_clock::now() < end);
      orbital::worker::poll(*pair.second, 1024); std::this_thread::yield();
    }
  }
  check(!arena->charged_bytes.load());
  arena = {};
  orbital::worker::relinquish(local); orbital::worker::relinquish(other);
}

int main() {
  for (auto count : {0U, 1U, 4U, 17U}) {
    series_retention(count, cs::Interpretation::shared_shape);
    series_retention(count, cs::Interpretation::selected_copy);
  }
  for (auto format : {v::Format::rows, v::Format::columns})
    for (auto count : {0U, 3U, 9U}) fixture_proof(format, count);
  groups_and_refusals();
  allocated_ownership(false); allocated_ownership(true);
  allocated_ownership(false, 17); allocated_ownership(true, 17);
  allocated_late_refusal();
}
