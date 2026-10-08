#include "packed-page.h"
#include <expected>
#include <stdexcept>
#include <utility>

namespace orbital_bench::packed {
namespace {
template<class T, class E> T checked(std::expected<T, E> result) {
  if (!result) throw std::runtime_error("packed page format admission failure");
  return std::move(*result);
}
template<class E> void checked(std::expected<void, E> result) {
  if (!result) throw std::runtime_error("packed page operation admission failure");
}
} // namespace

tp::layout Schema::make_layout() {
  std::array<tp::code, tuple_stride> codes;
  for (std::size_t i = 0; i < codes.size(); ++i) codes[i] = {tp::byte(i), 0, 8};
  return checked(tp::layout::make(tuple_stride, codes));
}
std::array<tp::byte, 8> Schema::map(unsigned first) {
  std::array<tp::byte, 8> result;
  for (unsigned i = 0; i < 8; ++i) result[i] = first + i;
  return result;
}
Schema::Schema() : layout(make_layout()), constructor(checked(tp::constructor::make(layout))),
    key_reader(checked(tp::reader<8>::make(layout, map(0)))),
    next_reader(checked(tp::reader<8>::make(layout, map(8)))),
    tag_reader(checked(tp::reader<8>::make(layout, std::array<tp::byte, 4>{16,17,18,19}))),
    key_writer(checked(tp::writer<8>::make(layout, map(0)))),
    next_writer(checked(tp::writer<8>::make(layout, map(8)))),
    tag_writer(checked(tp::writer<8>::make(layout, std::array<tp::byte, 4>{16,17,18,19}))) {}

Bindings::Bindings(const Schema& schema, std::span<std::byte> storage)
    : series(checked(SeriesView::attach(capacity, {{{
                  {reinterpret_cast<std::uint8_t*>(storage.data()), series_bytes}, Format::tile_bytes}, {}, {}}}))),
      prepared(checked(sp::bind_mutation(series))), values(sp::bind_decoder<std::uint32_t>(series)),
      write_value(prepared.erase<std::uint32_t, sp::sum_change>()),
      tuples(checked(tp::view::bind(schema.layout,
             {reinterpret_cast<tp::byte*>(storage.data() + tuple_offset), capacity * tuple_stride},
             capacity, tuple_stride))),
      keys(checked(tp::bind_reader(schema.key_reader, tuples))),
      next(checked(tp::bind_reader(schema.next_reader, tuples))),
      tags(checked(tp::bind_reader(schema.tag_reader, tuples))),
      write_key(checked(tp::bind_writer(schema.key_writer, tuples))),
      write_next(checked(tp::bind_writer(schema.next_writer, tuples))),
      write_tag(checked(tp::bind_writer(schema.tag_writer, tuples))) {
  // Ikea owns transfer-shape qualification; logical ranks and owner lifetime
  // remain with this consumer. Fields bind the same capacity; construction
  // initializes that extent before any logical row is published.
  checked(tp::native_reader(keys).admit_transfer<8>());
  checked(tp::native_reader(next).admit_transfer<8>());
  checked(tp::native_reader(tags).admit_transfer<4>());
}
} // namespace orbital_bench::packed
