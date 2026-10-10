#if SIXDB_OBJECT_MODULE_TEST
#include "object_module_test.h"
#endif
#include "shape_policy.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <source_location>

namespace ch = compact_history;
namespace cs = compact_shape;
namespace cd = compact_descriptor;
namespace v = orbital_bench::versioned;
constexpr orbital::ObjectId A{17, 1}, B{17, 2};
constexpr orbital::ProgramId data_program{19, 3};

template<class T> void check(const T& condition,
    std::source_location where = std::source_location::current()) {
  if (static_cast<bool>(condition)) return;
  std::fprintf(stderr, "check failed: %s:%u\n", where.file_name(), where.line());
  std::abort();
}

template<class T> T take(orbital::Result<T> result) {
  check(result); return std::move(*result);
}

struct EncodedBody {
  std::byte* address;
  std::size_t size, alignment;
  EncodedBody(std::size_t bytes, std::size_t align)
      : address(static_cast<std::byte*>(::operator new(bytes, std::align_val_t(align)))),
        size(bytes), alignment(align) {}
  ~EncodedBody() { ::operator delete(address, std::align_val_t(alignment)); }
  orbital::MutableBytes writable() { return {address, size}; }
  orbital::Bytes bytes() const { return {address, size}; }
};

template<class Store> typename Store::Owner encode(Store& store, ch::Memory& memory,
    std::uint32_t value, bool columns = true,
    std::array<std::weak_ptr<const void>, 4>* witnesses = nullptr,
    orbital::ProgramId program = data_program, std::size_t count = 3) {
  const std::array<orbital_bench::Row, 3> sample{{
      {1, {2, value, 4}}, {3, {4, value + 1, 6}}, {5, {6, value + 2, 8}}}};
  check(count <= sample.size());
  const auto rows = std::span(sample).first(count);
  v::Schema schema;
  auto physical = take(v::shape(schema, columns ? v::Format::columns : v::Format::rows, rows.size()));
  std::array<v::WritePart, 4> writable{};
  std::array<cd::PartSource, 4> sources{};
  std::array<cs::BodySource, 4> bodies{};
  for (std::size_t i = 0; i < physical.part_count; ++i) {
    const auto& part = physical.parts[i];
    auto owner = std::make_shared<EncodedBody>(part.body_bytes, std::max(std::size_t{16}, part.alignment));
    if (witnesses) (*witnesses)[i] = owner;
    writable[i] = {part.id, owner->writable()};
    sources[i] = {part.id, owner->bytes(), part.interpretation, 0, part.alignment, owner};
    bodies[i] = {part.id, owner->bytes(), owner, nullptr};
  }
  v::KernelCosts costs;
  check(v::construct_into(schema, physical, {writable.data(), physical.part_count}, rows, costs));
  auto* shape = take(cs::prove_fixture(memory, schema, program, physical.interpretation,
      {sources.data(), physical.part_count}));
  auto captured = store.capture({shape, rows.size()}, {bodies.data(), physical.part_count});
  cs::release(shape);
  return take(std::move(captured));
}

std::uint64_t first_value(cs::Selection selection) {
  check(selection && selection.records() == 3);
  if (selection.part_count() == 1) {
    auto part = take(selection.part(std::size_t{0}));
    return cs::tuple_law(selection.shape()).read_unchecked<2>(
        static_cast<const v::tp::byte*>(static_cast<const void*>(part.address())));
  }
  check(selection.part_count() == 4);
  auto part = take(selection.part(std::size_t{2}));
  auto source = v::sp::dense_source<v::Column<20>>{
      static_cast<const std::uint8_t*>(static_cast<const void*>(part.address())), 3};
  return v::sp::bind_decoder<std::uint64_t>(source).get_unchecked(0);
}

std::uint64_t exported_value(cs::Export* exported, bool columns) {
  auto part = cs::view(exported);
  if (!columns) return cs::tuple_law(exported).read_unchecked<2>(
      static_cast<const v::tp::byte*>(static_cast<const void*>(part.address())));
  auto source = v::sp::dense_source<v::Column<20>>{
      static_cast<const std::uint8_t*>(static_cast<const void*>(part.address())), cs::records(exported)};
  return v::sp::bind_decoder<std::uint64_t>(source).get_unchecked(0);
}

template<class Store> void publish(Store& store, std::span<const typename Store::Change> changes) {
  typename Store::Scope basis(store);
  typename Store::Prepared prepared(store); check(store.prepare(basis, changes, prepared));
  const auto before = store.statistics().allocation_calls;
  check(store.install(prepared));
  check(store.statistics().allocation_calls == before);
}

