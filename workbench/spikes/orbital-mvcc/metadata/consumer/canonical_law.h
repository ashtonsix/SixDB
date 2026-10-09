#pragma once

// Fixed canonical encoding experiment, not a production preparation contract.
// Private Ikea controls are derived at the inline authoring seam; payload loads
// remain exactly tp::detail::read_gpr_transfer<1,Bytes>. There is no retained
// Schema, ReadLaw transfer array, law pointer or allocated law box.
#if defined(CONSUMER_METADATA_FROZEN) && CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned/data.h"
#else
#include "consumer/data.h"
#endif
#include <ikea/tuplepack/detail/gpr.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace orbital_mvcc::metadata_consumer::canonical_law {
namespace v = orbital_bench::versioned;
namespace tp = ikea::tuplepack;
using orbital_bench::Key;
using orbital_bench::Outcome;
using orbital_bench::Record;
using orbital_bench::Row;

namespace detail {
constexpr std::array<tp::byte, 75> descriptor() {
  std::array<tp::byte, 75> result{};
  result[0] = 'T'; result[1] = 'P'; result[2] = 1;
  result[3] = v::tuple_bytes; result[4] = v::tuple_bytes;
  for (unsigned code = 0; code < v::tuple_bytes; ++code) {
    result[6 + 3 * code] = tp::byte(code);
    result[8 + 3 * code] = tp::byte(code == 18 ? 4 : 8);
  }
  return result;
}
inline bool valid_span(orbital::Bytes bytes) {
  const auto address = reinterpret_cast<std::uintptr_t>(bytes.data());
  return (!bytes.size() || address) && bytes.size() <= UINTPTR_MAX - address;
}
inline bool exact_descriptor(orbital::Bytes bytes) {
  if (bytes.size() != 75 || !valid_span(bytes)) return false;
  constexpr auto expected = descriptor();
  for (unsigned i = 0; i < expected.size(); ++i)
    if (std::to_integer<unsigned>(bytes[i]) != expected[i]) return false;
  return true;
}
} // namespace detail

// This empty tag may be materialized as an object; sizeof(Law) is then charged,
// not described as zero. Operations below use its static functions and store no
// tag object. Fixed source operands, instantiated code, descriptor comparison
// constants and any emitted rodata remain real program costs. Source/type
// extents are not a measurement of optimized code/rodata or stack-frame sizes.
struct Law {
  template<unsigned Field> static constexpr unsigned bytes = Field == 2 ? 3 : Field == 3 ? 4 : 8;
  template<unsigned Field> static constexpr unsigned offset = Field == 0 ? 0 : Field == 1 ? 8 : Field == 2 ? 16 : 19;
  template<unsigned Field> static constexpr std::uint64_t mask = Field == 2 ? orbital_bench::value_mask :
      Field == 3 ? UINT32_MAX : UINT64_MAX;

  template<unsigned Field>
  static constexpr tp::detail::word_transfer transfer() {
    static_assert(Field < 4);
    static_assert(offset<Field> + bytes<Field> <= v::tuple_bytes);
    return {mask<Field>, tp::byte(offset<Field>), 0, tp::byte(bytes<Field>)};
  }
  // Trusted complete row in exactly the canonical descriptor admitted below.
  template<unsigned Field>
  [[gnu::always_inline]] static std::uint64_t read_unchecked(const tp::byte* row) {
    tp::detail::gpr_read controls{};
    controls.slots = bytes<Field>;
    controls.transfer = transfer<Field>();
    return tp::detail::read_gpr_transfer<1, bytes<Field>>(controls,
        [&](unsigned) { return row; }, tp::detail::all_rows<1>, v::tuple_bytes);
  }
};

// Explicit shared count is the entire caller control. The same admitted count
// and uninterrupted immutable backing protection are required during native
// cursor use; arbitrarily replacing count is not a checked operation.
struct Control { const std::size_t records; };
class Part;
class Cursor;
[[nodiscard]] orbital::Result<Part> qualify_part(const Control&, orbital::Bytes, orbital::Bytes);
[[nodiscard]] orbital::Result<Cursor> qualify_cursor(const Control&, orbital::Bytes, orbital::Bytes);
void read_chain_into(Part, Key, unsigned steps, Outcome&);

