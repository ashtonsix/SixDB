#include "shape_private.h"
#include "native_fixture.h"
#include "internal.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <thread>
#include <vector>

namespace cs = compact_shape;
namespace oa = orbital::allocation;
namespace cd = compact_descriptor;
namespace sp = orbital_bench::versioned::sp;
template<class T> void check(const T& value, std::source_location loc = std::source_location::current()) {
  if (value) return;
  std::fprintf(stderr, "origin-owner check failed: %s:%u\n", loc.file_name(), loc.line()); std::abort();
}
template<class T> T take(orbital::Result<T> result) { check(result); return std::move(*result); }
struct Memory : std::pmr::memory_resource {
  std::size_t live{}, limit = SIZE_MAX;
  void* do_allocate(std::size_t n, std::size_t alignment) override {
    if (n > limit - live) throw std::bad_alloc();
    auto* p = std::pmr::new_delete_resource()->allocate(n, alignment); live += n; return p;
  }
  void do_deallocate(void* p, std::size_t n, std::size_t alignment) override {
    check(n <= live); live -= n; std::pmr::new_delete_resource()->deallocate(p, n, alignment);
  }
  bool do_is_equal(const memory_resource& r) const noexcept override { return this == &r; }
};

auto descriptor(std::size_t count = 3) {
  auto result = sp::encode_descriptor({8, 0, sp::geometry::local, count, {8, 0, 0}});
  check(result); return *result;
}

void selected_origins(cs::probe::NativeFixture& fixture) {
  Memory m;
  auto wire = descriptor();
  orbital::Bytes interpretation{reinterpret_cast<const std::byte*>(wire.data()), wire.size()};
  std::array<oa::Allocation, 256> allocations;
  std::array<oa::Storage*, 256> identities;
  std::array<const std::byte*, 256> expected;
  std::array<cd::PartSource, 256> sources;
  std::array<cs::BodySource, 256> bodies;
  for (std::size_t i = 0; i < allocations.size(); ++i) {
    allocations[i] = take(oa::allocate(*fixture.pool, {.bytes = 8, .alignment = 1}));
    identities[i] = allocations[i].storage; expected[i] = allocations[i].address;
    allocations[i].address[0] = std::byte(i);
    sources[i] = {{1,i}, {allocations[i].address, 8}, interpretation, 0, 1, {}};
    bodies[i] = {sources[i].id, sources[i].body, {}, &allocations[i]};
  }
  check(identities.back()->identity == 255);
  auto* shape = take(cs::prove_series(m, {3,4}, sources));
  auto* value = take(cs::capture(m, {shape, 3, cs::Owners::independent_allocation}, bodies));
  check(value->owners == cs::Owners::independent_origin);
  check(cs::footprint(value).requested_bytes == 24 + 8 * 256);
  cs::release(shape);
  for (std::size_t i = 0; i < allocations.size(); ++i) {
    check(!allocations[i]);
    auto view = take(cs::Selection{value}.part(i));
    check(view && view.address() == expected[i] && view.address()[0] == std::byte(i));
    check(cs::part_address(value, i) == expected[i]);
  }
  std::array<cs::Export*, 2> escaped;
  for (std::size_t i = 0; i < 2; ++i) escaped[i] = take(cs::detail::retain_part({value}, 255,
      {{9,8}, 7}, i ? cs::Interpretation::selected_copy : cs::Interpretation::shared_shape));
  cs::release(value);
  // Force ownership-vector growth and same-class allocation churn while exports retain identity 255.
  std::vector<oa::Allocation> churn;
  for (unsigned i = 0; i < 768; ++i) churn.push_back(take(oa::allocate(*fixture.pool, {.bytes = 8, .alignment = 1})));
  for (auto* exported : escaped) {
    check(cs::view(exported).address() == expected.back());
    check(cs::view(exported).address()[0] == std::byte{255});
    check(cs::binding(exported).version == 7);
    cs::release(exported);
  }
  churn.clear(); check(m.live == 0);
}

