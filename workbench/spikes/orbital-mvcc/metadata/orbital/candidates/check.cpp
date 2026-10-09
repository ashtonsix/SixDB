#include "candidate_store.h"
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <source_location>
#include <cstdlib>

using namespace compact_history;
namespace cd = compact_descriptor;

template<class T> void check(const T& condition,
    std::source_location location = std::source_location::current()) {
  if (static_cast<bool>(condition)) return;
  std::cerr << location.file_name() << ':' << location.line() << ": check failed\n";
  std::abort();
}

constexpr ObjectId A{1, 1}, B{1, 2}, C{1, 3};
constexpr std::array<std::byte, 2> logical{std::byte{0x51}, std::byte{0xa7}};

template<class Store> DescriptorOwner encode(Store& store, ObjectId id, std::uint64_t cut,
    std::uint64_t a, std::uint64_t b, bool columns = true,
    std::array<std::weak_ptr<const void>, 2>* witnesses = nullptr) {
  cd::RepresentationSpec spec{.object = id, .version = cut, .replica = {}, .format = {0, columns ? 1u : 2u},
      .logical_metadata = logical, .interpretation = {}, .dependencies = {}};
  std::vector<cd::PartSource> parts;
  if (columns) {
    for (std::size_t i = 0; i < 2; ++i) {
      auto owner = std::make_shared<const std::uint64_t>(i ? b : a);
      if (witnesses) (*witnesses)[i] = owner;
      parts.push_back({.id = {0, i}, .body = std::as_bytes(std::span(owner.get(), 1)), .interpretation = {}, .readable_padding = 0,
          .alignment = alignof(std::uint64_t), .backing = owner});
    }
  } else {
    auto owner = std::make_shared<const std::array<std::uint64_t, 2>>(std::array{a, b});
    parts.push_back({.id = {0, 0}, .body = std::as_bytes(std::span(*owner)), .interpretation = {}, .readable_padding = 0,
        .alignment = alignof(std::uint64_t), .backing = owner});
  }
  auto captured = store.capture(spec, parts); check(captured);
  return std::move(*captured);
}

std::uint64_t word(cd::View view, std::size_t index = 0) {
  auto bytes = view.body();
  check(bytes.size() >= (index + 1) * sizeof(std::uint64_t));
  std::uint64_t result;
  std::memcpy(&result, bytes.data() + index * sizeof(result), sizeof(result));
  return result;
}

void pair(cd::Selection selected, std::uint64_t a, std::uint64_t b) {
  check(selected);
  auto description = cd::describe(selected.descriptor);
  check(std::ranges::equal(description.logical_metadata, logical));
  if (description.format == orbital::ProgramId{0, 1}) {
    check(selected.part_count() == 2);
    check(word(*selected.part(std::size_t{0})) == a && word(*selected.part(std::size_t{1})) == b);
  } else {
    check((description.format == orbital::ProgramId{0, 2} && selected.part_count() == 1));
    auto body = *selected.part(std::size_t{0});
    check(word(body, 0) == a && word(body, 1) == b);
  }
}

template<class Store> void publish(Store& store, std::span<const Change> changes) {
  typename Store::Scope basis(store);
  auto prepared = store.prepare(basis, changes); check(prepared);
  const auto calls = store.statistics().allocation_calls;
  check(store.install(*prepared));
  check(store.statistics().allocation_calls == calls);
  check(prepared->disposition() == Disposition::installed);
  check(!store.install(*prepared));
}

