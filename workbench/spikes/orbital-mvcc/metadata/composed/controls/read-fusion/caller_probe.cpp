#include "workbench/spikes/orbital-mvcc/metadata/composed/backend.h"
namespace c=orbital_mvcc::composed;
namespace p=orbital_versioned_performance;
extern "C" void baseline_scope_once(const c::Inline& owner, std::size_t object,
    const orbital_bench::Operation& op, orbital_bench::Outcome& output) {
  auto selected=owner.bodies<1>();
  output=p::read_operation(selected.body(object),op);
}
extern "C" void fused_scope_once(const c::Inline& owner, std::size_t object,
    const orbital_bench::Operation& op, orbital_bench::Outcome& output) {
  auto selected=owner.bodies<1>(); selected.consume_into(object,op,output);
}
extern "C" void fused_current_once(const c::Inline& owner, std::size_t object,
    const orbital_bench::Operation& op, orbital_bench::Outcome& output) {
  if(object>=owner.corpus().images.size())
    p::need(orbital::Result<void>{std::unexpected(orbital::Error::invalid_argument)});
  const auto status=c::InlineReadFusion::consume_current(owner,owner.corpus().images[object].address.object,op,output);
  if (!status) p::need(status);
}
