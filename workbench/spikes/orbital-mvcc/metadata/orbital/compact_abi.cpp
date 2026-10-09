// Ordinary external boundaries for the actual candidate, not mirrored packets.
#include "candidates/candidate_store.h"

namespace history = compact_history;
namespace physical = compact_descriptor;

extern "C" physical::Selection compact_bind(
    const history::Store16::Scope& scope, orbital::ObjectId object) noexcept {
  return scope.bind(object);
}

extern "C" orbital::Result<physical::Selection> compact_alternative(
    const history::Store16::Scope& scope, orbital::ObjectId object) {
  return scope.bind_alternative(object);
}

extern "C" history::Store16::Borrowed compact_borrow(const history::Store16& store) noexcept {
  return store.borrow();
}

extern "C" orbital::Result<void> compact_install(
    history::Store16& store, history::Store16::Prepared& prepared) noexcept {
  return store.install(prepared);
}

extern "C" orbital::Result<history::Store16::Prepared> compact_prepare(
    history::Store16& store, const history::Store16::Scope& scope,
    std::span<const history::Change> changes) {
  return store.prepare(scope, changes);
}

extern "C" void compact_enroll(history::Store16::Scope* final_place,
    const history::Store16& store) noexcept {
  std::construct_at(final_place, store);
}

extern "C" void compact_close(history::Store16::Scope& scope) noexcept {
  std::destroy_at(&scope);
}
