#pragma once

// Disposable fixed-law experiment against the frozen 7f73c81 consumer. This
// uses Ikea's private detail::word_transfer/gpr_read authoring seam, not a new
// production preparation or ownership contract. No payload load body is copied.
#if defined(CONSUMER_METADATA_FROZEN) && CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned/data.h"
#else
#include "consumer/data.h"
#endif
#include <ikea/tuplepack/author/execution.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace orbital_mvcc::metadata_consumer::shape_read::detail { struct CursorAccess; }

namespace orbital_mvcc::metadata_consumer::compact_law {
namespace v = orbital_bench::versioned;
namespace tp = ikea::tuplepack;
using orbital_bench::Row;

namespace detail {
inline constexpr std::array<unsigned, 4> offsets{0, 8, 16, 19};
inline constexpr std::array<unsigned, 4> widths{8, 8, 3, 4};
inline constexpr std::array<std::uint64_t, 4> masks{
    UINT64_MAX, UINT64_MAX, orbital_bench::value_mask, UINT32_MAX};

// The exact existing portable Tuple descriptor, including all codes. The
// canonical law is fixed to this description; arbitrary Tuple layouts reject.
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
  // constexpr source law; this bounded comparison retains no decoded layout
  // and no pointer to the supplied interpretation after qualification.
  constexpr auto expected = descriptor();
  for (unsigned i = 0; i < expected.size(); ++i)
    if (std::to_integer<unsigned>(bytes[i]) != expected[i]) return false;
  return true;
}
constexpr std::array<tp::detail::word_transfer, 4> transfers() {
  std::array<tp::detail::word_transfer, 4> result{};
  for (unsigned field = 0; field < result.size(); ++field)
    result[field] = {masks[field], tp::byte(offsets[field]), 0, tp::byte(widths[field])};
  return result;
}
inline bool exact_transfer(const tp::detail::word_transfer& transfer, unsigned field) {
  return transfer.mask == masks[field] && transfer.offset == offsets[field] &&
         transfer.shift == 0 && transfer.bytes == widths[field] &&
         unsigned(transfer.offset) + transfer.bytes <= v::tuple_bytes;
}
} // namespace detail

// Actual owned read-law storage. It contains four transfer controls and nothing
// else: no Schema pointer, constructors, writers, allocation, endpoint or box.
// Factory results own their controls; the preparation Schema can end at once.
class ReadLaw {
  std::array<tp::detail::word_transfer, 4> transfers_;
  explicit constexpr ReadLaw(std::array<tp::detail::word_transfer, 4> transfers)
      : transfers_(transfers) {}
public:
  // Compile-law alternative: derives the same private controls for this exact
  // canonical encoding. It performs no Schema/reader/writer preparation. The
  // check compares every derived control with preparation from existing Ikea.
  static constexpr ReadLaw canonical() { return ReadLaw{detail::transfers()}; }

  // Extraction alternative: a full Schema is transient preparation input. Its
  // 75-byte serialization, named empty view and four checked native endpoints
  // admit all unit/transfer facts before any controls are copied into ReadLaw.
  [[nodiscard]] static orbital::Result<ReadLaw> from_schema(const v::Schema& schema) {
    const auto refused = [] { return std::unexpected(orbital::Error::invalid_argument); };
    if (schema.tuple.bytes() != v::tuple_bytes || schema.tuple.encoded_size() != 75)
      return refused();
    std::array<tp::byte, 75> encoded{};
    const auto serialized = schema.tuple.encode(encoded);
    if (!serialized || *serialized != encoded.size() || encoded != detail::descriptor())
      return refused();
    const auto placed = tp::const_view::bind(schema.tuple, {}, 0, v::tuple_bytes);
    if (!placed) return refused();
    const auto admit = [&]<unsigned Bytes>(unsigned field) {
      const auto& reader = schema.readers[field];
      const auto& controls = reader.controls();
      if (reader.unit_bytes() != v::tuple_bytes || controls.slots != Bytes ||
          !detail::exact_transfer(controls.transfer, field)) return false;
      const auto operation = tp::bind_reader(reader, *placed);
      return operation && tp::native_reader(*operation).template admit_transfer<Bytes>().has_value();
    };
    if (!admit.template operator()<8>(0) || !admit.template operator()<8>(1) ||
        !admit.template operator()<3>(2) || !admit.template operator()<4>(3)) return refused();
    return ReadLaw{std::array{schema.readers[0].controls().transfer,
        schema.readers[1].controls().transfer, schema.readers[2].controls().transfer,
        schema.readers[3].controls().transfer}};
  }

  // Read-only inspection supports actual metadata and extraction checks. These
  // are private Ikea authoring controls, never serialized Part interpretation.
  const auto& transfers() const noexcept { return transfers_; }

  template<unsigned Field>
  // Trusted one complete row in the admitted fixed encoding; backing is live.
  [[gnu::always_inline]] std::uint64_t read_unchecked(const tp::byte* row) const {
    static_assert(Field < 4);
    constexpr unsigned bytes = detail::widths[Field];
    // Only slots and transfer participate in Rows=1's fixed transfer body. The
    // other gpr_read controls are transient zero state, not retained metadata.
    // A probe must distinguish sizeof(this local) from the optimized frame.
    tp::detail::gpr_read controls{};
    controls.slots = bytes;
    controls.transfer = transfers_[Field];
    return tp::detail::read_gpr_transfer<1, bytes>(controls,
        [&](unsigned) { return row; }, tp::detail::all_rows<1>, v::tuple_bytes);
  }
};