void placement_and_failure(cs::probe::NativeFixture& fixture, std::size_t allocation_bytes,
    std::size_t offset, cs::Owners owners) {
  Memory m; auto wire = descriptor();
  orbital::Bytes interpretation{reinterpret_cast<const std::byte*>(wire.data()), wire.size()};
  auto allocation = take(oa::allocate(*fixture.pool, {.bytes = allocation_bytes, .alignment = 1}));
  auto* expected = allocation.address + offset; expected[0] = std::byte{0x6b};
  const cd::PartSource source{{1,2}, {expected,8}, interpretation,0,1,{}};
  const cs::BodySource body{source.id,source.body,{},&allocation};
  auto* shape = take(cs::prove_series(m,{3,4},{&source,1}));
  const auto before = m.live;
  m.limit = before;
  check(!cs::capture(m,{shape,3,owners},{&body,1}));
  check(allocation && m.live == before && allocation.address[offset] == std::byte{0x6b});
  m.limit = SIZE_MAX;
  auto* value = take(cs::capture(m,{shape,3,owners},{&body,1}));
  cs::release(shape);
  const bool compressed = owners == cs::Owners::independent_allocation && !offset;
  check(cs::origin(value->owners) == compressed);
  check(cs::footprint(value).requested_bytes == (compressed ? 32 : 40));
  check(take(cs::Selection{value}.part(0)).address() == expected);
  auto* exported = take(cs::detail::retain_part({value},0,{{9,8},2},cs::Interpretation::selected_copy));
  cs::release(value);
  check(cs::view(exported).address() == expected && expected[0] == std::byte{0x6b});
  cs::release(exported); check(m.live == 0);
}

void growth(cs::probe::NativeFixture& fixture, bool window) {
  auto allocation = take(oa::allocate(*fixture.pool,
      {.bytes = window ? 8192u : 8u, .reserved_bytes = window ? 32768u : 64u, .alignment = 1}));
  auto retained = take(oa::hold(allocation,{0,allocation.size}));
  auto* before = allocation.address;
  check(cs::detail::allocation_origin(retained.storage) == before);
  check(oa::grow(allocation,{.bytes = window ? 16384u : 32u,.alignment = 1}));
  check(allocation.address == before && cs::detail::allocation_origin(retained.storage) == before);
  auto refused = oa::grow(allocation,{.bytes = window ? 65536u : 1536u,.alignment = 1});
  check(!refused && refused.error() == orbital::Error::conflict);
  check(cs::detail::allocation_origin(retained.storage) == before);
  oa::release(allocation);
  check(cs::detail::allocation_origin(retained.storage) == before);
  oa::release(retained);
}

void empty_and_shared() {
  Memory m; auto wire = descriptor(0);
  orbital::Bytes interpretation{reinterpret_cast<const std::byte*>(wire.data()),wire.size()};
  const cd::PartSource source{{1,2},{},interpretation,0,1,{}};
  auto* shape = take(cs::prove_series(m,{3,4},{&source,1}));
  const cs::BodySource body{source.id,{}, {},nullptr};
  for (auto owners : {cs::Owners::independent_allocation, cs::Owners::independent_shared, cs::Owners::grouped_shared}) {
    auto* value = take(cs::capture(m,{shape,0,owners},{&body,1}));
    const auto view = take(cs::Selection{value}.part(0));
    check(view && !view.address());
    cs::release(value);
  }
  cs::release(shape); check(m.live == 0);
}

int main() {
  cs::probe::NativeFixture fixture; check(fixture.open());
  selected_origins(fixture);
  for (auto bytes : {64u,8192u}) for (auto offset : {0u,8u})
    placement_and_failure(fixture,bytes,offset,cs::Owners::independent_allocation);
  placement_and_failure(fixture,64,8,cs::Owners::grouped_allocation);
  growth(fixture,false); growth(fixture,true); empty_and_shared();
  check(fixture.close());
  std::printf("{\"exact_origin_part_bytes\":8,\"explicit_part_bytes\":16,\"selection_bytes\":%zu,\"view_bytes\":%zu,\"result_view_bytes\":%zu,\"focused_histories\":true}\n",sizeof(cs::Selection),sizeof(cs::View),sizeof(orbital::Result<cs::View>));
}
