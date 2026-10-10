#pragma once
#include "material.h"

namespace orbital_mvcc::composed {
template<class Store> class Backend;
// Disposable friend-style full-operation seam. Uses actual owner, enrolled
// Scope and opaque ObjectId. No deferred Body, per-Object cache or new lifetime.
struct InlineReadFusion {
  using Owner = Backend<compact_history::InlineShapeStore16>;
  using Scope = compact_history::InlineShapeStore16::Scope;
private:
  using Core = compact_history::InlineShapeStore16::Core;
  static inline Result<void> consume_at(const Owner&, Core*, std::uint64_t, orbital::ObjectId,
      const orbital_bench::Operation&, orbital_bench::Outcome&);
public:
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
} // namespace orbital_mvcc::composed
