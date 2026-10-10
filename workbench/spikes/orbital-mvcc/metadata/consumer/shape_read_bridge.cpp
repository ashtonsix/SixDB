// Actual caller Shape and Orbital physical layout. Ordinary separate TU,
// no copied layout, assumed offset, or textual implementation seam.
#include "../orbital/shape/shape_private.h"
#include "shape_read_private.h"

namespace orbital_mvcc::metadata_consumer::shape_read {
orbital::Result<void> read_proven(TupleProof proof, cs::Selection selected,
    Key key, unsigned steps, Outcome& output) noexcept {
  if (!selected) return std::unexpected(orbital::Error::absent);
  auto* value = selected.realization;
  if (value->control.shape != proof.shape()) return std::unexpected(orbital::Error::invalid_argument);
  // Factory admitted this immutable one-Part Tuple Shape; capture admitted
  // count/placement. The enclosing owner protects both for this entire call.
  // TupleProof also proves every stored transfer equals this canonical law.
  // Keep exact Shape identity and the actual selected count for old/current views.
  constexpr auto law = compact_law::ReadLaw::canonical();
  const compact_law::Control control{law, static_cast<std::size_t>(value->control.records)};
  const auto cursor = detail::CursorAccess::admitted(*cs::address_slot(value, 0));
  [[clang::always_inline]] compact_law::read_chain_into(law, cursor.with(control),
      key, steps, output);
  return {};
}

orbital::Result<void> read_mapped_proven(TupleProof proof, std::uint64_t count,
    orbital::Bytes body, Key key, unsigned steps, Outcome& output) noexcept {
  // Mapping has no capture placement proof: keep its actual range admission.
  if (!detail::mapped_extent(count, body)) return std::unexpected(orbital::Error::invalid_argument);
  // The same exact TupleProof used by the fused catalog caller admits every
  // canonical transfer. Retain fresh mapped range admission above and the
  // caller's protecting transaction; specialize the identical Ikea body.
  (void)proof;
  constexpr auto law = compact_law::ReadLaw::canonical();
  const compact_law::Control control{law, static_cast<std::size_t>(count)};
  const auto cursor = detail::CursorAccess::admitted(body.data());
  [[clang::always_inline]] compact_law::read_chain_into(law, cursor.with(control),
      key, steps, output);
  return {};
}
} // namespace orbital_mvcc::metadata_consumer::shape_read