template<class Store> void coherent_history() {
  Memory metadata;
  std::array<std::weak_ptr<const void>, 2> a_backing;
  cd::Export* escaped{};
  {
    Store store(metadata);
    {
      std::array<Change, 2> initial{{{A, encode(store, A, 1, 10, 20, true, &a_backing)},
          {B, encode(store, B, 1, 100, 200)}}};
      publish(store, initial);
    }
    {
      typename Store::Scope old(store);
      auto first = old.bind(A); check(first.part_count() == 2);
      auto bound = store.bound(old, A); check(bound && bound->selection().descriptor == first.descriptor);
      check(bound->binding().object == A && bound->binding().version == 1);
      check(word(*first.part(std::size_t{0})) == 10);
      auto export_result = bound->export_part(0); check(export_result); escaped = *export_result;
      std::array<Change, 2> next{{{A, encode(store, A, 2, 30, 40)}, {B, encode(store, B, 2, 300, 400)}}};
      auto prepared = store.prepare(old, next); check(prepared);
      auto conversion = store.conversion(old, A); check(conversion);
      {
        auto row = encode(store, A, 1, 10, 20, false);
        check(store.adopt(*conversion, row));
      }
      auto alternative = old.bind_alternative(A); check(alternative && alternative->part_count() == 1);
      {
        auto second_row = encode(store, A, 1, 10, 20, false);
        check(store.adopt(*conversion, second_row));
      }
      store.drain(); pair(*alternative, 10, 20); pair(first, 10, 20);
      check(store.install(*prepared)); // Physical adoption did not stale logical basis.
      check(bound->binding().object == A && bound->binding().version == 1);
      check(cd::describe(cd::view(escaped)).version == 1);
      pair(old.bind(B), 100, 200); // B was not discovered before the publication.
      pair(old.bind(A), 10, 20); // Reopen after all earlier native Views ended.
      auto current = store.borrow(); pair(current.bind(A), 30, 40); pair(current.bind(B), 300, 400);
      std::array<Change, 1> created{{{C, encode(store, C, 3, 7, 8)}}}; publish(store, created);
      check(!old.bind(C)); // A future insertion remains absent at the old cut.
      std::array<Change, 1> erased{{{A, {}}}}; publish(store, erased);
      check(!store.borrow().bind(A)); pair(old.bind(A), 10, 20);
      store.drain(); check(!a_backing[0].expired() && !a_backing[1].expired());
    }
    store.drain();
    check(!a_backing[0].expired() && a_backing[1].expired());
    check(word(cd::view(escaped)) == 10);
    auto description = cd::describe(cd::view(escaped));
    check(description.object == A && description.version == 1);
  }
  check(word(cd::view(escaped)) == 10); // Metadata resource still lives; Core is gone.
  cd::release(escaped); check(a_backing[0].expired()); check(metadata.live == 0);
}

template<class Store> void teardown_and_authority() {
  Memory metadata;
  {
    Store source(metadata);
    std::array<Change, 2> seed{{{A, encode(source, A, 1, 10, 20)}, {B, encode(source, B, 1, 100, 200)}}};
    publish(source, seed); seed = {};
    typename Store::Scope old(source);
    typename Store::Scope basis(source);
    std::array<Change, 1> next{{{A, encode(source, A, 2, 30, 40)}}};
    auto accepted = source.prepare(basis, next); check(accepted); next = {};
    Store foreign(metadata);
    check(!foreign.install(*accepted));
    Store moved(std::move(source));
    check(moved.install(*accepted));
    check(!moved.prepare(old, std::span<const Change>{}));
    pair(old.bind(B), 100, 200);
    moved = {};
    pair(old.bind(A), 10, 20); pair(old.bind(B), 100, 200); check(!old.bind(C));
  }
  check(metadata.live == 0);
  {
    Store store(metadata);
    typename Store::Scope basis(store);
    std::array<Change, 1> private_result{{{A, encode(store, A, 1, 10, 20)}}};
    auto accepted = store.prepare(basis, private_result); check(accepted);
    accepted->abort(); check(accepted->disposition() == Disposition::aborted);
    check(!store.install(*accepted)); check(!basis.bind(A));
    store = {}; // Prepared and Scope pins keep disposal's real Core alive.
  }
  check(metadata.live == 0);
}

