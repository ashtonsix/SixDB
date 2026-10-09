#pragma once
#include "descriptor_adapter.h"
#include "compact_law.h"

namespace orbital_mvcc::metadata_consumer::compact_law {

// Full actual selected admission, preserving the frozen one-Part parent law.
// ReadLaw64 and count are explicit inputs, independently charged. No Schema,
// rich parent value, owner packet or serialized-description copy is retained.
// This remains cold qualification; a shared shape can move it to construction
// only if that shape is actually associated with the immutable realization.
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
} // namespace orbital_mvcc::metadata_consumer::compact_law
