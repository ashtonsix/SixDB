#pragma once
#include "candidates.h"
#include <concepts>

namespace orbital_mvcc::metadata_consumer {
// Only Part facts are admitted here. The enclosing immutable fixture descriptor
// or selected catalog entry must already establish Object/version, ProgramId,
// logical law/count and backing protection. Every access uses the requested
// explicit PartId; it does not navigate between Parts or infer membership.
// This works with the actual compact_descriptor View/Selection as well as the
// frozen baseline packets. No copied parent Description or owning wrapper is
// created to qualify native bytes.
template<class View>
concept PartView = requires(const View& view) {
  { bool(view) } -> std::same_as<bool>;
  { view.body() } -> std::same_as<orbital::Bytes>;
  { view.interpretation() } -> std::same_as<orbital::Bytes>;
  { view.id() } -> std::same_as<orbital::PartId>;
};

template<PartView View>
inline orbital::Result<QualifiedTuple8> qualify_borrowed_part8(
    const v::TupleControl& control, const View& view, orbital::PartId requested) {
  if (!view || view.id() != requested) return std::unexpected(orbital::Error::invalid_argument);
  return qualify_tuple_part8(control, view.body(), view.interpretation());
}
template<PartView View>
inline orbital::Result<ExplicitTuple16> qualify_borrowed_part16(
    const v::TupleControl& control, const View& view, orbital::PartId requested) {
  if (!view || view.id() != requested) return std::unexpected(orbital::Error::invalid_argument);
  return qualify_tuple_part16(control, view.body(), view.interpretation());
}
template<PartView View>
inline orbital::Result<void> qualify_borrowed_part16(
    const v::TupleControl& control, const View& view, orbital::PartId requested,
    ExplicitTuple16& output) {
  if (!view || view.id() != requested) return std::unexpected(orbital::Error::invalid_argument);
  return qualify_tuple_part16(control, view.body(), view.interpretation(), output);
}

template<class Selection>
requires requires(const Selection& selected, orbital::PartId id) { selected.part(id); }
inline orbital::Result<QualifiedTuple8> qualify_selected_part8(
    const v::TupleControl& control, const Selection& selected, orbital::PartId requested) {
  if (!selected) return std::unexpected(orbital::Error::invalid_argument);
  const auto part = selected.part(requested);
  if (!part) return std::unexpected(part.error());
  return qualify_borrowed_part8(control, *part, requested);
}
template<class Selection>
requires requires(const Selection& selected, orbital::PartId id) { selected.part(id); }
inline orbital::Result<ExplicitTuple16> qualify_selected_part16(
    const v::TupleControl& control, const Selection& selected, orbital::PartId requested) {
  if (!selected) return std::unexpected(orbital::Error::invalid_argument);
  const auto part = selected.part(requested);
  if (!part) return std::unexpected(part.error());
  return qualify_borrowed_part16(control, *part, requested);
}
} // namespace orbital_mvcc::metadata_consumer