template<class Store> void repeated_physical_binding() {
  ch::Memory memory;
  cs::Export* escaped{};
  {
    Store store(memory);
    auto same = encode(store, memory, 31);
    std::array<typename Store::Change, 1> changes{{{A, same}}};
    publish(store, changes);
    std::optional<typename Store::ConversionTarget> old_target;
    {
      typename Store::Scope old(store);
      auto selected_before_publication = old.bind(A);
      auto older = take(store.bound(old, A));
      auto issued_copy = older;
      old_target.emplace(take(store.conversion(old, A)));
      publish(store, changes); // The identical Realization has two logical cuts.
      typename Store::Scope current(store);
      auto newer = take(store.bound(current, A));
      check(older.selection().realization == newer.selection().realization);
      check(older.binding().object == A && newer.binding().object == A);
      check(older.binding().version == 1 && newer.binding().version == 2);
      check(older.selection().realization == selected_before_publication.realization);
      escaped = take(issued_copy.export_part(2, cs::Interpretation::selected_copy));
      check(cs::binding(escaped).version == 1 && exported_value(escaped, true) == 31);
    }
    store.drain();
    auto replacement = encode(store, memory, 31, false);
    auto refused = store.adopt(*old_target, replacement);
    check(!refused && refused.error() == orbital::Error::absent);
    // A physical-only target must not accidentally authorize the current cut.
    typename Store::Scope current(store);
    auto target = take(store.conversion(current, A));
    check(store.adopt(target, replacement));
    auto alternative = take(current.bind_alternative(A));
    check(alternative.part_count() == 1 && first_value(alternative) == 31);
  }
  check(cs::binding(escaped).object == A && cs::binding(escaped).version == 1);
  check(exported_value(escaped, true) == 31);
  cs::release(escaped);
  check(!memory.live);
}

template<class Store> void overwrite_erase_recreate() {
  ch::Memory memory;
  std::array<std::weak_ptr<const void>, 4> old_backing;
  cs::Export* escaped{};
  {
    Store store(memory);
    {
      std::array<typename Store::Change, 2> initial{{
          {A, encode(store, memory, 41, true, &old_backing)},
          {B, encode(store, memory, 101, false)}}};
      publish(store, initial);
    }
    {
      typename Store::Scope old(store);
      auto issued = take(store.bound(old, A));
      auto selected = issued.selection();
      {
        std::array<typename Store::Change, 1> update{{{A, encode(store, memory, 51)}}};
        publish(store, update);
        std::array<typename Store::Change, 1> erase{{{A, {}}}};
        publish(store, erase);
        std::array<typename Store::Change, 1> recreate{{{A, encode(store, memory, 61, false)}}};
        publish(store, recreate);
      }
      store.drain();
      check(first_value(selected) == 41 && first_value(old.bind(B)) == 101);
      auto exact = take(store.bound(old, A));
      check(exact.binding().version == 1 && exact.selection().realization == selected.realization);
      // Use the original issued pair after all three subsequent publications.
      escaped = take(issued.export_part(2, cs::Interpretation::selected_copy));
      typename Store::Scope current(store);
      check(first_value(current.bind(A)) == 61 && take(store.bound(current, A)).binding().version == 4);
    }
    store.drain();
    check(old_backing[0].expired() && old_backing[1].expired() && old_backing[3].expired());
    check(!old_backing[2].expired() && exported_value(escaped, true) == 41);
  }
  check(exported_value(escaped, true) == 41 && cs::binding(escaped).version == 1);
  cs::release(escaped);
  check(old_backing[2].expired() && !memory.live);
}

template<class Store> void exact_store_authority() {
  ch::Memory first_memory, second_memory;
  {
    Store first(first_memory), second(second_memory);
    std::array<typename Store::Change, 1> initial_a{{{A, encode(first, first_memory, 71)}}};
    std::array<typename Store::Change, 1> initial_b{{{A, encode(second, second_memory, 701)}}};
    publish(first, initial_a); publish(second, initial_b);
    typename Store::Scope first_scope(first), second_scope(second);
    auto delayed = take(first.conversion(first_scope, A));
    auto foreign_replacement = encode(first, first_memory, 71, false);
    auto refused = second.adopt(delayed, foreign_replacement);
    check(!refused && refused.error() == orbital::Error::conflict);
    check(first_value(second_scope.bind(A)) == 701);
    auto correct_second_target = take(second.conversion(second_scope, A));
    auto foreign_budget = second.adopt(correct_second_target, foreign_replacement);
    check(!foreign_budget && foreign_budget.error() == orbital::Error::conflict);
    std::array<typename Store::Change, 1> foreign_changes{{{A, foreign_replacement}}};
    typename Store::Prepared prepared(second);
    auto foreign_prepare = second.prepare(second_scope, foreign_changes, prepared);
    check(!foreign_prepare && foreign_prepare.error() == orbital::Error::conflict);
    // Closing the public Store handle does not reinterpret the target as the
    // other Store incarnation, even when Object ID and cut are equal.
    first = Store{};
    check(first_value(delayed.source()) == 71);
    check(!second.adopt(delayed, foreign_replacement));
  }
  check(!first_memory.live && !second_memory.live);
}

