#pragma once
#include "../orbital/shape/shape.h"
#include "compact_operations.h"

namespace orbital_mvcc::metadata_consumer::shape_read {
namespace cs = compact_shape;
using orbital_bench::Key;
using orbital_bench::Outcome;

// Borrowed semantic proof, not an owner or selected Object/version label.
// Its actual immutable Shape must stay live through every use, independently
// of mapped-body protection. Factory checks all fixed Tuple format facts once.
class TupleProof {
  const cs::Shape* shape_;
  explicit TupleProof(const cs::Shape* shape) : shape_(shape) {}
  friend orbital::Result<TupleProof> admit_tuple_shape(const cs::Shape*,
      orbital::ProgramId, orbital::PartId) noexcept;
public:
  const cs::Shape* shape() const noexcept { return shape_; }
};
[[nodiscard]] orbital::Result<TupleProof> admit_tuple_shape(const cs::Shape*,
    orbital::ProgramId expected_program, orbital::PartId expected_part) noexcept;

// A live owner/catalog Scope protects Selection, its slot, Shape and backing.
// Exact Shape identity is required; selected count is the actual captured count.
// Sorted fixed keys; output is disjoint from encoded bytes and live metadata.
// Refusal leaves Outcome intact. Both paths borrow the same stored ReadLaw and
// run the same existing compact kernels; no Schema or payload scratch remains.
[[nodiscard]] orbital::Result<void> read_checked(TupleProof, cs::Selection,
    Key, unsigned steps, Outcome&) noexcept;
[[nodiscard]] orbital::Result<void> read_proven(TupleProof, cs::Selection,
    Key, unsigned steps, Outcome&) noexcept;

// Same consumer improvement for mapped LMDB values. The enclosing read
// transaction associates these bytes with the admitted format/count and protects
// them; neither path can infer encoding or Object/version identity from payload.
// Both admit the new mapping's exact extent/null/overflow before any output.
[[nodiscard]] orbital::Result<void> read_mapped_checked(TupleProof, std::uint64_t count,
    orbital::Bytes, Key, unsigned steps, Outcome&) noexcept;
[[nodiscard]] orbital::Result<void> read_mapped_proven(TupleProof, std::uint64_t count,
    orbital::Bytes, Key, unsigned steps, Outcome&) noexcept;

static_assert(sizeof(TupleProof) == 8 && std::is_trivially_copyable_v<TupleProof>);
} // namespace orbital_mvcc::metadata_consumer::shape_read
