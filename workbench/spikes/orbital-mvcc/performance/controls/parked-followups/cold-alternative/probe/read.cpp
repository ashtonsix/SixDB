#include "object_material.h"
using object_test::Store;
using object_test::Selection;
static_assert(sizeof(Store) == 8 && sizeof(Store::Scope) == 32);
static_assert(sizeof(orbital::object_detail::History<object_test::Physical>::Entry) == 40);
extern "C" Selection scope_read(const Store::Scope& scope, orbital::ObjectId id) {
  return scope.bind(id);
}
extern "C" Selection borrowed_read(Store::Borrowed borrow, orbital::ObjectId id) {
  return borrow.bind(id);
}
