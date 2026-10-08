#pragma once
#include "workload.h"
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <orbital/allocation.h>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>

// The concrete packed consumer's representation, not a module-level lookup API.
// An uninterrupted owner batch keeps the directory, pages, named views/plans,
// slot mapping and initialized contents stable until every borrower returns.
namespace orbital_bench::packed {
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
namespace alloc = orbital::allocation;
using Format = sp::preset_format<20, sp::preset::compact>;
using SeriesView = sp::view<Format, std::uint8_t>;
using TupleRead = tp::read_operation<8, tp::byte>;
using Prepared = std::remove_cvref_t<decltype(*sp::bind_mutation(std::declval<SeriesView&>()))>;
constexpr std::size_t capacity = 256;
constexpr std::size_t series_bytes = ((capacity + Format::tile_rows - 1) / Format::tile_rows) * Format::tile_bytes;
constexpr std::size_t tuple_offset = (series_bytes + 63) & ~std::size_t{63};
constexpr std::size_t tuple_stride = 20;
constexpr std::size_t order_offset = tuple_offset + capacity * tuple_stride;
constexpr std::size_t bitmap_offset = order_offset + capacity * sizeof(std::uint16_t);
constexpr std::size_t page_bytes = bitmap_offset + 4 * sizeof(std::uint64_t);

struct Schema {
  tp::layout layout;
  tp::constructor constructor;
  tp::reader<8> key_reader, next_reader, tag_reader;
  tp::writer<8> key_writer, next_writer, tag_writer;
  static tp::layout make_layout();
  static std::array<tp::byte, 8> map(unsigned first);
  Schema();
};

// Named views and operations stay at stable addresses. Merely forming a
// member's address does not load that member or qualify payload bytes.
struct Bindings {
  SeriesView series;
  Prepared prepared;
  sp::decoder<std::uint32_t> values;
  sp::composition::mutation_operation<std::uint32_t, sp::sum_change> write_value;
  tp::view tuples;
  TupleRead keys, next, tags;
  tp::mutation_operation<8> write_key, write_next, write_tag;
  Bindings(const Schema&, std::span<std::byte>);
  Bindings(const Bindings&) = delete;
};
struct Page {
  alloc::Allocation allocation;
  alloc::Hold retained;
  std::size_t live{};
  std::uint64_t sum{};
  std::uint16_t* order{};
  std::uint64_t* occupied{};
  // Construct only after backing qualifies; Page itself never moves.
  std::optional<Bindings> bindings;
};
// Packed owns each published Page exactly once through its private directory.
struct Entry { Key maximum; Page* page; };
static_assert(std::is_trivially_copyable_v<Entry>);
struct Source { std::span<const Entry> directory; };

// Stable Bindings admits this transfer shape through Ikea's author endpoint.
// Caller separately qualifies the original physical row and backing lifetime.
template<unsigned Bytes>
[[gnu::always_inline]] inline std::uint64_t read(const TupleRead& operation, std::size_t slot) {
  return tp::native_reader(operation).template get_transfer_unchecked<Bytes>(slot);
}
[[gnu::always_inline]] inline Key read_key(const Bindings& binding, std::size_t slot) {
  return read<8>(binding.keys, slot);
}
[[gnu::always_inline]] inline Record record(const Bindings& binding, std::size_t slot) {
  return {read<8>(binding.next, slot), std::uint32_t(sp::get_unchecked(binding.series, slot)),
          std::uint32_t(read<4>(binding.tags, slot))};
}
} // namespace orbital_bench::packed
