#include "object_material.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <source_location>

namespace {
using namespace object_test;
using orbital::Error;
using orbital::PublicationState;
constexpr orbital::ObjectId a{3, 11}, b{5, 13}, c{7, 17};
void check(bool condition, const char* expression,
    std::source_location where = std::source_location::current()) noexcept {
  if (condition) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), expression);
  std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)
template<class T> T take(orbital::Result<T> result) { CHECK(result); return std::move(*result); }
template<class T> void refused(const orbital::Result<T>& result, Error error) {
  CHECK(!result && result.error() == error);
}
struct Memory final : std::pmr::memory_resource {
  std::size_t live{}, calls{}, fail_at = SIZE_MAX;
  ~Memory() { CHECK(!live); }
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (calls++ == fail_at) throw std::bad_alloc{};
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes; return result;
  }
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
    CHECK(live >= bytes); live -= bytes;
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
Store::Owner capture(Store& store, std::uint64_t number, bool complemented = false) {
  return take(store.capture({number, complemented}, {}));
}
void expect(Selection selected, std::uint64_t number) { CHECK(selected && selected.number() == number); }
void publish(Store& store, std::span<const Store::Change> changes) {
  Store::Scope basis(store);
  Store::Prepared packet(store);
  CHECK(store.prepare(basis, changes, packet)); CHECK(store.install(packet));
}
void coherent_lifetimes() {
  Memory memory;
  std::optional<Export> escaped;
  {
    Store store(memory);
    {
      const std::array<Store::Change, 2> initial{{{a, capture(store, 10)}, {b, capture(store, 20)}}};
      publish(store, initial);
    }
    Store::Scope old(store);
    CHECK(old.cut() == 1); expect(old.bind(a), 10);
    auto exact = take(store.bound(old, a));
    {
      const std::array<Store::Change, 3> next{{{a, capture(store, 11)}, {b, capture(store, 21)}, {c, capture(store, 30)}}};
      publish(store, next);
    }
    Store::Scope middle(store);
    CHECK(middle.cut() == 2); expect(middle.bind(a), 11); expect(middle.bind(c), 30);
    const std::array<Store::Change, 1> erase{{{b, {}}}}; publish(store, erase);
    Store::Scope deleted(store); CHECK(!deleted.bind(b));
    {
      const std::array<Store::Change, 1> reborn{{{b, capture(store, 22)}}}; publish(store, reborn);
    }
    CHECK(store.drain(1) <= 1); store.drain();
    // First discovery occurs after replacement, deletion and recreation.
    expect(old.bind(b), 20); CHECK(!old.bind(c));
    expect(middle.bind(b), 21); CHECK(!deleted.bind(b));
    expect(store.borrow().bind(b), 22);
    escaped.emplace(take(exact.export_part(0)));
    CHECK(escaped->binding.object == a && escaped->binding.version == 1);
    store = {};
    expect(old.bind(a), 10); expect(old.bind(b), 20); CHECK(!old.bind(c));
  }
  CHECK(escaped->number() == 10 && memory.live == sizeof(Value));
  escaped.reset(); CHECK(!memory.live);
}
void publication_refusal() {
  unsigned failures{};
  // Fail each allocation in one multi-change preparation, including late ones.
  for (unsigned ordinal = 0; ordinal != 32; ++ordinal) {
    Memory memory;
    bool encountered{};
    {
      Store store(memory);
      { const std::array<Store::Change, 1> initial{{{a, capture(store, 1)}}}; publish(store, initial); }
      Store::Scope basis(store);
      const std::array<Store::Change, 3> changes{{{a, capture(store, 2)}, {b, capture(store, 3)}, {c, capture(store, 4)}}};
      Store::Prepared packet(store);
      memory.fail_at = memory.calls + ordinal;
      auto accepted = store.prepare(basis, changes, packet);
      memory.fail_at = SIZE_MAX;
      if (!accepted) {
        refused(accepted, Error::capacity); ++failures; encountered = true;
        CHECK(packet.disposition() == PublicationState::empty);
        expect(store.borrow().bind(a), 1); CHECK(!store.borrow().bind(b) && !store.borrow().bind(c));
        CHECK(store.prepare(basis, changes, packet)); // Same refused packet can retry.
      }
      expect(store.borrow().bind(a), 1); CHECK(!store.borrow().bind(b));
      Store::Prepared contender(store);
      refused(store.prepare(basis, changes, contender), Error::unavailable);
      packet.abort(); packet.abort(); CHECK(packet.disposition() == PublicationState::aborted);
      refused(store.install(packet), Error::rejected);
      refused(store.prepare(basis, changes, packet), Error::unavailable);
      expect(store.borrow().bind(a), 1); CHECK(!store.borrow().bind(c));
      CHECK(store.prepare(basis, changes, contender));
      const auto calls = memory.calls; memory.fail_at = calls;
      CHECK(store.install(contender)); CHECK(memory.calls == calls);
      memory.fail_at = SIZE_MAX;
      refused(store.install(contender), Error::rejected);
      Store::Scope current(store); CHECK(current.cut() == basis.cut() + 1);
      expect(current.bind(a), 2); expect(current.bind(b), 3); expect(current.bind(c), 4);
      expect(basis.bind(a), 1); CHECK(!basis.bind(b));
      Store::Prepared stale(store); refused(store.prepare(basis, changes, stale), Error::stale);
    }
    if (!encountered) { CHECK(failures); return; }
  }
  CHECK(false); // The bounded fault sweep must reach a successful preparation.
}
void conversion_authority() {
  Memory memory;
  std::optional<Export> escaped;
  std::optional<Store::ConversionTarget> target;
  {
    Store store(memory), other(memory);
    { const std::array<Store::Change, 2> initial{{{a, capture(store, 41)}, {b, capture(store, 42)}}}; publish(store, initial); }
    { const std::array<Store::Change, 1> initial{{{a, capture(other, 41)}}}; publish(other, initial); }
    {
      Store::Scope basis(store), foreign(other);
      target.emplace(take(store.conversion(basis, a)));
      auto first = capture(store, 41, true), second = capture(store, 41, true);
      refused(store.bound(foreign, a), Error::conflict);
      refused(other.adopt(*target, first), Error::conflict);
      CHECK(store.adopt(*target, first)); CHECK(store.borrow().cut == 1);
      auto exact = take(store.bound_alternative(basis, a));
      CHECK(exact.selection().value == first.get());
      CHECK(store.adopt(*target, second));
      CHECK(take(basis.bind_alternative(a)).value == second.get());
      escaped.emplace(take(exact.export_part(0)));
      CHECK(escaped->value == first.get() && escaped->binding.object == a && escaped->binding.version == 1);
      const std::array<Store::Change, 1> next{{{a, capture(store, 43)}}}; publish(store, next);
      store.drain(); expect(basis.bind(a), 41); expect(store.borrow().bind(a), 43);
    }
    store.drain(); // A target owns source bytes, but does not preserve logical history.
    expect(target->source(), 41);
    auto replacement = capture(store, 41, true);
    refused(store.adopt(*target, replacement), Error::absent);
    store = Store(memory);
    refused(store.adopt(*target, replacement), Error::conflict);
  }
  CHECK(escaped->number() == 41); target.reset();
  CHECK(memory.live == sizeof(Value)); escaped.reset();
}
void packet_lifetimes() {
  Memory memory;
  std::optional<Store::Prepared> packet;
  {
    Store store(memory), foreign(memory);
    Store::Scope basis(store);
    const std::array<Store::Change, 1> changes{{{a, capture(store, 91)}}};
    packet.emplace(store);
    refused(foreign.prepare(basis, changes, *packet), Error::conflict);
    CHECK(store.prepare(basis, changes, *packet));
    refused(foreign.install(*packet), Error::conflict);
    CHECK(store.install(*packet));
    store = Store(memory);
    Store::Scope replacement(store);
    refused(store.prepare(replacement, changes, *packet), Error::conflict);
  }
  CHECK(memory.live); packet.reset(); CHECK(!memory.live);
  { Store store(memory); packet.emplace(store); }
  CHECK(memory.live); packet.reset(); // Empty packets also pin their original Core.
}
struct Callback {
  Store* store{};
  Store::Prepared* aborting{};
  unsigned calls{};
  bool final{};
  static void dispose(void* input) noexcept {
    auto& self = *static_cast<Callback*>(input); ++self.calls;
    self.store->drain();
    Store::Scope basis(*self.store);
    if (self.final) {
      CHECK(!self.store->borrow().bind(a) && !basis.bind(a));
      return;
    }
    Store::Prepared attempt(*self.store);
    const std::array<Store::Change, 2> erased{{{a, {}}, {b, {}}}};
    refused(self.store->prepare(basis, erased, attempt), Error::unavailable);
    self.aborting->abort(); // Terminal state is visible before owner disposal.
  }
};
void disposal_reentry() {
  for (bool explicit_abort : {false, true}) {
    Memory memory;
    Store store(memory);
    Callback context{&store};
    {
      Store::Prepared packet(store); context.aborting = &packet;
      {
        Store::Scope basis(store);
        const std::array<Store::Change, 2> changes{{
            {a, take(store.capture({1, false, Callback::dispose, &context}, {}))},
            {b, take(store.capture({2, false, Callback::dispose, &context}, {}))}}};
        CHECK(store.prepare(basis, changes, packet));
      }
      if (explicit_abort) packet.abort();
    }
    CHECK(context.calls == 2 && !store.borrow().bind(a) && !store.borrow().bind(b));
    { const std::array<Store::Change, 1> next{{{c, capture(store, 3)}}}; publish(store, next); }
    expect(store.borrow().bind(c), 3);
  }
  Memory memory;
  Callback context; context.final = true;
  {
    Store store(memory); context.store = &store;
    const std::array<Store::Change, 1> initial{{{a, take(store.capture({7, false, Callback::dispose, &context}, {}))}}};
    publish(store, initial);
  }
  CHECK(context.calls == 1 && !memory.live);
}
} // namespace
int main() {
  coherent_lifetimes(); publication_refusal(); conversion_authority(); packet_lifetimes(); disposal_reentry();
  std::puts("Object catalog: coherent histories, publication refusal, conversion and lifetime checks passed");
}
