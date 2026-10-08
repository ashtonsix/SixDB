#include "packed-construction.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace orbital_bench::packed {
namespace {
template<class T,class E> T checked(std::expected<T,E> result) {
  if (!result) throw std::runtime_error("packed page construction admission failed");
  return std::move(*result);
}
template<class E> void checked(std::expected<void,E> result) {
  if (!result) throw std::runtime_error("packed page construction failed");
}
}
ConstructionDelta construct_blank_page(Page& page, const Schema& schema,
    std::span<std::byte> body, std::size_t readable_padding, bool check_effects) {
  if (body.size() != page_bytes || readable_padding < 64 ||
      reinterpret_cast<std::uintptr_t>(body.data()) % 64 || page.bindings)
    throw std::runtime_error("blank construction requires fresh qualified backing");
  // Bind/admit before any destination write. The named view objects remain in
  // this stable allocation even when the owning unique_ptr<Page> is handed off.
  auto& b = page.bindings.emplace(schema, body);
  struct ResetOnFailure {
    Page& page;
    bool completed{};
    ~ResetOnFailure() { if (!completed) page.bindings.reset(); }
  } cleanup{page};
  auto constructor = checked(tp::bind_constructor(schema.constructor, b.tuples));
  const tp::construction_input zero_tuple{};
  std::array<std::uint32_t, capacity> zero_values{};
  std::array<ikea::owner_write, capacity + 64> records;
  ikea::source_write_journal effects{records};
  const auto needed = checked(b.prepared.construction_effect_capacity()) +
      capacity * constructor.effect_capacity();
  if (constructor.size() != capacity || !constructor.accepts(zero_tuple) || needed > records.size())
    throw std::runtime_error("blank construction domain/extent/effect admission failed");
  auto* order = reinterpret_cast<std::uint16_t*>(body.data() + order_offset);
  auto* occupied = reinterpret_cast<std::uint64_t*>(body.data() + bitmap_offset);
  for (std::size_t i = 0; i < capacity; ++i) std::construct_at(order + i, 0);
  for (std::size_t i = 0; i < 4; ++i) std::construct_at(occupied + i, 0);
  checked(b.write_value.initialize(zero_values, effects));
  // One qualified input reused across the initialized extent. The actual
  // constructor owns unit zeroing, code stores, spare bits and issued effects.
  for (std::size_t slot = 0; slot < capacity; ++slot)
    constructor.set_unchecked(slot, zero_tuple, effects);
  auto delta = pack_writes(b, effects);
  delta.index_write_bytes = page_bytes - order_offset;
  delta.index_write_spans = 1;
  if (check_effects) {
    std::array<bool, page_bytes> covered{};
    std::fill(covered.begin() + order_offset, covered.end(), true);
    for (const auto& effect : effects.entries()) {
      const auto base = effect.source == &b.series ? 0 : tuple_offset;
      std::fill_n(covered.begin() + base + effect.bytes.offset, effect.bytes.size, true);
    }
    // This fixed page format constructs the whole body. Requiring its actual
    // issued coverage to span that body is stronger than a byte-difference
    // check and never reads uninitialized old storage.
    if (!std::all_of(covered.begin(), covered.end(), [](bool byte) { return byte; }))
      throw std::runtime_error("page construction does not cover the whole body");
  }
  page.order = order; page.occupied = occupied;
  page.live = 0; page.sum = 0; cleanup.completed = true;
  return delta;
}
} // namespace orbital_bench::packed
