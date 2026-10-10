#include "object_material.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <source_location>

namespace {
using namespace object_test;
using orbital::Error;
constexpr orbital::ObjectId a{3, 11}, b{5, 13};
void check(bool good, const char* expression,
    std::source_location where = std::source_location::current()) {
  if (good) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), expression);
  std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)
template<class T> T take(orbital::Result<T> result) { CHECK(result); return std::move(*result); }
template<class T> void refused(const orbital::Result<T>& result, Error error) {
  CHECK(!result && result.error() == error);
}
struct Memory final : std::pmr::memory_resource {
  std::size_t live{}, calls{};
  bool fail{};
  ~Memory() { CHECK(!live); }
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    ++calls;
    if (fail) throw std::bad_alloc{};
    auto* p = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes; return p;
  }
  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
    CHECK(live >= bytes); live -= bytes;
    std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
Store::Owner capture(Store& store, std::uint64_t number, bool complement = false) {
  return take(store.capture({number, complement}, {}));
}
void publish(Store& store, orbital::ObjectId id, Store::Owner owner) {
  const std::array<Store::Change, 1> changes{{{id, std::move(owner)}}};
  Store::Prepared packet(store);
  { Store::Scope basis(store); CHECK(store.prepare(basis, changes, packet)); }
  CHECK(store.install(packet));
}
void adopt(Store& store, orbital::ObjectId id, const Store::Owner& owner) {
  Store::Scope basis(store);
  auto target = take(store.conversion(basis, id)); CHECK(store.adopt(target, owner));
}
void expect(Selection selected, std::uint64_t number) {
  CHECK(selected && selected.number() == number);
}
void discard_live_selections() {
  Memory memory;
  Store store(memory);
  publish(store, a, capture(store, 41));
  auto alternative = capture(store, 41, true);
  adopt(store, a, alternative);
  auto* alternate = alternative.get(); alternative = {};
  std::optional<Export> escaped;
  {
    Store::Scope old(store);
    auto primary = take(store.bound(old, a));
    auto selected = take(store.bound_alternative(old, a));
    const auto cut = old.cut(), calls = memory.calls;
    CHECK(alternate->references == 2); // Catalog + captured Scope.
    memory.fail = true;
    CHECK(store.discard_alternative(a));
    memory.fail = false;
    CHECK(memory.calls == calls && alternate->references == 1);
    CHECK(store.borrow().cut == cut);
    expect(primary.selection(), 41); expect(selected.selection(), 41);
    escaped.emplace(take(selected.export_part(0)));
    CHECK(escaped->value == alternate && escaped->binding.version == cut);
    Store::Scope late(store); // First discovery at the same cut after removal.
    expect(late.bind(a), 41); refused(late.bind_alternative(a), Error::absent);
    refused(store.discard_alternative(a), Error::absent);
    refused(store.promote_alternative(a), Error::unavailable);
  }
  CHECK(escaped->value->references == 1 && escaped->number() == 41);
  escaped.reset();
}

void promotion_lifetimes_and_authority() {
  Memory memory;
  Store store(memory), foreign(memory);
  publish(store, a, capture(store, 51));
  publish(foreign, a, capture(foreign, 51));
  auto external = capture(store, 51, true);
  auto* chosen = external.get();
  std::optional<Store::ConversionTarget> old_target;
  std::optional<Export> old_part, alternative_part;
  Value* old_primary{};
  {
    Store::Scope scope(store);
    old_target.emplace(take(store.conversion(scope, a)));
    old_primary = Physical::get(old_target->source());
    CHECK(store.adopt(*old_target, external));
    auto old = take(store.bound(scope, a));
    auto alt = take(store.bound_alternative(scope, a));
    old_part.emplace(take(old.export_part(0)));
    alternative_part.emplace(take(alt.export_part(0)));
    const auto calls = memory.calls;
    refused(store.promote_alternative(a), Error::unavailable);
    CHECK(memory.calls == calls && scope.bind(a).value == old_primary);
    CHECK(take(scope.bind_alternative(a)).value == chosen);
  }
  {
    // No bound Scope remains: an entirely unbound Scope still blocks promotion.
    Store::Scope unbound(store);
    const auto calls = memory.calls;
    refused(store.promote_alternative(a), Error::unavailable);
    CHECK(memory.calls == calls && store.borrow().bind(a).value == old_primary);
  }
  CHECK(old_primary->references == 3); // Catalog + target + export.
  CHECK(chosen->references == 3); // Catalog + external + export.
  const auto cut = store.borrow().cut, calls = memory.calls;
  memory.fail = true;
  CHECK(store.promote_alternative(a));
  memory.fail = false;
  CHECK(memory.calls == calls && store.borrow().cut == cut);
  CHECK(old_primary->references == 2 && chosen->references == 3);
  CHECK(store.borrow().bind(a).value == chosen);
  CHECK(old_part->value == old_primary && old_part->number() == 51);
  CHECK(alternative_part->value == chosen && alternative_part->number() == 51);
  {
    Store::Scope late(store);
    CHECK(late.bind(a).value == chosen && late.bind(a).value->specification.complemented);
    CHECK(take(store.bound(late, a)).binding().version == cut);
    refused(late.bind_alternative(a), Error::absent);
  }
  refused(store.promote_alternative(a), Error::absent);
  auto delayed = capture(store, 51);
  CHECK(store.adopt(*old_target, delayed));
  CHECK(Physical::compatibility_source == old_primary); // Not the promoted image.
  refused(foreign.adopt(*old_target, delayed), Error::conflict);
  CHECK(store.discard_alternative(a));
  old_part.reset(); CHECK(old_primary->references == 1);
  publish(store, a, capture(store, 52));
  store.drain();
  expect(old_target->source(), 51);
  refused(store.adopt(*old_target, delayed), Error::absent);
  old_target.reset(); // Retained source never prolonged logical eligibility.
  CHECK(alternative_part->value == chosen && chosen->references == 2);
  alternative_part.reset(); CHECK(chosen->references == 1);
  external = {};
}