template<class Store> void conversion_compatibility_refusals() {
  ch::Memory memory;
  {
    Store store(memory);
    std::array<typename Store::Change, 1> initial{{{A, encode(store, memory, 71)}}};
    publish(store, initial); initial = {};
    typename Store::Scope basis(store);
    const auto cut = basis.cut();
    auto primary = basis.bind(A);
    auto target = take(store.conversion(basis, A));
    auto first_alternative = encode(store, memory, 71, false);
    check(store.adopt(target, first_alternative)); first_alternative = {};
    auto retained_alternative = take(basis.bind_alternative(A));

    // Both replacements are valid, same-resource physical realizations. Only
    // their semantic program or logical row count makes adoption incompatible.
    std::array<typename Store::Owner, 2> incompatible{
        encode(store, memory, 71, false, nullptr, {19, 4}),
        encode(store, memory, 71, false, nullptr, data_program, 2)};
    for (const auto& replacement : incompatible) {
      auto refused = store.adopt(target, replacement);
      check(!refused && refused.error() == orbital::Error::conflict);
      check(store.borrow().cut == cut && basis.bind(A).realization == primary.realization);
      typename Store::Scope current(store);
      auto unchanged = take(store.bound(current, A));
      check(unchanged.binding().object == A && unchanged.binding().version == cut);
      check(unchanged.selection().realization == primary.realization && first_value(unchanged.selection()) == 71);
      auto alternative = take(current.bind_alternative(A));
      check(alternative.realization == retained_alternative.realization && first_value(alternative) == 71);
    }

    // Refusal must neither consume the target nor disturb an existing borrow.
    auto replacement = encode(store, memory, 71, false);
    check(store.adopt(target, replacement));
    check(store.borrow().cut == cut && basis.bind(A).realization == primary.realization);
    auto adopted = take(basis.bind_alternative(A));
    check(adopted.realization == replacement.selection().realization && first_value(adopted) == 71);
    check(first_value(primary) == 71 && first_value(retained_alternative) == 71);
  }
  check(!memory.live);
}

template<class Store> void accepted_private_and_alternatives() {
  ch::Memory memory;
  {
    Store store(memory);
    std::array<typename Store::Change, 1> initial{{{A, encode(store, memory, 81)}}};
    publish(store, initial); initial = {};
    typename Store::Scope basis(store);
    auto target = take(store.conversion(basis, A));
    auto first_alternative = encode(store, memory, 81, false);
    check(store.adopt(target, first_alternative)); first_alternative = {};
    auto captured_alternative = take(basis.bind_alternative(A));
    std::array<typename Store::Change, 2> changes{{
        {A, encode(store, memory, 91)}, {B, encode(store, memory, 191, false)}}};
    typename Store::Prepared prepared(store); check(store.prepare(basis, changes, prepared));
    auto second_alternative = encode(store, memory, 81, false);
    check(store.adopt(target, second_alternative)); second_alternative = {};
    store.drain();
    check(first_value(captured_alternative) == 81);
    check(first_value(Store::select_private(basis, changes, B)) == 191 && !basis.bind(B));
    const auto calls = memory.calls;
    check(store.install(prepared)); check(calls == memory.calls);
    check(first_value(basis.bind(A)) == 81 && !basis.bind(B));
    typename Store::Scope current(store);
    check(first_value(current.bind(A)) == 91 && first_value(current.bind(B)) == 191);
  }
  check(!memory.live);
}

