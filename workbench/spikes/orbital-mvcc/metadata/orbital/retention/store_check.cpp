#include "checks.h"
#include "../candidates/candidate_store.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <optional>

namespace cd = compact_descriptor;
using namespace compact_history;
using compact_retention::DistinctRetention;
using compact_retention::FloorRetention;
constexpr ObjectId object{0, 17};
using Witnesses = std::array<std::weak_ptr<const void>, 4>;

template<class Store> DescriptorOwner representation(Store& store, std::uint64_t cut,
    bool columns = true, Witnesses* witnesses = nullptr, ObjectId key = object) {
  std::array<cd::PartSource, 4> parts{};
  if (columns) {
    for (std::size_t i = 0; i < 4; ++i) {
      auto body = std::make_shared<const std::uint64_t>(cut * 10 + i);
      if (witnesses) (*witnesses)[i] = body;
      parts[i] = {{0, i}, std::as_bytes(std::span(body.get(), 1)), {}, 0, alignof(std::uint64_t), body};
    }
  } else {
    auto body = std::make_shared<const std::array<std::uint64_t, 4>>(
        std::array{cut * 10, cut * 10 + 1, cut * 10 + 2, cut * 10 + 3});
    if (witnesses) (*witnesses)[0] = body;
    parts[0] = {{0, 0}, std::as_bytes(std::span(*body)), {}, 0, alignof(std::uint64_t), body};
  }
  cd::RepresentationSpec spec{.object = key, .version = cut, .replica = {},
      .format = {0, columns ? 4u : 1u}, .logical_metadata = std::as_bytes(std::span(&cut, 1)),
      .interpretation = {}, .dependencies = {}};
  auto captured = store.capture(spec, std::span(parts).first(columns ? 4 : 1));
  CHECK(captured);
  return std::move(*captured);
}

std::uint64_t word(cd::Selection selection, std::size_t index = 3) {
  CHECK(selection);
  auto part = selection.part(selection.part_count() == 4 ? index : 0);
  CHECK(part);
  auto body = part->body();
  const auto offset = selection.part_count() == 4 ? 0 : index * sizeof(std::uint64_t);
  CHECK(body.size() >= offset + sizeof(std::uint64_t));
  std::uint64_t value;
  std::memcpy(&value, body.data() + offset, sizeof(value));
  return value;
}

template<class Store> void publish(Store& store, std::uint64_t cut, bool columns = true,
    Witnesses* witnesses = nullptr, bool erased = false, ObjectId key = object) {
  typename Store::Scope basis(store);
  std::array<Change, 1> change{{{key, erased ? DescriptorOwner{} : representation(store, cut, columns, witnesses, key)}}};
  auto accepted = store.prepare(basis, change);
  CHECK(accepted && store.install(*accepted));
}

template<class Nodes, class Retention> void closing_cuts() {
  using Store = compact_history::Store<Nodes, Retention>;
  Memory metadata;
  std::array<Witnesses, 6> witnesses;
  {
    Store store(metadata);
    publish(store, 1, true, &witnesses[0]);
    std::optional<typename Store::Scope> one(std::in_place, store);
    publish(store, 2, true, &witnesses[1]);
    publish(store, 3, true, &witnesses[2]);
    std::optional<typename Store::Scope> three_a(std::in_place, store);
    std::optional<typename Store::Scope> three_b(std::in_place, store);
    for (std::uint64_t cut = 4; cut <= 6; ++cut) publish(store, cut, true, &witnesses[cut - 1]);
    std::optional<typename Store::Scope> current(std::in_place, store);
    CHECK(store.drain() > 0);
    CHECK(store.statistics().versions == (Retention::uses_distinct_cuts ? 3 : 6));
    CHECK(word(one->bind(object)) == 13 && word(three_b->bind(object)) == 33);
    CHECK(word(current->bind(object)) == 63);
    three_a.reset();
    CHECK(store.drain() == 0); // Another Scope still selects cut 3.
    CHECK(word(three_b->bind(object)) == 33);
    current.reset();
    CHECK(store.drain() == 0); // Current heads remain independently necessary.
    three_b.reset();
    if constexpr (Retention::uses_distinct_cuts) {
      CHECK(store.drain() > 0); // Oldest cut stays 1; cut 3 becomes reclaimable.
      CHECK(store.statistics().versions == 2);
      for (const auto& witness : witnesses[2]) CHECK(witness.expired());
    } else {
      CHECK(store.drain() == 0 && store.statistics().versions == 6);
    }
    CHECK(word(one->bind(object)) == 13 && word(store.borrow().bind(object)) == 63);
    one.reset();
    CHECK(store.drain() > 0 && store.statistics().versions == 1);
    CHECK(word(store.borrow().bind(object)) == 63);
  }
  CHECK(metadata.live == 0);
}

