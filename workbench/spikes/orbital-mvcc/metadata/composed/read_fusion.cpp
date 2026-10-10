#include "backend.h"
#include "../orbital/shape/shape_private.h"
#include "../consumer/shape_read_private.h"

namespace orbital::object_detail {
using namespace orbital_mvcc::composed;
[[gnu::always_inline]] inline Result<void> InlineReadFusion::consume_at(const Owner& owner, Core* core,
    std::uint64_t cut, orbital::ObjectId object, const orbital_bench::Operation& op,
    orbital_bench::Outcome& output) {

  // A lookup-only walk over the actual directory. Keep the found cell address
  // through version selection; find()->handle->get() needlessly reopens it.
  // Full opaque identity, tombstones and bounded probing remain mandatory.
  const auto& directory = core->entries.entries_;
  using Directory = orbital::object_detail::TipDirectory;
  const auto capacity = directory.buckets_.size();
  if (!capacity) return std::unexpected(Error::absent);
  auto index = static_cast<std::size_t>(Directory::hash(object)) & (capacity - 1);
  const orbital::object_detail::Entry* found{};
  for (std::size_t probes = 0; probes < capacity; ++probes) {
    const auto handle = directory.buckets_[index];
    if (handle == Directory::empty) break;
    if (handle != Directory::deleted) {
      const auto& entry = directory.cell(handle);
      if (entry.object == object) { found = &entry; break; }
    }
    index = (index + 1) & (capacity - 1);
  }
  if (!found) return std::unexpected(Error::absent);

  // The enrolled cut, not current catalog time, governs first discovery.
  // Primary-only selection matches body(..., alternative=false). Retained
  // ancestors and tombstones follow the same actual nodes/registry as resolve.
  cs::Realization* value{};
  if (found->cut && found->cut <= cut)
    value = static_cast<cs::Realization*>(found->primary);
  else {
    auto node = found->older;
    while (node && core->nodes.cut(node) > cut) node = core->nodes.get(node).older;
    if (node) value = static_cast<cs::Realization*>(core->directory.get(core->nodes.get(node).primary));
  }
  if (!value) return std::unexpected(Error::absent);

  const auto& material = owner.material_;
  using orbital_bench::Kind;
  if (material.tuple_read_ && value->control.shape == material.tuple_read_->shape() &&
      (op.kind == Kind::read || op.kind == Kind::chain)) {
    if (op.kind == Kind::chain && op.count > 16)
      throw std::invalid_argument("composed chain exceeds fixture bound");
    // TupleProof cold admission proves every transfer equals canonical().
    // Specialize the same Ikea transfer body; stored Shape/ReadLaw still exist
    // and remain charged. Capture established actual count/placement; Scope
    // or uninterrupted current borrow protects backing. No runtime Shape
    // count/transfer reload is needed.
    constexpr auto law = orbital_mvcc::metadata_consumer::compact_law::ReadLaw::canonical();
    const orbital_mvcc::metadata_consumer::compact_law::Control control{law, static_cast<std::size_t>(value->control.records)};
    const auto cursor = sr::detail::CursorAccess::admitted(*cs::address_slot(value, 0));
    [[clang::always_inline]] orbital_mvcc::metadata_consumer::compact_law::read_chain_into(law, cursor.with(control),
        op.key, op.kind == Kind::chain ? op.count : 1, output);
    return {};
  }
  // Real converted Series primary or scan: preserve the established behavior.
  // This slow path publishes output only after its complete operation succeeds.
  output = material.consume({value}, op);
  return {};
}
Result<void> InlineReadFusion::consume(const Owner& owner, const Scope& scope,
    orbital::ObjectId object, const orbital_bench::Operation& op, orbital_bench::Outcome& output) {
  auto* core = scope.core_;
  if (!core || core != owner.store_.core_) return std::unexpected(Error::invalid_argument);
  return consume_at(owner, core, scope.cut_, object, op, output);
}
Result<void> InlineReadFusion::consume_current(const Owner& owner, orbital::ObjectId object,
    const orbital_bench::Operation& op, orbital_bench::Outcome& output) {
  const auto borrowed = owner.store_.borrow();
  if (!borrowed.core || borrowed.cut != borrowed.core->cut)
    return std::unexpected(Error::invalid_argument);
  return consume_at(owner, borrowed.core, borrowed.cut, object, op, output);
}
} // namespace orbital::object_detail
