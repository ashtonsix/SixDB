// Actual Shape export: a refused physical pin must unwind unexposed metadata.
#include "../../shape/shape.h"
#include "../../shape/native_fixture.h"
#include "internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory_resource>

namespace oa = orbital::allocation;
namespace cs = compact_shape;
namespace cd = compact_descriptor;
namespace sp = orbital_bench::versioned::sp;
template<class T> T take(orbital::Result<T> result) {
  if (!result) std::abort(); return std::move(*result);
}
void check(bool condition) { if (!condition) std::abort(); }

struct Memory : std::pmr::memory_resource {
  std::size_t live{};
  void* do_allocate(std::size_t size, std::size_t alignment) override {
    auto* value = std::pmr::new_delete_resource()->allocate(size, alignment);
    live += size; return value;
  }
  void do_deallocate(void* value, std::size_t size, std::size_t alignment) override {
    check(live >= size); live -= size;
    std::pmr::new_delete_resource()->deallocate(value, size, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

int main() {
  compact_shape::probe::NativeFixture fixture;
  check(fixture.open().has_value());
  Memory memory;
  auto allocation = take(oa::allocate(*fixture.pool,
      {.bytes = 8, .alignment = 1, .initialization = oa::Initialization::zero}));
  auto* physical = allocation.storage;
  auto encoded = sp::encode_descriptor({8, 0, sp::geometry::local, 3, {8, 0, 0}});
  check(encoded.has_value());
  const auto description = orbital::Bytes{
      reinterpret_cast<const std::byte*>(encoded->data()), encoded->size()};
  const cd::PartSource source{{1, 7}, oa::bytes(allocation), description, 0, 1, {}};
  const cs::BodySource body{source.id, oa::bytes(allocation), {}, &allocation};
  auto* shape = take(cs::prove_series(memory, {3, 4}, {&source, 1}));
  auto* value = take(cs::capture(memory, {shape, 3, cs::Owners::independent_allocation}, {&body, 1}));
  cs::release(shape);
  const auto normal = physical->users.load();
  const auto before = memory.live;
  physical->users.store(oa::detail::returning - 1);
  for (auto policy : {cs::Interpretation::shared_shape, cs::Interpretation::selected_copy}) {
    auto refused = cs::detail::retain_part({value}, 0, {{1, 9}, 2}, policy);
    check(!refused && refused.error() == orbital::Error::capacity);
    check(memory.live == before && physical->users.load() == oa::detail::returning - 1);
  }
  physical->users.store(normal);
  auto* escaped = take(cs::detail::retain_part({value}, 0, {{1, 9}, 2}, cs::Interpretation::selected_copy));
  cs::release(value);
  check(cs::view(escaped).address()[0] == std::byte{0});
  cs::release(escaped); check(memory.live == 0);
  check(fixture.close().has_value());
  std::printf("{\"storage_bytes\":%zu,\"saturated_export_refusal_cleanup\":true}\n", sizeof(oa::Storage));
}
