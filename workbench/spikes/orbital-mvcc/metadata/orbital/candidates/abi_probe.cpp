#include "shape_policy.h"

using Store = compact_history::InlineShapeStore16;
using Selection = compact_shape::Selection;

static_assert(sizeof(Store::Scope) == 32);
static_assert(sizeof(Store::Borrowed) == 16);
static_assert(sizeof(Store::Owner) == 8);
static_assert(sizeof(Store::Change) == 24);
static_assert(sizeof(Store::BoundSelection) == 32);
static_assert(sizeof(Store::Prepared) == 48);
static_assert(sizeof(compact_history::Binding) == 24);
static_assert(sizeof(Store::ConversionTarget) == 40);

Selection inline_bind(const Store::Scope& scope, orbital::ObjectId id) {
  return scope.bind(id);
}

Store::Borrowed inline_borrow(const Store& store) { return store.borrow(); }

orbital::Result<compact_shape::View> inline_part(Selection selection, std::size_t index) {
  return selection.part(index);
}

compact_shape::Control inline_control(Selection selection) { return selection.control(); }

orbital::Result<void> inline_prepare(Store& store, const Store::Scope& basis,
    std::span<const Store::Change> changes, Store::Prepared& prepared) {
  return store.prepare(basis, changes, prepared);
}
