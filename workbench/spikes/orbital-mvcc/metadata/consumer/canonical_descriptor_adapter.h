#pragma once
#include "descriptor_adapter.h"
#include "canonical_law.h"

namespace orbital_mvcc::metadata_consumer::canonical_law {

// Full cold admission of actual immutable selected descriptions. Compile-time
// law does not replace Object/version, ProgramId, logical/parent count, actual
// Part directory, ID, descriptor or placement checks. No Schema is retained.
inline orbital::Result<Cursor> qualify_descriptor_cursor(const Control& control,
    compact_descriptor::Selection selected, orbital::ObjectId object,
    std::uint64_t version, orbital::PartId requested) {
  const auto parent = qualify_descriptor_tuple_parent(control.records, selected,
                                                       object, version, requested);
  if (!parent) return std::unexpected(parent.error());
  const auto part = selected.part(requested);
  if (!part) return std::unexpected(part.error());
  if (!*part || part->id() != requested)
    return std::unexpected(orbital::Error::invalid_argument);
  return qualify_cursor(control, part->body(), part->interpretation());
}
} // namespace orbital_mvcc::metadata_consumer::canonical_law
