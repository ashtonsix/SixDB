#include "shape_read.h"

namespace orbital_mvcc::metadata_consumer::shape_read {
orbital::Result<TupleProof> admit_tuple_shape(const cs::Shape* shape,
    orbital::ProgramId expected_program, orbital::PartId expected_part) noexcept {
  const auto invalid = [] { return std::unexpected(orbital::Error::invalid_argument); };
  if (!shape || cs::program(shape) != expected_program || cs::part_count(shape) != 1)
    return invalid();
  const auto interpretation = cs::tuple_interpretation(shape);
  // Empty interpretation distinguishes Series kind before tuple_law's assert.
  if (!compact_law::detail::exact_descriptor(interpretation)) return invalid();
  const auto& geometry = cs::geometry(shape, 0);
  if (geometry.id != expected_part || geometry.kind != 0 || geometry.tile_rows != 1 ||
      geometry.tile_bytes != compact_law::v::tuple_bytes || geometry.readable_padding ||
      geometry.alignment_log2 != 0) return invalid();
  const auto admitted = compact_law::qualify_cursor({cs::tuple_law(shape), 0}, {}, interpretation);
  if (!admitted) return std::unexpected(admitted.error());
  return TupleProof{shape};
}

orbital::Result<void> read_mapped_checked(TupleProof proof, std::uint64_t count,
    orbital::Bytes body, Key key, unsigned steps, Outcome& output) noexcept {
  if (count > SIZE_MAX / compact_law::v::tuple_bytes)
    return std::unexpected(orbital::Error::invalid_argument);
  const compact_law::Control control{cs::tuple_law(proof.shape()), static_cast<std::size_t>(count)};
  const auto cursor = compact_law::qualify_cursor(control, body, cs::tuple_interpretation(proof.shape()));
  if (!cursor) return std::unexpected(cursor.error());
  compact_law::read_chain8_into(control, *cursor, key, steps, output);
  return {};
}

orbital::Result<void> read_checked(TupleProof proof, cs::Selection selected,
    Key key, unsigned steps, Outcome& output) noexcept {
  if (!selected) return std::unexpected(orbital::Error::absent);
  const auto control = selected.control();
  if (control.shape != proof.shape()) return std::unexpected(orbital::Error::invalid_argument);
  const auto part = selected.part(std::size_t{0});
  if (!part) return std::unexpected(part.error());
  const auto extent = cs::geometry(control.shape, 0).extent(control.records);
  if (!extent || *extent > SIZE_MAX) return std::unexpected(orbital::Error::invalid_argument);
  return read_mapped_checked(proof, control.records,
      {part->address(), static_cast<std::size_t>(*extent)}, key, steps, output);
}
} // namespace orbital_mvcc::metadata_consumer::shape_read
