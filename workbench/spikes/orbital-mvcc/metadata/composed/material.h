#pragma once
#include "../../performance/runtime.h"
#include "../../performance/read.h"
#include "../consumer/shape_read.h"
#include "../consumer/write_batch.h"
#include "../consumer/shape_write.h"
#include "../consumer/point_write.h"
#include "../orbital/candidates/shape_policy.h"
#include <optional>

namespace orbital_mvcc::composed {
namespace p = orbital_versioned_performance;
namespace v = orbital_bench::versioned;
namespace cs = compact_shape;
namespace oa = orbital::allocation;
namespace sr = metadata_consumer::shape_read;
namespace wb = metadata_consumer::write_batch;
namespace sw = metadata_consumer::shape_write;
namespace pw = metadata_consumer::point_write;
using orbital::Result;
using orbital::Error;

// Private, initialized Parts only cross capture after the last native writer.
// No address points into this movable record; Allocation owns the actual bytes.
struct PrivateParts {
  const v::Shape* shape{};
  std::array<oa::Allocation, 4> allocations;
  std::array<v::WritePart, 4> writable;
  auto parts() const { return std::span(writable).first(shape->part_count); }
};

struct Accounting {
  std::size_t metadata_live{}, metadata_peak{}, metadata_calls{}, metadata_limit{};
  std::size_t shared_shape_bytes{}, write_shape_heap_bytes{}, schema_bytes{};
  std::size_t borrowed_write_shape_record_bytes{}, pool_setup_and_inventory_bytes{};
  // Inline slots are included in sizeof(Backend), separate from PMR metadata.
  // Live law bytes are a subset of these slots, not an additional allocation.
  std::size_t write_control_live_bytes{}, write_control_slots_bytes{};
  std::size_t active_allocations{}, live_capacity_bytes{};
  // Some Allocation treatments do not track this diagnostic. Null is not zero.
  std::optional<std::size_t> held_extent_bytes;
  std::uint64_t arena_charge{};
  oa::Statistics pool;
};

// The Corpus is the common external input/oracle fixture. Its Schema and one
// Tuple Shape are also explicit borrowed write controls, counted separately.
// Actual captured Shapes own their read law. No per-body shared_ptr is used.
class Material {
  const p::Corpus& corpus_;
  v::Shape columns_;
  struct ReleaseShape { void operator()(cs::Shape* value) const { cs::release(value); } };
  std::unique_ptr<cs::Shape, ReleaseShape> tuple_, columns_proof_;
  std::optional<sr::TupleProof> tuple_read_;
  std::optional<pw::TupleLaw> tuple_write_;
  std::optional<sw::SeriesProof> columns_write_;
  Result<PrivateParts> allocate(const v::Shape&, v::Costs&);
public:
  compact_history::Memory memory;
  p::Runtime runtime;
  Material(const p::Corpus&, std::size_t total_bytes, std::size_t metadata_bytes);
  ~Material();
  Material(const Material&) = delete;
  const p::Corpus& corpus() const { return corpus_; }
  const v::Shape& shape(v::Format format) const;
  const v::Shape& shape(cs::Selection) const;
  Result<PrivateParts> construct(std::span<const orbital_bench::Row>, v::Format, v::Costs&);
  Result<PrivateParts> clone(cs::Selection, v::Costs&);
  Result<PrivateParts> convert(cs::Selection, v::Format, v::Costs&, v::ConstructionOptions = {});
  Result<cs::Shape*> proof(const PrivateParts&);
  v::ReadData generic(cs::Selection) const;
  Result<void> apply(cs::Selection basis, const PrivateParts&, std::span<const pw::Command>,
      v::KernelCosts&, wb::ProbeCosts&) const;
  orbital_bench::Outcome consume(cs::Selection, const orbital_bench::Operation&) const;
  Accounting accounting() const;
};

// Borrowed selected body; its caller keeps both Material and the issuing Scope
// stationary/live. Native kernels are the released consumer, not a new codec.
struct Body {
  const Material* material;
  cs::Selection selected;
  auto consume(const orbital_bench::Operation& op) const { return material->consume(selected, op); }
  auto get(orbital_bench::Key key) const { return material->generic(selected).get(key); }
  auto scan(orbital_bench::Key key, std::span<orbital_bench::Row> output) const {
    return material->generic(selected).scan(key, output);
  }
  auto row_unchecked(std::size_t row) const { return material->generic(selected).row_unchecked(row); }
  std::size_t size() const { return selected.records(); }
  v::Format format() const { return material->shape(selected).format; }
};
static_assert(sizeof(Body) == 16);
} // namespace orbital_mvcc::composed
