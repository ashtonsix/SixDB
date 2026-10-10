#pragma once
#include "material.h"

namespace orbital_mvcc::composed { template<class Store> class Backend; }
namespace orbital::object_detail {
using namespace orbital_mvcc::composed;
// Disposable friend-style full-operation seam. Uses actual owner, enrolled
// Scope and opaque ObjectId. No deferred Body, per-Object cache or new lifetime.
template<> struct Access<compact_history::ShapePhysical> {
  using Owner = Backend<Catalog>;
  using Scope = Catalog::Scope;
private:
  using Core = Catalog::Core;
  static inline Result<void> consume_at(const Owner&, Core*, std::uint64_t, orbital::ObjectId,
      const orbital_bench::Operation&, orbital_bench::Outcome&);
public:
  static compact_history::CompactShapeStore16::Statistics statistics(const Catalog&, const compact_history::Memory&) noexcept;
  // Scope belongs to owner; missing Object/cross-owner refusal preserves output.
  // Existing invalid-operation exceptions and non-Tuple fallback are retained.
  static Result<void> consume(const Owner&, const Scope&, orbital::ObjectId,
      const orbital_bench::Operation&, orbital_bench::Outcome&);
  // Explicit current-Object entry: obtain existing Borrowed inside this call.
  // Fixed native reads cannot yield/reenter/publish; nothing borrowed escapes.
  // The Scope-based trial does not silently select this weaker lifetime mode.
  static Result<void> consume_current(const Owner&, orbital::ObjectId,
      const orbital_bench::Operation&, orbital_bench::Outcome&);
};
} // namespace orbital::object_detail