template<class Store> void tombstones() {
  Memory metadata;
  {
    Store store(metadata);
    {
      typename Store::Scope oldest(store);
      std::array<Change, 1> erased{{{A, {}}}}; publish(store, erased);
      store.drain(); check(!oldest.bind(A));
    }
    store.drain(); check(store.statistics().versions == 0 && store.statistics().objects == 0);
    {
      std::array<Change, 1> initial{{{A, encode(store, A, 2, 10, 20)}}}; publish(store, initial);
    }
    std::array<Change, 1> erased{{{A, {}}}}; publish(store, erased);
    {
      typename Store::Scope basis(store);
      std::array<Change, 1> recreated{{{A, encode(store, A, 4, 30, 40)}}};
      auto prepared = store.prepare(basis, recreated); check(prepared);
      store.drain(); check(store.install(*prepared)); pair(store.borrow().bind(A), 30, 40);
    }
    store.drain();
    check(store.statistics().objects == 1 && store.statistics().versions == 1);
    for (std::uint64_t i = 0; i < 200; ++i) {
      publish(store, erased); store.drain();
      check(store.statistics().objects == 0 && store.statistics().versions == 0);
      typename Store::Scope basis(store);
      std::array<Change, 1> recreated{{{A, encode(store, A, basis.cut() + 1, i, i + 1)}}};
      auto prepared = store.prepare(basis, recreated); check(prepared); check(store.install(*prepared));
    }
  }
  check(metadata.live == 0);
}

template<class Store> void refusal() {
  for (std::size_t allowance = 0; allowance <= 2048; allowance += 8) {
    Memory metadata;
    {
      Store store(metadata);
      {
        std::array<Change, 2> initial{{{A, encode(store, A, 1, 10, 20)}, {B, encode(store, B, 1, 100, 200)}}};
        publish(store, initial);
      }
      typename Store::Scope basis(store);
      std::array<Change, 3> update{{{A, encode(store, A, 2, 30, 40)}, {B, encode(store, B, 2, 300, 400)},
          {C, encode(store, C, 2, 7, 8)}}};
      metadata.limit = metadata.live + allowance;
      auto prepared = store.prepare(basis, update);
      metadata.limit = std::numeric_limits<std::size_t>::max();
      if (prepared) { prepared->abort(); check(prepared->disposition() == Disposition::aborted); }
      else check(prepared.error() == Error::capacity);
      pair(basis.bind(A), 10, 20); pair(basis.bind(B), 100, 200); check(!basis.bind(C));
      pair(store.borrow().bind(A), 10, 20); pair(store.borrow().bind(B), 100, 200);
      check(!store.borrow().bind(C));
    }
    check(metadata.live == 0);
  }
}

template<class Store> void private_failure_and_empty() {
  Memory metadata;
  {
    Store store(metadata);
    {
      std::array<Change, 2> initial{{{A, encode(store, A, 1, 10, 20)}, {B, encode(store, B, 1, 100, 200)}}};
      publish(store, initial);
    }
    typename Store::Scope basis(store);
    auto first_private = encode(store, A, 2, 30, 40);
    auto before = metadata.live; metadata.limit = before;
    cd::RepresentationSpec second{.object = B, .version = 2, .replica = {}, .format = {},
        .logical_metadata = logical, .interpretation = {}, .dependencies = {}};
    std::array<cd::PartSource, 2> empty{}; empty[0].id = {0, 0}; empty[1].id = {0, 1};
    auto refused = store.capture(second, empty); check(!refused && refused.error() == Error::capacity);
    check(metadata.live == before); metadata.limit = std::numeric_limits<std::size_t>::max();
    pair(basis.bind(A), 10, 20); pair(basis.bind(B), 100, 200);
    auto zero = store.capture(second, empty); check(zero);
    std::array<Change, 1> updated{{{B, std::move(*zero)}}};
    auto accepted = store.prepare(basis, updated); check(accepted); check(store.install(*accepted));
    auto selected = store.borrow().bind(B); check(selected.part_count() == 2);
    check(selected.part(std::size_t{0})->body().empty() && selected.part(std::size_t{1})->body().empty());
  }
  check(metadata.live == 0);
}

template<class Store> void private_reads() {
  Memory metadata;
  {
    Store store(metadata);
    {
      std::array<Change, 2> initial{{{A, encode(store, A, 1, 10, 20)}, {B, encode(store, B, 1, 100, 200)}}};
      publish(store, initial);
    }
    typename Store::Scope basis(store);
    std::array<Change, 2> changes{{{A, encode(store, A, 2, 30, 40)}, {B, {}}}};
    auto a = Store::select_private(basis, std::span(changes).first(1), A);
    auto b = Store::select_private(basis, std::span(changes).first(1), B);
    const auto sum = word(*a.part(std::size_t{0})) + word(*a.part(std::size_t{1}));
    changes[1].representation = encode(store, B, 2, sum, word(*b.part(std::size_t{1})));
    auto prepared = store.prepare(basis, changes); check(prepared); check(store.install(*prepared));
    pair(store.borrow().bind(B), 70, 200); pair(basis.bind(B), 100, 200);
  }
  check(metadata.live == 0);
}