template<class Store> void accepted_while_older_history_retires() {
  ch::Memory memory;
  {
    Store store(memory);
    std::array<typename Store::Change, 1> changes{{{A, encode(store, memory, 201)}}};
    publish(store, changes); changes = {};
    std::optional<typename Store::Scope> first;
    first.emplace(store);
    changes = {{{A, encode(store, memory, 202)}}};
    publish(store, changes); changes = {};
    typename Store::Scope basis(store);
    changes = {{{A, encode(store, memory, 203)}}};
    typename Store::Prepared prepared(store); check(store.prepare(basis, changes, prepared)); changes = {};
    auto target = take(store.conversion(basis, A));
    auto alternative = encode(store, memory, 202, false);
    check(store.adopt(target, alternative)); alternative = {};
    first.reset(); store.drain(); // The prepared old-tip link must not retain a stale slot.
    check(store.statistics().versions == 1);
    const auto calls = memory.calls;
    check(store.install(prepared)); check(memory.calls == calls);
    store.drain();
    check(first_value(basis.bind(A)) == 202 && first_value(take(basis.bind_alternative(A))) == 202);
    typename Store::Scope current(store);
    check(first_value(current.bind(A)) == 203 && store.statistics().versions == 2);
  }
  check(!memory.live);
}

#if SIXDB_OBJECT_MODULE_TEST
template<class Store> void exact_alternative_escape() {
  ch::Memory memory, foreign_memory;
  std::array<std::weak_ptr<const void>, 4> first_backing;
  cs::Export* escaped{};
  {
    Store store(memory), foreign(foreign_memory);
    std::array<typename Store::Change, 1> initial{{{A, encode(store, memory, 301)}}};
    publish(store, initial); initial = {};
    typename Store::Scope basis(store), wrong(foreign);
    check(!store.bound_alternative(basis, A));
    auto foreign_scope = store.bound_alternative(wrong, A);
    check(!foreign_scope && foreign_scope.error() == orbital::Error::conflict);
    auto target = take(store.conversion(basis, A));
    auto first = encode(store, memory, 301, false, &first_backing);
    check(store.adopt(target, first)); first = {};
    memory.limit = memory.live;
    auto refused = store.bound_alternative(basis, A);
    check(!refused && refused.error() == orbital::Error::capacity);
    check(!first_backing[0].expired());
    memory.limit = SIZE_MAX;
    auto selected = take(store.bound_alternative(basis, A));
    const auto calls = memory.calls;
    auto repeated = take(store.bound_alternative(basis, A));
    check(memory.calls == calls && repeated.selection().realization == selected.selection().realization);
    check(selected.binding().object == A && selected.binding().version == 1);
    auto replacement = encode(store, memory, 301, false);
    check(store.adopt(target, replacement)); replacement = {};
    auto current = take(store.bound_alternative(basis, A));
    check(current.selection().realization != selected.selection().realization);
    check(!first_backing[0].expired());
    std::array<typename Store::Change, 1> update{{{A, encode(store, memory, 302)}}};
    publish(store, update); update = {}; store.drain();
    check(first_value(selected.selection()) == 301);
    escaped = take(selected.export_part(0, cs::Interpretation::selected_copy));
    check(cs::binding(escaped).object == A && cs::binding(escaped).version == 1);
    check(exported_value(escaped, false) == 301);
  }
  check(!first_backing[0].expired() && exported_value(escaped, false) == 301);
  cs::release(escaped);
  check(first_backing[0].expired() && !memory.live && !foreign_memory.live);
}
#endif

template<class Store> void run(const char* label) {
  repeated_physical_binding<Store>();
  overwrite_erase_recreate<Store>();
  exact_store_authority<Store>();
  conversion_compatibility_refusals<Store>();
  accepted_private_and_alternatives<Store>();
  accepted_while_older_history_retires<Store>();
  std::printf("%s: six actual encoded ownership/binding histories passed\n", label);
}

int main() {
#if SIXDB_OBJECT_MODULE_TEST
  using Selected = object_module_test::Store<ch::ShapePhysical>;
  static_assert(!std::is_aggregate_v<Selected::BoundSelection>);
  static_assert(!std::is_constructible_v<Selected::BoundSelection, cs::Selection, cs::Binding>);
  run<Selected>("orbital-object-shape");
  exact_alternative_escape<Selected>();
  std::puts("orbital-object-shape: exact alternative escape/refusal/replacement history passed");
#else
  static_assert(!std::is_aggregate_v<ch::ShapeStore16::BoundSelection>);
  static_assert(!std::is_constructible_v<ch::ShapeStore16::BoundSelection, cs::Selection, cs::Binding>);
  run<ch::ShapeStore32>("shape32-distinct");
  run<ch::ShapeStore16>("shape16-distinct");
  run<ch::ShapeStore8>("shape8-distinct");
  run<ch::CompactShapeStore32>("compact-shape32-distinct");
  run<ch::CompactShapeStore16>("compact-shape16-distinct");
  run<ch::CompactShapeStore8>("compact-shape8-distinct");
  run<ch::InlineShapeStore16>("inline-shape16-distinct");
#endif
}
