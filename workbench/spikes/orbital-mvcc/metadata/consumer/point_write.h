#pragma once
#include "shape_write.h"

namespace orbital_mvcc::metadata_consumer::point_write {
namespace cs = compact_shape;
namespace v = orbital_bench::versioned;
namespace tp = ikea::tuplepack;
using orbital_bench::Key;
using write_batch::ProbeCosts;

// This driver's command changes value/tag; key and next remain in the clone.
// The generic full-record Update and shape_write control retain their semantics.
struct Command {
  Key key;
  std::uint32_t value;
  std::uint32_t tag;
  friend bool operator==(const Command&, const Command&) = default;
};

class TupleLaw;
// Borrowed actual Shape association and stationary owning law. Neither is
// retained by copying this packet; both must remain live and immutable.
class TupleProof {
  const TupleLaw* law_;
  explicit TupleProof(const TupleLaw* law) noexcept : law_(law) {}
  friend class TupleLaw;
public:
  const TupleLaw& law() const noexcept { return *law_; }
};

class TupleLaw {
  class Admission { Admission() = default; friend class TupleLaw; };
  const cs::Shape* const shape_;
  const tp::layout layout_;
  const std::array<tp::write_plan<8>, 2> writers_;
public:
  TupleLaw(Admission, const cs::Shape*, const tp::layout&,
      std::span<const tp::write_plan<8>, 2>) noexcept;
  TupleLaw(const TupleLaw&) = delete;
  TupleLaw(TupleLaw&&) = delete;
  TupleLaw& operator=(const TupleLaw&) = delete;
  TupleLaw& operator=(TupleLaw&&) = delete;
  [[nodiscard]] static orbital::Result<void> prepare(std::optional<TupleLaw>&,
      const cs::Shape*, const tp::layout&, std::span<const tp::write_plan<8>, 2>,
      orbital::ProgramId expected_program, orbital::PartId expected_part) noexcept;
  TupleProof proof() const noexcept { return TupleProof{this}; }
  const cs::Shape* shape() const noexcept { return shape_; }
  const tp::layout& layout() const noexcept { return layout_; }
  const auto& writers() const noexcept { return writers_; }
};

// Reuse the already admitted actual four-column Shape; no runtime Series plans.
using shape_write::SeriesProof;
using shape_write::admit_series_shape;

// Before any source memcpy/reservation, admit this transaction's mapped Tuple
// count/extent/null/address arithmetic. Encoding association and protection are
// caller facts. This does not search keys or grant private mutation authority.
[[nodiscard]] orbital::Result<void> admit_mapped(TupleProof, std::uint64_t count,
    orbital::Bytes) noexcept;

// Initialized exclusive exact-basis clone; owner supplies authority/lifetime.
// All Parts (including untouched key/next) and metadata aliases are admitted.
// Nonempty <=64 commands: one key preflight per command in offered value/key
// error order, then acceptance/effect capacity for the whole batch before stores.
// Duplicates are last offered wins. Refusal preserves bytes and both counters.
// Driver clone-first use intentionally allows capacity before absent and wasted
// copy work on refusal; publication/Outcomes remain outside this primitive.
[[nodiscard]] orbital::Result<void> apply(TupleProof, std::uint64_t count,
    std::span<const v::WritePart>, std::span<const Command>,
    v::KernelCosts&, ProbeCosts&) noexcept;
[[nodiscard]] orbital::Result<void> apply(SeriesProof, std::uint64_t count,
    std::span<const v::WritePart>, std::span<const Command>,
    v::KernelCosts&, ProbeCosts&) noexcept;
} // namespace orbital_mvcc::metadata_consumer::point_write
