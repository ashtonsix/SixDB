// Authoritative private Shape declarations/helpers, owned by Orbital. Ordinary
// separate TU, no copied layout, assumed offset, or textual implementation seam.
#include "../orbital/shape/shape_private.h"
#include "shape_read_private.h"

namespace orbital_mvcc::metadata_consumer::shape_read {
orbital::Result<void> read_proven(TupleProof proof, cs::Selection selected,
    Key key, unsigned steps, Outcome& output) noexcept {
  if (!selected) return std::unexpected(orbital::Error::absent);
  auto* value = selected.realization;
  if (value->shape != proof.shape()) return std::unexpected(orbital::Error::invalid_argument);
  // Factory admitted this immutable one-Part Tuple Shape; capture admitted
  // count/placement. The enclosing owner protects both for this entire call.
  detail::consume(*cs::law(value->shape), *cs::address_slot(value, 0),
                  value->count, key, steps, output);
  return {};
}

orbital::Result<void> read_mapped_proven(TupleProof proof, std::uint64_t count,
    orbital::Bytes body, Key key, unsigned steps, Outcome& output) noexcept {
  // Mapping has no capture placement proof: keep its actual range admission.
  if (!detail::mapped_extent(count, body)) return std::unexpected(orbital::Error::invalid_argument);
  detail::consume(*cs::law(const_cast<cs::Shape*>(proof.shape())), body.data(),
                  count, key, steps, output);
  return {};
}
} // namespace orbital_mvcc::metadata_consumer::shape_read