class Part {
  const tp::byte* data_;
  std::size_t records_;
  Part(const tp::byte* data, std::size_t records) : data_(data), records_(records) {}
  friend class Cursor;
  friend orbital::Result<Part> qualify_part(const Control&, orbital::Bytes, orbital::Bytes);
  friend orbital::Result<Cursor> qualify_cursor(const Control&, orbital::Bytes, orbital::Bytes);
  friend void read_chain_into(Part, Key, unsigned, Outcome&);
  [[gnu::always_inline]] const tp::byte* address(std::size_t row) const {
    return data_ + row * v::tuple_bytes;
  }
  [[gnu::always_inline]] Key key_unchecked(std::size_t row) const {
    return Law::read_unchecked<0>(address(row));
  }
  [[gnu::always_inline]] Record record_unchecked(std::size_t row) const {
    const auto* source = address(row);
    return {Law::read_unchecked<1>(source), std::uint32_t(Law::read_unchecked<2>(source)),
            std::uint32_t(Law::read_unchecked<3>(source))};
  }
public:
  std::size_t size() const noexcept { return records_; }
  // Trusted row<size(), caller protects encoded backing. No endpoint borrows
  // a former Part/control/view address, so these admitted values can move.
  [[gnu::always_inline]] Row row_unchecked(std::size_t row) const {
    return {key_unchecked(row), record_unchecked(row)};
  }
  [[nodiscard]] orbital::Result<Row> row(std::size_t row) const {
    if (row >= records_) return std::unexpected(orbital::Error::invalid_argument);
    return row_unchecked(row);
  }
};
class Cursor {
  const tp::byte* data_;
  explicit Cursor(const tp::byte* data) : data_(data) {}
  friend orbital::Result<Cursor> qualify_cursor(const Control&, orbital::Bytes, orbital::Bytes);
public:
  Part with(const Control& control) const { return {data_, control.records}; }
};

// Actual descriptor and extent admission; this checks no parent/Object/PartId
// association and creates no ownership or authority. Those remain the actual
// selected adapter's responsibility. Exact canonical descriptor proves every
// compile transfer fact, so no runtime transfer array or recheck belongs here.
inline orbital::Result<Part> qualify_part(const Control& control, orbital::Bytes body,
                                         orbital::Bytes interpretation) {
  if (control.records > SIZE_MAX / v::tuple_bytes ||
      body.size() != control.records * v::tuple_bytes || !detail::valid_span(body) ||
      !detail::exact_descriptor(interpretation))
    return std::unexpected(orbital::Error::invalid_argument);
  return Part{reinterpret_cast<const tp::byte*>(body.data()), control.records};
}
inline orbital::Result<Cursor> qualify_cursor(const Control& control, orbital::Bytes body,
                                             orbital::Bytes interpretation) {
  const auto part = qualify_part(control, body, interpretation);
  if (!part) return std::unexpected(part.error());
  return Cursor{part->data_};
}

// Complete point/chain call: actual encoded key search, immediate consumption,
// no Record array scratch and no retained law. Native keys are sorted; Outcome
// is disjoint from protected source bytes and live count/control metadata.
inline void read_chain_into(Part state, Key key, unsigned steps, Outcome& output) {
  output = {};
  for (unsigned step = 0; step < steps; ++step) {
    std::size_t first = 0, last = state.size();
    while (first < last) {
      const auto middle = first + (last - first) / 2;
      if (state.key_unchecked(middle) < key) first = middle + 1;
      else last = middle;
    }
    if (first == state.size() || state.key_unchecked(first) != key) {
      orbital_bench::consume_absent(output, key);
      break;
    }
    const auto record = state.record_unchecked(first);
    orbital_bench::consume_row(output, key, record);
    key = record.next;
  }
}
inline void read_chain8_into(const Control& control, Cursor state, Key key,
                             unsigned steps, Outcome& output) {
  read_chain_into(state.with(control), key, steps, output);
}
inline void read_point8_into(const Control& control, Cursor state, Key key, Outcome& output) {
  read_chain8_into(control, state, key, 1, output);
}
} // namespace orbital_mvcc::metadata_consumer::canonical_law
