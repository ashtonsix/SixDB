#pragma once
#if defined(CONSUMER_METADATA_FROZEN) && CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned/consumer.h"
#else
#include "consumer/consumer.h"
#endif

namespace orbital_mvcc::metadata_consumer::write_batch {
namespace v = orbital_bench::versioned;

// Logical instrumentation, not hardware work or frame measurements. Costs
// commit only after successful completion; rejected batches issue no stores.
struct ProbeCosts {
  std::uint64_t private_admissions{}, key_reads{}, full_rows_read{}, applied_updates{};
};

// Initialized, private, exclusive clone; exact supplied Shape/Part descriptions
// and backing are qualified at this complete call boundary. Schema, Shape and
// input remain live/immutable; this function grants no write or publication
// authority. The enclosing owner closes writers only after return. Input,
// metadata and accounting outputs must be disjoint from targets and each other.
// Sorted fixed keys, <=64 nonempty updates; duplicates apply in offered order.
// Range/value/absent/description/placement failures leave bytes and costs intact.
// Same-record updates issue preserving stores like the generic control.
[[nodiscard]] orbital::Result<void> apply(const v::Schema&, const v::Shape&,
    std::span<const v::WritePart>, std::span<const v::Update>,
    v::KernelCosts&, ProbeCosts&);

// Same private-clone admission and batch refusal boundary, then unchanged
// update_into per row. Final existence check materializes Row24 as in the old
// consumer; apply checks only the encoded key. No publication is performed.
[[nodiscard]] orbital::Result<void> generic_control(const v::Schema&, const v::Shape&,
    std::span<const v::WritePart>, std::span<const v::Update>,
    v::KernelCosts&, ProbeCosts&);
} // namespace orbital_mvcc::metadata_consumer::write_batch
