#pragma once
#include "adapters.h"
#include "../orbital/descriptor/descriptor.h"

namespace orbital_mvcc::metadata_consumer {
namespace cd = compact_descriptor;

// Parent admission for the actual immutable frozen one-Part fixture. Keeping
// count separate permits compact read laws without retaining a Schema. The
// Caller's protection keeps the selected descriptor, parent slab and directory
// live during this call; Selection is a borrow and acquires no owner.
inline orbital::Result<void> qualify_descriptor_tuple_parent(std::size_t records,
    cd::Selection selected, orbital::ObjectId object, std::uint64_t version,
    orbital::PartId requested) {
  if (!selected) return std::unexpected(orbital::Error::invalid_argument);
  const auto& description = cd::description(selected);
  const auto logical = description.logical_metadata();
  if (description.object != object || description.version != version ||
      description.format != v::data_program || logical.size() != 8 ||
      !detail::valid_span(logical))
    return std::unexpected(orbital::Error::invalid_argument);
  const auto count = detail::count_bytes(records);
  if (!std::equal(count.begin(), count.end(), logical.begin()))
    return std::unexpected(orbital::Error::invalid_argument);
  return qualify_fixed_tuple_parent(records, description.interpretation(),
                                     requested, selected.part_count());
}

// Full comparable admission for the frozen one-Part fixture. describe() returns
// the actual 88B parent value by ordinary indirect ABI; its cost is deliberately
// present here. The generic Part-only adapter above is a separate mechanism
// probe, not a substitute for this parent's verification. Multi-Part fixtures
// need their own concrete immutable parent law admitted at construction/capture.
inline orbital::Result<QualifiedTuple8> qualify_descriptor_tuple8(
    const v::TupleControl& control, cd::Selection selected,
    orbital::ObjectId object, std::uint64_t version, orbital::PartId requested) {
  if (!selected) return std::unexpected(orbital::Error::invalid_argument);
  const auto description = cd::describe(selected.descriptor);
  if (description.object != object || description.version != version ||
      description.format != v::data_program || description.logical_metadata.size() != 8 ||
      !detail::valid_span(description.logical_metadata))
    return std::unexpected(orbital::Error::invalid_argument);
  const auto count = detail::count_bytes(control.records);
  if (!std::equal(count.begin(), count.end(), description.logical_metadata.begin()))
    return std::unexpected(orbital::Error::invalid_argument);
  const auto parent = qualify_tuple_parent(control, description.interpretation,
                                           requested, selected.part_count());
  if (!parent) return std::unexpected(parent.error());
  return qualify_selected_part8(control, selected, requested);
}

// Same full frozen one-Part admission through a protected immutable parent
// borrow. No rich Description value crosses the boundary. The parent still
// resides in Orbital's charged metadata slab; its dependent loads remain.
inline orbital::Result<QualifiedTuple8> qualify_descriptor_borrowed_tuple8(
    const v::TupleControl& control, cd::Selection selected,
    orbital::ObjectId object, std::uint64_t version, orbital::PartId requested) {
  const auto parent = qualify_descriptor_tuple_parent(control.records, selected,
                                                       object, version, requested);
  if (!parent) return std::unexpected(parent.error());
  return qualify_selected_part8(control, selected, requested);
}
} // namespace orbital_mvcc::metadata_consumer