void accepted_work(bool target_object, bool install) {
  Memory memory;
  Store store(memory);
  publish(store, a, capture(store, 61)); publish(store, b, capture(store, 71));
  auto alternate = capture(store, 61, true); adopt(store, a, alternate);
  const auto id = target_object ? a : b;
  const std::array<Store::Change, 1> changes{{{id, capture(store, target_object ? 62 : 72)}}};
  Store::Prepared packet(store);
  {
    Store::Scope basis(store); CHECK(store.prepare(basis, changes, packet));
  }
  const auto calls = memory.calls, cut = store.borrow().cut;
  auto* before = store.borrow().bind(a).value;
  refused(store.promote_alternative(a), Error::unavailable);
  CHECK(memory.calls == calls && store.borrow().cut == cut && store.borrow().bind(a).value == before);
  CHECK(store.discard_alternative(a)); // Unlike promotion, safe with accepted work.
  CHECK(packet.disposition() == orbital::PublicationState::accepted);
  CHECK(store.borrow().bind(a).value == before && store.borrow().cut == cut);
  adopt(store, a, alternate); // Restore the alternative without touching its primary.
  if (install) CHECK(store.install(packet)); else packet.abort();
  expect(store.borrow().bind(a), target_object && install ? 62 : 61);
  if (target_object && install) {
    store.drain(); refused(store.promote_alternative(a), Error::absent);
  } else {
    CHECK(store.promote_alternative(a));
    CHECK(store.borrow().bind(a).value->specification.complemented);
  }
}

void current_tip_only() {
  Memory memory;
  Store store(memory);
  publish(store, a, capture(store, 31));
  Store::Scope old(store);
  auto old_target = take(store.conversion(old, a));
  publish(store, a, capture(store, 32));
  auto historical = capture(store, 31, true);
  CHECK(store.adopt(old_target, historical));
  historical = {};
  refused(store.discard_alternative(a), Error::absent); // Never search older slots.
  CHECK(take(old.bind_alternative(a)).number() == 31);
  auto current = capture(store, 32, true); adopt(store, a, current);
  CHECK(store.discard_alternative(a));
  CHECK(take(old.bind_alternative(a)).number() == 31);
  expect(store.borrow().bind(a), 32);
}

void alias_and_packet_states() {
  Memory memory;
  Store store(memory);
  auto owner = capture(store, 81);
  auto* value = owner.get(); publish(store, a, Store::Owner::retain(value));
  adopt(store, a, owner);
  CHECK(value->references == 3);
  CHECK(store.promote_alternative(a)); CHECK(value->references == 2);
  adopt(store, a, owner);
  CHECK(store.discard_alternative(a)); CHECK(value->references == 2);
  adopt(store, a, owner);
  Store::Prepared empty(store), refused_packet(store);
  { Store::Scope basis(store); refused(store.prepare(basis, {}, refused_packet), Error::unavailable); }
  CHECK(empty.disposition() == orbital::PublicationState::empty);
  CHECK(refused_packet.disposition() == orbital::PublicationState::empty);
  CHECK(store.promote_alternative(a)); // Core pins alone are not accepted work.
  Store empty_store;
  refused(empty_store.discard_alternative(a), Error::absent);
  refused(empty_store.promote_alternative(a), Error::absent);
  refused(store.discard_alternative(b), Error::absent);
}

struct Callback {
  enum class Action { drain, publish, reset } action;
  Store* store;
  Value* expected{};
  unsigned calls{};
  static void dispose(void* input) noexcept {
    auto& self = *static_cast<Callback*>(input); ++self.calls;
    CHECK(self.store->borrow().bind(a).value == self.expected);
    { Store::Scope scope(*self.store); refused(scope.bind_alternative(a), Error::absent); }
    if (self.action == Action::reset) { *self.store = {}; return; }
    self.store->drain();
    if (self.action == Action::publish) publish(*self.store, b, capture(*self.store, 92));
  }
};
void reentrant_disposal(bool promote) {
  for (auto action : {Callback::Action::drain, Callback::Action::publish, Callback::Action::reset}) {
    Memory memory;
    Store store(memory);
    Callback callback{action, &store};
    if (promote) {
      publish(store, a, take(store.capture({91, false, Callback::dispose, &callback}, {})));
      auto replacement = capture(store, 91, true); callback.expected = replacement.get();
      adopt(store, a, replacement); replacement = {};
      CHECK(store.promote_alternative(a));
    } else {
      publish(store, a, capture(store, 91)); callback.expected = store.borrow().bind(a).value;
      auto replacement = take(store.capture({91, true, Callback::dispose, &callback}, {}));
      adopt(store, a, replacement); replacement = {};
      CHECK(store.discard_alternative(a));
    }
    CHECK(callback.calls == 1);
    if (action == Callback::Action::reset) CHECK(!store.borrow().bind(a));
    else { expect(store.borrow().bind(a), 91); if (action == Callback::Action::publish) expect(store.borrow().bind(b), 92); }
  }
}
} // namespace
int main() {
  static_assert(sizeof(orbital::object_detail::History<Physical>::Entry) == 40);
  discard_live_selections(); promotion_lifetimes_and_authority();
  for (bool target : {false, true}) for (bool install : {false, true}) accepted_work(target, install);
  current_tip_only(); alias_and_packet_states(); reentrant_disposal(false); reentrant_disposal(true);
  std::puts("Cold choices: selection lifetime, pending publication, delayed authority, alias and disposal checks passed");
}
