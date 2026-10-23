#pragma once
#include "shape_read.h"
#include "write_batch.h"
#include <optional>

namespace orbital_mvcc::metadata_consumer::shape_write {
namespace cs = compact_shape;
namespace v = orbital_bench::versioned;
namespace tp = ikea::tuplepack;
using orbital_bench::Key;
using orbital_bench::Record;
using write_batch::ProbeCosts;

class TupleLaw;
// Borrowed, sealed packet. Neither the stationary law nor its actual Shape is
// retained by a copy. Keep both live, immutable and at their admitted addresses.
class TupleProof {
  const TupleLaw* law_;
  explicit TupleProof(const TupleLaw* law) noexcept : law_(law) {}
  friend class TupleLaw;
public:
  const TupleLaw& law() const noexcept { return *law_; }
};

// Owning existing public controls, prepared once directly in final storage.
// Association with Shape is borrowed; no Schema or decoded-map pointer remains.
// An engaged destination cannot be replaced while borrowed proofs may exist.
class TupleLaw {
  class Admission { Admission() = default; friend class TupleLaw; };
  const cs::Shape* const shape_;
  const tp::layout layout_;
  const std::array<tp::write_plan<8>, 3> writers_;
public:
  TupleLaw(Admission, const cs::Shape*, const tp::layout&,
      std::span<const tp::write_plan<8>, 3>) noexcept;
  TupleLaw(const TupleLaw&) = delete;
  TupleLaw(TupleLaw&&) = delete;
  TupleLaw& operator=(const TupleLaw&) = delete;
  TupleLaw& operator=(TupleLaw&&) = delete;
  [[nodiscard]] static orbital::Result<void> prepare(std::optional<TupleLaw>&,
      const cs::Shape*, const tp::layout&, std::span<const tp::write_plan<8>, 3>,
      orbital::ProgramId expected_program, orbital::PartId expected_part) noexcept;
  TupleProof proof() const noexcept { return TupleProof{this}; }
  const cs::Shape* shape() const noexcept { return shape_; }
  const tp::layout& layout() const noexcept { return layout_; }
  const auto& writers() const noexcept { return writers_; }
};

// Fixed four-column law needs no runtime plans; cold admission proves the
// semantic Program and ordered key/next/value/tag IDs and exact typed geometry.
class SeriesProof {
  const cs::Shape* shape_;
  explicit SeriesProof(const cs::Shape* shape) noexcept : shape_(shape) {}
  friend orbital::Result<SeriesProof> admit_series_shape(const cs::Shape*,
      orbital::ProgramId, std::span<const orbital::PartId, 4>) noexcept;
public:
  const cs::Shape* shape() const noexcept { return shape_; }
};
[[nodiscard]] orbital::Result<SeriesProof> admit_series_shape(const cs::Shape*,
    orbital::ProgramId, std::span<const orbital::PartId, 4>) noexcept;

// Protected actual Selection, exact admitted Shape identity, sorted fixed keys.
// Row and Record outputs are disjoint from backing, live metadata and each
// other. Both stay intact on refusal; positions confer no mutation authority.
[[nodiscard]] orbital::Result<void> lookup(TupleProof, cs::Selection, Key,
    std::size_t& row, Record&) noexcept;
[[nodiscard]] orbital::Result<void> lookup(SeriesProof, cs::Selection, Key,
    std::size_t& row, Record&) noexcept;
// Enclosing transaction protects mapping and associates encoding/count. This
// seam admits the new exact mapping extent/null/address arithmetic each time.
[[nodiscard]] orbital::Result<void> lookup_mapped(TupleProof, std::uint64_t count,
    orbital::Bytes, Key, std::size_t& row, Record&) noexcept;

// Initialized exclusive private clone; caller owns that authority and lifetime.
// Actual count, ordered IDs, fresh exact extents/alignment and all aliases are
// checked before lookup. <=64 nonempty commands, duplicates in offered order.
// Immutable law replaces descriptor/plan re-admission; whole-batch value/key/
// capacity admission precedes stores. Refusals leave bytes and counters intact.
// Effects resolve against named stationary views before this call closes them.
[[nodiscard]] orbital::Result<void> apply(TupleProof, std::uint64_t count,
    std::span<const v::WritePart>, std::span<const v::Update>,
    v::KernelCosts&, ProbeCosts&) noexcept;
[[nodiscard]] orbital::Result<void> apply(SeriesProof, std::uint64_t count,
    std::span<const v::WritePart>, std::span<const v::Update>,
    v::KernelCosts&, ProbeCosts&) noexcept;

static_assert(sizeof(TupleProof) == 8 && sizeof(SeriesProof) == 8);
static_assert(std::is_trivially_copyable_v<TupleProof> && std::is_trivially_copyable_v<SeriesProof>);
static_assert(sizeof(TupleLaw) == 1408);
} // namespace orbital_mvcc::metadata_consumer::shape_write