template<class Nodes> void absence_and_preparation() {
  using Store = compact_history::Store<Nodes, DistinctRetention>;
  Memory metadata;
  cd::Export* escaped = nullptr;
  {
    Store store(metadata);
    std::optional<typename Store::Scope> zero(std::in_place, store);
    publish(store, 1);
    std::optional<typename Store::Scope> one(std::in_place, store);
    auto retained = cd::retain_part(one->bind(object), 3);
    CHECK(retained); escaped = *retained;
    publish(store, 2, true, nullptr, true);
    std::optional<typename Store::Scope> two(std::in_place, store);
    publish(store, 3, false);
    std::optional<typename Store::Scope> three(std::in_place, store);
    publish(store, 4, false);
    store.drain();
    CHECK(!zero->bind(object) && !two->bind(object));
    CHECK(word(one->bind(object)) == 13 && word(three->bind(object)) == 33);
    CHECK(word(store.borrow().bind(object)) == 43);
    one.reset(); three.reset();
    CHECK(store.drain() > 0 && store.statistics().versions == 2);
    CHECK(!zero->bind(object) && !two->bind(object));
    publish(store, 5, true, nullptr, true);
    store.drain();
    CHECK(!store.borrow().bind(object) && store.statistics().versions == 2);
    {
      typename Store::Scope basis(store);
      std::array<Change, 1> change{{{object, representation(store, 6, false)}}};
      auto accepted = store.prepare(basis, change);
      CHECK(accepted);
      two.reset(); store.drain(); // The only old selected tombstone becomes unnecessary.
      CHECK(store.statistics().versions == 1 && store.statistics().objects == 1);
      zero.reset(); store.drain(); // The accepted Entry must survive tombstone eligibility.
      CHECK(store.statistics().objects == 1);
      CHECK(store.install(*accepted) && word(store.borrow().bind(object)) == 63);
      CHECK(!basis.bind(object));
    }
    store.drain();
    CHECK(store.statistics().versions == 1 && word(store.borrow().bind(object)) == 63);
  }
  auto body = cd::view(escaped).body();
  std::uint64_t value;
  CHECK(body.size() == sizeof(value));
  std::memcpy(&value, body.data(), sizeof(value));
  CHECK(value == 13); // Resolved Part retention does not keep its logical Version.
  cd::release(escaped);
  CHECK(metadata.live == 0);
}

template<class Nodes> void absence_only_churn() {
  using Store = compact_history::Store<Nodes, DistinctRetention>;
  Memory metadata;
  {
    Store store(metadata);
    typename Store::Scope zero(store);
    std::size_t warmed_bytes = 0;
    for (std::uint64_t n = 0; n < 512; ++n) {
      const ObjectId key{n ^ 0xb62d5a14, n + 100};
      Witnesses witnesses;
      publish(store, n * 2 + 1, true, &witnesses, false, key);
      CHECK(word(store.borrow().bind(key)) == (n * 2 + 1) * 10 + 3);
      CHECK(!zero.bind(key)); // This object did not exist when the Scope opened.
      publish(store, n * 2 + 2, true, nullptr, true, key);
      CHECK(!store.borrow().bind(key) && !zero.bind(key));
      CHECK(store.drain() > 0);
      CHECK(store.statistics().versions == 0 && store.statistics().objects == 0);
      for (const auto& witness : witnesses) CHECK(witness.expired());
      if (!n) warmed_bytes = metadata.live;
      else CHECK(metadata.live == warmed_bytes); // Fresh IDs do not accumulate absent Entries.
    }
  }
  CHECK(metadata.live == 0);
}

template<class Nodes> void scratch_refusal() {
  using Store = compact_history::Store<Nodes, DistinctRetention>;
  Memory metadata;
  {
    Store store(metadata);
    publish(store, 1);
    typename Store::Scope old(store);
    publish(store, 2); publish(store, 3);
    const auto before = store.statistics();
    metadata.limit = metadata.live;
    CHECK(store.drain() == 0);
    const auto refused = store.statistics();
    CHECK(refused.versions == before.versions && refused.requested_bytes == before.requested_bytes);
    CHECK(refused.maintenance_refusals == before.maintenance_refusals + 1);
    CHECK(word(old.bind(object)) == 13 && word(store.borrow().bind(object)) == 33);
    metadata.limit = std::numeric_limits<std::size_t>::max();
    CHECK(store.drain() > 0 && store.statistics().versions == 2);
    CHECK(store.statistics().last_sweep_cuts == 1 && store.statistics().last_sweep_bytes == 8);
  }
  CHECK(metadata.live == 0);
}

template<class Nodes> void run(const char* name) {
  closing_cuts<Nodes, FloorRetention>(); closing_cuts<Nodes, DistinctRetention>();
  absence_and_preparation<Nodes>(); absence_only_churn<Nodes>(); scratch_refusal<Nodes>();
  std::printf("%s: real Store exact-cut close/absence/preparation/refusal histories passed\n", name);
}

int main() {
  run<Slab<Version32>>("v32"); run<Slab<Version16>>("v16"); run<Chunks8>("v8");
}