template<class Store> void callbacks() {
  for (int mode = 0; mode < 2; ++mode) {
    Memory metadata;
    {
      Store store(metadata);
      std::size_t called = 0, nested = 1;
      {
        auto backing = std::shared_ptr<const std::uint64_t>(new std::uint64_t(10), [&](const std::uint64_t* word) {
          delete word; ++called; nested = store.drain();
          if (mode == 0) store = {};
          else {
            std::array<Change, 2> update{{{A, {}}, {B, encode(store, B, 3, 7, 8)}}}; publish(store, update);
          }
        });
        cd::RepresentationSpec spec{.object = A, .version = 1, .replica = {}, .format = {0, 0},
            .logical_metadata = logical, .interpretation = {}, .dependencies = {}};
        std::array<cd::PartSource, 1> part{{{.id = {0, 0}, .body = std::as_bytes(std::span(backing.get(), 1)),
            .interpretation = {}, .readable_padding = 0, .alignment = alignof(std::uint64_t), .backing = backing}}};
        auto descriptor = store.capture(spec, part); check(descriptor);
        std::array<Change, 1> initial{{{A, std::move(*descriptor)}}}; publish(store, initial);
      }
      {
        std::array<Change, 1> update{{{A, encode(store, A, 2, 30, 40)}}}; publish(store, update);
      }
      store.drain(); check(called == 1 && nested == 0);
      if (mode == 0) check(metadata.live == 0);
      else { store.drain(); check(!store.borrow().bind(A)); pair(store.borrow().bind(B), 7, 8); }
    }
    check(metadata.live == 0);
  }
}

template<class Store> void conversion_authority() {
  Memory metadata;
  {
    Store first(metadata), second(metadata);
    {
      std::array<Change, 1> a{{{A, encode(first, A, 1, 10, 20)}}}; publish(first, a);
      std::array<Change, 1> b{{{A, encode(second, A, 1, 100, 200)}}}; publish(second, b);
    }
    std::optional<typename Store::ConversionTarget> delayed;
    DescriptorOwner row;
    {
      typename Store::Scope source(first);
      auto target = first.conversion(source, A); check(target);
      delayed.emplace(std::move(*target)); row = encode(first, A, 1, 10, 20, false);
    }
    typename Store::Scope destination(second);
    auto correct = second.conversion(destination, A); check(correct);
    check(cd::same_logical_state(correct->source().descriptor, row.get())); // Labels alone are insufficient.
    auto rejected = second.adopt(*delayed, row); check(!rejected && rejected.error() == Error::conflict);
    first = {}; // The old incarnation cannot vanish/reuse its address while the target lives.
    pair(delayed->source(), 10, 20); pair(destination.bind(A), 100, 200);
    auto correct_row = encode(second, A, 1, 100, 200, false);
    check(second.adopt(*correct, correct_row)); pair(*destination.bind_alternative(A), 100, 200);
  }
  check(metadata.live == 0);
  {
    Store store(metadata);
    {
      std::array<Change, 1> initial{{{A, encode(store, A, 1, 10, 20)}}}; publish(store, initial);
    }
    std::optional<typename Store::ConversionTarget> delayed;
    DescriptorOwner row;
    {
      typename Store::Scope source(store);
      auto target = store.conversion(source, A); check(target);
      delayed.emplace(std::move(*target)); row = encode(store, A, 1, 10, 20, false);
    }
    {
      std::array<Change, 1> update{{{A, encode(store, A, 2, 30, 40)}}}; publish(store, update);
    }
    store.drain(); pair(delayed->source(), 10, 20); check(store.statistics().versions == 1);
    auto obsolete = store.adopt(*delayed, row); check(!obsolete && obsolete.error() == Error::absent);
  }
  check(metadata.live == 0);
}

