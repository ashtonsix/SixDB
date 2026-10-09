#pragma once
#include "data.h"
#include <orbital/object.h>

namespace orbital_bench::versioned {
namespace obj = orbital::object;
inline constexpr orbital::ProgramId data_program{0x4f4256455253494fULL, 1};
struct Costs {
  KernelCosts kernel;
  std::uint64_t requested_body_bytes{}, requested_parts{}, copied_bytes{}, copied_spans{};
};
struct Update { Key key{}; Record record; };
constexpr std::size_t write_batch_limit = 64;

// These native bindings borrow Schema and actual Orbital protection. Keep the
// selected/retained/context/Representation owner live through every body. No
// independent Hold, cache wrapper or lifetime substitute is introduced here.
orbital::Result<ReadData> bind(const Schema&, obj::Selection);
orbital::Result<ReadData> bind(const Schema&, const obj::Representation&);
orbital::Result<ReadData> bind(const Schema&, obj::View); // Complete Tuple Part only.
orbital::Result<ReadData> bind(const Schema&, obj::Representation&&) = delete;

// Homogeneous native bindings. The selected descriptors are fully qualified,
// including the count in the explicit shared control. Cursors borrow body
// bytes only; keep actual Orbital protection and Schema live, and use the same
// or equivalent immutable control at open(). A Tuple View discovers no siblings.
orbital::Result<TupleCursor> qualify_tuple(const TupleControl&, obj::Selection);
orbital::Result<TupleCursor> qualify_tuple(const TupleControl&, obj::View);
orbital::Result<ColumnCursors> qualify_columns(const Schema&, const ColumnControl&, obj::Selection);

// Complete calls include Shape preparation, real Builder allocation, acquiring
// exclusive WriteViews, codec work, closing all writers and immutable freeze.
// Failed/incomplete private construction returns no Representation. Draft
// insertion, prepare/install/retirement or physical adoption remain explicit
// actual Orbital calls at the enclosing request boundary.
orbital::Result<obj::Representation> construct(const Schema&, obj::Store&,
    orbital::allocation::Pool&, orbital::ObjectId, std::uint64_t version,
    std::span<const Row>, Format, Costs&, ConstructionOptions = {});
orbital::Result<obj::Representation> convert(const Schema&, obj::Store&,
    orbital::allocation::Pool&, obj::Selection, Format, Costs&, ConstructionOptions = {});
// Fixed membership: resolve all keys against the selected basis before private
// allocation, copy its initialized encoded Parts, then perform checked updates.
// The new version comes from the actual owning Draft. Empty/over64 batches,
// absent keys and over-width values reject before private writes.
orbital::Result<obj::Representation> update(const Schema&, obj::Store&,
    orbital::allocation::Pool&, obj::Selection, std::uint64_t new_version,
    std::span<const Update>, Costs&);
} // namespace orbital_bench::versioned