// The complete read-law cost is sizeof(ReadLaw), shared explicitly, plus this
// control. It is not hidden behind an allocated pointer facade. Backing remains
// caller protected. Control count must be the count admitted for the cursor.
struct Control { const ReadLaw& law; const std::size_t records; };
class Cursor;
class Part;
[[nodiscard]] orbital::Result<Part> qualify_part(const Control&, orbital::Bytes body,
                                                orbital::Bytes interpretation);
[[nodiscard]] orbital::Result<Cursor> qualify_cursor(const Control&, orbital::Bytes body,
                                                    orbital::Bytes interpretation);

// Pointer+count alternative. Copies/moves borrow only encoded backing. Reads
// receive a complete compact law as a call argument; no former control/Part
// address or preparation Schema is borrowed. Output spans must be disjoint
// from encoded bytes and live law metadata, as in the existing native body.
class Part {
  const tp::byte* data_;
  std::size_t records_;
  Part(const tp::byte* data, std::size_t records) : data_(data), records_(records) {}
  friend class Cursor;
  friend orbital::Result<Part> qualify_part(const Control&, orbital::Bytes, orbital::Bytes);
  friend orbital::Result<Cursor> qualify_cursor(const Control&, orbital::Bytes, orbital::Bytes);
  [[gnu::always_inline]] const tp::byte* address(std::size_t row) const {
    return data_ + row * v::tuple_bytes;
  }
public:
  std::size_t size() const noexcept { return records_; }
  // Trusted row<size(); law and caller-protected backing remain live.
  [[gnu::always_inline]] Row row_unchecked(const ReadLaw& law, std::size_t row) const {
    const auto* source = address(row);
    return {law.read_unchecked<0>(source), {law.read_unchecked<1>(source),
        std::uint32_t(law.read_unchecked<2>(source)), std::uint32_t(law.read_unchecked<3>(source))}};
  }
  [[nodiscard]] orbital::Result<Row> row(const ReadLaw& law, std::size_t row) const {
    if (row >= records_) return std::unexpected(orbital::Error::invalid_argument);
    return row_unchecked(law, row);
  }
  [[nodiscard]] orbital::Result<void> field(const ReadLaw& law, v::Field field, std::size_t first,
                                          std::span<std::uint64_t> output) const {
    if (static_cast<unsigned>(field) >= 4 || first > records_ || output.size() > records_ - first)
      return std::unexpected(orbital::Error::invalid_argument);
    const auto range = [&]<unsigned Field>() {
      for (std::size_t i = 0; i < output.size(); ++i)
        output[i] = law.read_unchecked<Field>(address(first + i));
    };
    switch (field) {
    case v::Field::key: range.template operator()<0>(); break;
    case v::Field::next: range.template operator()<1>(); break;
    case v::Field::value: range.template operator()<2>(); break;
    case v::Field::tag: range.template operator()<3>(); break;
    }
    return {};
  }
};

// Pointer-only alternative. with() copies count from the same/equivalent
// immutable control used in qualification; replacing count is trusted misuse.
// Returned Part retains no pointer into this cursor or the Control object.
class Cursor {
  const tp::byte* data_;
  explicit Cursor(const tp::byte* data) : data_(data) {}
  friend orbital::Result<Cursor> qualify_cursor(const Control&, orbital::Bytes, orbital::Bytes);
  friend struct orbital_mvcc::metadata_consumer::shape_read::detail::CursorAccess;
public:
  Part with(const Control& control) const { return {data_, control.records}; }
};

// Direct Part qualification proves exactly this fixed Tuple law and extent.
// It discovers no sibling, checks no Object/ProgramId/PartId association and
// grants no protection or authority; the actual selected adapter owns those.
// Count/extent and pointer arithmetic are admitted before any payload access.
inline orbital::Result<Part> qualify_part(const Control& control, orbital::Bytes body,
                                         orbital::Bytes interpretation) {
  const auto refused = [] { return std::unexpected(orbital::Error::invalid_argument); };
  if (control.records > SIZE_MAX / v::tuple_bytes ||
      body.size() != control.records * v::tuple_bytes || !detail::valid_span(body) ||
      !detail::exact_descriptor(interpretation)) return refused();
  // Both ReadLaw factories admit these facts independently of any placement.
  // The check keeps them explicit at this cold seam, including access bounds.
  for (unsigned field = 0; field < 4; ++field)
    if (!detail::exact_transfer(control.law.transfers()[field], field)) return refused();
  return Part{reinterpret_cast<const tp::byte*>(body.data()), control.records};
}
inline orbital::Result<Cursor> qualify_cursor(const Control& control, orbital::Bytes body,
                                             orbital::Bytes interpretation) {
  const auto part = qualify_part(control, body, interpretation);
  if (!part) return std::unexpected(part.error());
  return Cursor{part->data_};
}
} // namespace orbital_mvcc::metadata_consumer::compact_law