template<class Store> void abort_callback() {
  Memory metadata;
  {
    Store store(metadata);
    {
      std::array<Change, 1> initial{{{A, encode(store, A, 1, 10, 20)}}}; publish(store, initial);
    }
    std::array<Change, 1> erased{{{A, {}}}}; publish(store, erased);
    typename Store::Scope basis(store);
    std::optional<typename Store::Prepared> accepted;
    bool called = false;
    {
      auto backing = std::shared_ptr<const std::uint64_t>(new std::uint64_t(30), [&](const std::uint64_t* word) {
        delete word; called = true; check(store.drain() > 0); store = {};
      });
      cd::RepresentationSpec spec{.object = A, .version = 3, .replica = {}, .format = {},
          .logical_metadata = logical, .interpretation = {}, .dependencies = {}};
      std::array<cd::PartSource, 1> part{{{.id = {0, 0}, .body = std::as_bytes(std::span(backing.get(), 1)),
          .interpretation = {}, .readable_padding = 0, .alignment = alignof(std::uint64_t), .backing = backing}}};
      auto descriptor = store.capture(spec, part); check(descriptor);
      std::array<Change, 1> changes{{{A, std::move(*descriptor)}}};
      auto prepared = store.prepare(basis, changes); check(prepared); accepted.emplace(std::move(*prepared));
    }
    accepted->abort(); check(called && accepted->disposition() == Disposition::aborted);
    check(!basis.bind(A)); accepted.reset();
  }
  check(metadata.live == 0);
}

// A's private backing is disposed before the outer batch unpins B. Its
// callback must not accept another batch that mistakes B's pin for its own.
template<class Store> void two_change_abort_callback() {
  for (bool explicit_abort : {false, true}) {
    Memory metadata;
    {
      Store store(metadata);
      {
        std::array<Change, 1> initial{{{B, encode(store, B, 1, 100, 200)}}};
        publish(store, initial);
      }
      std::array<Change, 1> deleted{{{B, {}}}}; publish(store, deleted);
      typename Store::Scope basis(store);
      std::optional<typename Store::Prepared> accepted, nested;
      std::size_t called = 0;
      {
        auto backing = std::shared_ptr<const std::uint64_t>(new std::uint64_t(30), [&](const auto* word) {
          delete word; ++called;
          check(store.drain() > 0); // B stays pinned while obsolete B's body can go.
          check(store.statistics().objects == 1 && store.statistics().versions == 1);
          std::array<Change, 1> change{{{B, encode(store, B, 3, 300, 400)}}};
          auto prepared = store.prepare(basis, change);
          if (prepared) nested.emplace(std::move(*prepared));
          check(!prepared && prepared.error() == Error::unavailable);
          if (explicit_abort) accepted->abort(); // Terminal before owner callbacks.
        });
        cd::RepresentationSpec spec{.object = A, .version = 3, .replica = {}, .format = {},
            .logical_metadata = logical, .interpretation = {}, .dependencies = {}};
        std::array<cd::PartSource, 1> part{{{.id = {0, 0}, .body = std::as_bytes(std::span(backing.get(), 1)),
            .interpretation = {}, .readable_padding = 0, .alignment = alignof(std::uint64_t), .backing = backing}}};
        auto descriptor = store.capture(spec, part); check(descriptor);
        std::array<Change, 2> changes{{{A, std::move(*descriptor)}, {B, encode(store, B, 3, 300, 400)}}};
        auto prepared = store.prepare(basis, changes); check(prepared);
        accepted.emplace(std::move(*prepared));
      }
      if (explicit_abort) {
        accepted->abort(); check(accepted->disposition() == Disposition::aborted);
        accepted->abort();
        auto installed = store.install(*accepted);
        check(!installed && installed.error() == Error::rejected);
      } else accepted.reset();
      check(called == 1 && !nested && !basis.bind(A) && !basis.bind(B));
      // Disposal finishes the exclusion. A later batch can reuse B normally.
      std::array<Change, 1> change{{{B, encode(store, B, 3, 300, 400)}}};
      auto retry = store.prepare(basis, change); check(retry);
      check(store.install(*retry)); pair(store.borrow().bind(B), 300, 400);
      check(!basis.bind(B));
    }
    check(metadata.live == 0);
  }
}

