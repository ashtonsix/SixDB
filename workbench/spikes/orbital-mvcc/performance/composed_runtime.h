#pragma once
#include "bodies.h"
#include "../metadata/composed/backend.h"

namespace orbital_versioned_performance {
namespace composed = orbital_mvcc::composed;
namespace shape_read = orbital_mvcc::metadata_consumer::shape_read;
namespace write_batch = orbital_mvcc::metadata_consumer::write_batch;

// One admitted immutable law, independent of any mapped-value transaction.
// The same native read bridge used by composed Orbital also receives LMDB's
// protected bytes directly. Schema remains an explicitly resident write input.
class MappedShape {
  const Corpus& corpus_;
  compact_history::Memory memory_;
  compact_shape::Shape* shape_{};
  std::optional<shape_read::TupleProof> proof_;
public:
  explicit MappedShape(const Corpus& corpus);
  ~MappedShape();
  MappedShape(const MappedShape&) = delete;
  auto proof() const { return *proof_; }
  const Corpus& corpus() const { return corpus_; }
  std::size_t metadata_bytes() const { return memory_.live; }
};

Interval run_lmdb_shape_reads(Lmdb&, const Corpus&, const MappedShape&, bool latency,
    std::span<const orbital_bench::Operation>, std::span<const std::size_t>,
    std::span<orbital_bench::Outcome>, std::size_t batch, std::uint64_t& bindings);
void lmdb_composed_update(Lmdb&, const Corpus&, std::span<const orbital_bench::Operation>,
    std::span<const std::size_t>, std::span<orbital_bench::Outcome>, data::Costs&, write_batch::ProbeCosts&);

template<class Backend>
Interval composed_reads(Backend& owner, bool latency, std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects, std::span<orbital_bench::Outcome> output,
    std::size_t batch, std::uint64_t& bindings) {
  const auto run = [&]<std::size_t Capacity>() {
    const auto acquire = [&] { return owner.template bodies<Capacity>(); };
    return latency ? opaque_read_trial<true>(acquire, input, objects, output, batch, bindings) :
                     opaque_read_trial<false>(acquire, input, objects, output, batch, bindings);
  };
  return batch == 1 ? run.template operator()<1>() : run.template operator()<64>();
}
} // namespace orbital_versioned_performance