// A later owner callback accepts private work after the first debt retires.
// That enrollment must neither skip B's remaining debt nor lose C's private pin.
template<class Store> void private_prepare_callback() {
  Memory metadata;
  constexpr ObjectId X{1, 4};
  {
    Store store(metadata);
    std::optional<typename Store::Prepared> accepted;
    bool armed = false, called = false;
    {
      auto body = std::shared_ptr<const std::uint64_t>(new std::uint64_t(10), [&](const auto* word) {
        delete word;
        if (!armed) return;
        called = true;
        typename Store::Scope basis(store);
        std::array<Change, 1> change{{{C, encode(store, C, 3, 30, 40)}}};
        auto prepared = store.prepare(basis, change); check(prepared);
        accepted.emplace(std::move(*prepared));
      });
      cd::RepresentationSpec spec{.object = A, .version = 1, .replica = {}, .format = {},
          .logical_metadata = logical, .interpretation = {}, .dependencies = {}};
      std::array<cd::PartSource, 1> source{{{.id = {0, 0}, .body = std::as_bytes(std::span(body.get(), 1)),
          .interpretation = {}, .readable_padding = 0, .alignment = alignof(std::uint64_t), .backing = body}}};
      auto physical = store.capture(spec, source); check(physical);
      std::array<Change, 3> initial{{{X, encode(store, X, 1, 1, 2)},
          {A, std::move(*physical)}, {B, encode(store, B, 1, 100, 200)}}};
      publish(store, initial);
    }
    {
      typename Store::Scope old(store);
      std::array<Change, 3> changes{{{X, {}}, {A, encode(store, A, 2, 20, 30)},
          {B, encode(store, B, 2, 110, 210)}}};
      publish(store, changes);
    }
    armed = true;
    while (store.drain()) {}
    check(called && accepted);
    check(store.statistics().versions == 2); // Both obsolete bodies were removed.
    check(store.statistics().objects == 3); // Private C is still pinned.
    accepted.reset();
    check(store.statistics().objects == 2);
    typename Store::Scope current(store);
    check(!current.bind(X) && !current.bind(C));
    pair(current.bind(A), 20, 30); pair(current.bind(B), 110, 210);
  }
  check(!metadata.live);
}

template<class Store> void run(const char* name) {
  coherent_history<Store>(); teardown_and_authority<Store>(); tombstones<Store>();
  refusal<Store>(); private_failure_and_empty<Store>(); private_reads<Store>(); callbacks<Store>();
  conversion_authority<Store>(); abort_callback<Store>();
  two_change_abort_callback<Store>(); private_prepare_callback<Store>();
  std::cout << name << ": eleven grouped histories passed\n";
}

int main() {
  run<Store32>("v32-floor"); run<Store16>("v16-floor"); run<Store8>("v8-floor");
  run<Store<Slab<Version32>, compact_retention::DistinctRetention, DescriptorPhysical>>("v32-distinct");
  run<Store<Slab<Version16>, compact_retention::DistinctRetention, DescriptorPhysical>>("v16-distinct");
  run<Store<Chunks8, compact_retention::DistinctRetention, DescriptorPhysical>>("v8-distinct");
  run<Store<Slab<Version32>, compact_retention::FloorRetention, DescriptorPhysical, CompactObjects>>("compact-v32-floor");
  run<Store<Slab<Version16>, compact_retention::FloorRetention, DescriptorPhysical, CompactObjects>>("compact-v16-floor");
  run<Store<Chunks8, compact_retention::FloorRetention, DescriptorPhysical, CompactObjects>>("compact-v8-floor");
  run<Store<Slab<Version32>, compact_retention::DistinctRetention, DescriptorPhysical, CompactObjects>>("compact-v32-distinct");
  run<Store<Slab<Version16>, compact_retention::DistinctRetention, DescriptorPhysical, CompactObjects>>("compact-v16-distinct");
  run<Store<Chunks8, compact_retention::DistinctRetention, DescriptorPhysical, CompactObjects>>("compact-v8-distinct");
  run<Store<Slab<Version16>, compact_retention::FloorRetention, DescriptorPhysical, TipObjects>>("inline-v16-floor");
  run<Store<Slab<Version16>, compact_retention::DistinctRetention, DescriptorPhysical, TipObjects>>("inline-v16-distinct");
}
