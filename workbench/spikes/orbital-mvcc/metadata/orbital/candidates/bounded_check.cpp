#include "candidate_store.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <source_location>

using namespace compact_history;
namespace cd = compact_descriptor;
constexpr ObjectId A{31, 1}, B{31, 2};
constexpr std::array<std::byte, 1> logical{std::byte{0x27}};

template<class T> void check(const T& condition,
    std::source_location where = std::source_location::current()) {
  if (static_cast<bool>(condition)) return;
  std::fprintf(stderr, "%s:%u: bounded check failed\n", where.file_name(), where.line());
  std::abort();
}

template<class Store> DescriptorOwner encode(Store& store, ObjectId id, std::uint64_t cut,
    std::uint64_t value, std::shared_ptr<const std::uint64_t> body = {}) {
  if (!body) body = std::make_shared<const std::uint64_t>(value);
  cd::RepresentationSpec spec{.object = id, .version = cut, .replica = {}, .format = {},
      .logical_metadata = logical, .interpretation = {}, .dependencies = {}};
  const std::array<cd::PartSource, 1> sources{{{.id = {0, 0}, .body = std::as_bytes(std::span(body.get(), 1)),
      .interpretation = {}, .readable_padding = 0, .alignment = alignof(std::uint64_t), .backing = body}}};
  auto captured = store.capture(spec, sources); check(captured); return std::move(*captured);
}

std::uint64_t word(cd::Selection selected) {
  check(selected); auto part = selected.part(std::size_t{0}); check(part);
  auto bytes = part->body(); check(bytes.size() == 8);
  std::uint64_t value; std::memcpy(&value, bytes.data(), 8); return value;
}

// An epoch no caller observed should not force a reader-cut plan rebuild.
template<class Store> void publish(Store& store, std::span<const Change> changes) {
  std::optional<typename Store::Prepared> accepted;
  {
    typename Store::Scope basis(store);
    auto prepared = store.prepare(basis, changes); check(prepared);
    accepted.emplace(std::move(*prepared));
  }
  const auto allocations = store.statistics().allocation_calls;
  check(store.install(*accepted)); check(store.statistics().allocation_calls == allocations);
}

template<class Store> void update(Store& store, ObjectId id, std::uint64_t value) {
  const auto cut = store.borrow().cut + 1;
  std::array<Change, 1> change{{{id, encode(store, id, cut, value)}}}; publish(store, change);
}

template<class Store> std::size_t finish(Store& store, std::size_t budget = 1) {
  std::size_t calls = 0;
  while (store.statistics().maintenance_pending) {
    const auto before = store.statistics();
    const auto work = store.drain(budget);
    check(work && work <= budget);
    check(store.statistics().reclamation_work - before.reclamation_work == work);
    check(++calls < 100000);
  }
  check(store.drain(budget) == 0); return calls;
}

template<class Store> void limits_and_cuts() {
  Memory memory;
  {
    Store store(memory); update(store, A, 10);
    std::optional<typename Store::Scope> one(std::in_place, store);
    update(store, A, 20); update(store, A, 30);
    std::optional<typename Store::Scope> three_a(std::in_place, store), three_b(std::in_place, store);
    update(store, A, 40); update(store, A, 50);
    std::optional<typename Store::Scope> current(std::in_place, store);
    const auto before = store.statistics();
    check(store.drain(0) == 0);
    auto zero = store.statistics();
    check(zero.versions == before.versions && zero.allocation_calls == before.allocation_calls);
    check(zero.reclamation_work == before.reclamation_work && zero.plan_work == before.plan_work);
    check(finish(store) > 1);
    check(store.statistics().versions == (Store::exact_retention ? 3u : 5u));
    check(word(one->bind(A)) == 10 && word(three_b->bind(A)) == 30 && word(current->bind(A)) == 50);
    three_a.reset(); current.reset(); check(!store.statistics().maintenance_pending);
    three_b.reset(); finish(store);
    check(store.statistics().versions == (Store::exact_retention ? 2u : 5u));
    one.reset(); finish(store); check(store.statistics().versions == 1);
  }
  check(!memory.live);
}

template<class Store> void sparse_predecessors() {
  Memory memory;
  {
    Store store(memory); update(store, A, 10); update(store, B, 20);
    typename Store::Scope two(store);
    update(store, B, 30); typename Store::Scope three(store);
    update(store, A, 40); update(store, B, 50); update(store, A, 60);
    finish(store);
    check(word(two.bind(A)) == 10 && word(three.bind(A)) == 10);
    check(word(two.bind(B)) == 20 && word(three.bind(B)) == 30);
    check(word(store.borrow().bind(A)) == 60 && word(store.borrow().bind(B)) == 50);
    check(store.statistics().versions == (Store::exact_retention ? 5u : 6u));
  }
  check(!memory.live);
}

template<class Store> void interrupted_plan() {
  Memory memory;
  {
    Store store(memory); update(store, A, 10);
    std::optional<typename Store::Scope> one(std::in_place, store);
    update(store, A, 20);
    std::optional<typename Store::Scope> two_a(std::in_place, store), two_b(std::in_place, store);
    update(store, A, 30);
    std::optional<typename Store::Scope> three(std::in_place, store);
    std::array<Change, 2> fourth{{{A, encode(store, A, 4, 40)}, {B, encode(store, B, 4, 400)}}};
    publish(store, fourth); fourth = {};
    check(store.drain(1) == 1); // Setup only: its saved Scope pointer is not yet visited.
    one.reset(); two_a.reset(); // Unique and duplicate pointer removal mid-build.
    typename Store::Scope four(store); // Newly current enrollment.
    update(store, B, 500); // Newly admitted cut 4 needs B, which cut 2/3 saw absent.
    update(store, A, 60);
    finish(store);
    check(word(two_b->bind(A)) == 20 && word(three->bind(A)) == 30 && word(four.bind(A)) == 40);
    check(!two_b->bind(B) && !three->bind(B) && word(four.bind(B)) == 400);
    check(word(store.borrow().bind(A)) == 60 && word(store.borrow().bind(B)) == 500);
    check(store.statistics().versions == 6);
    if constexpr (Store::exact_retention) check(store.statistics().cut_visits > 0);
  }
  check(!memory.live);
}

template<class Store> void refusal_and_reuse() {
  Memory memory;
  {
    Store store(memory); update(store, A, 10);
    typename Store::Scope old(store);
    update(store, A, 20); update(store, A, 30);
    const auto before = store.statistics(); memory.limit = memory.live;
    const auto work = store.drain(1);
    check(work == 1);
    if constexpr (Store::exact_retention) {
      auto refused = store.statistics();
      check(refused.maintenance_refusals == before.maintenance_refusals + 1);
      check(refused.versions == 3 && refused.requested_bytes == before.requested_bytes);
      check(refused.maintenance_pending && refused.reclamation_work == before.reclamation_work + 1);
    }
    memory.limit = std::numeric_limits<std::size_t>::max(); finish(store);
    check(word(old.bind(A)) == 10 && word(store.borrow().bind(A)) == 30);
    check(store.statistics().versions == (Store::exact_retention ? 2u : 3u));
    const auto plan_capacity = store.statistics().cut_capacity_bytes;
    const auto plan_visits = store.statistics().cut_visits;
    update(store, A, 40); finish(store);
    check(store.statistics().cut_capacity_bytes == plan_capacity);
    // Cached cuts survive a publication whose writer basis closed beforehand.
    check(store.statistics().cut_visits - plan_visits < 8);
  }
  check(!memory.live);
}

template<class Store> void accepted_and_recycled() {
  Memory memory;
  {
    Store store(memory);
    typename Store::Scope zero(store); update(store, A, 10);
    std::array<Change, 1> erased{{{A, {}}}}; publish(store, erased);
    typename Store::Scope basis(store);
    std::array<Change, 2> changes{{{A, encode(store, A, 3, 30)}, {B, encode(store, B, 3, 300)}}};
    auto prepared = store.prepare(basis, changes); check(prepared);
    finish(store); // Pin and debt enrollment must survive history reclamation.
    check(store.statistics().objects == 2 && !basis.bind(A) && !zero.bind(B));
    const auto allocations = store.statistics().allocation_calls;
    check(store.install(*prepared)); check(store.statistics().allocation_calls == allocations);
    check(word(store.borrow().bind(A)) == 30 && word(store.borrow().bind(B)) == 300);
    check(!zero.bind(A) && !zero.bind(B)); finish(store);
  }
  check(!memory.live);
  {
    Store store(memory);
    typename Store::Scope zero(store);
    for (std::uint64_t n = 0; n < 96; ++n) {
      const ObjectId fresh{42, n}; update(store, fresh, n);
      std::array<Change, 1> erased{{{fresh, {}}}}; publish(store, erased);
      finish(store);
      check(!zero.bind(fresh) && !store.borrow().bind(fresh));
      if constexpr (Store::exact_retention) check(store.statistics().objects == 0);
    }
  }
  check(!memory.live);
}

template<class Store> void callbacks_and_fairness() {
  Memory memory;
  {
    bool armed = false;
    std::size_t callbacks = 0;
    std::function<DescriptorOwner(std::uint64_t)> custom;
    Store store(memory); // Callback state also lives through final owner disposal.
    custom = [&](std::uint64_t cut) {
      auto body = std::shared_ptr<const std::uint64_t>(new std::uint64_t(cut), [&](const auto* value) {
        delete value;
        if (!armed || callbacks == 32) return;
        ++callbacks; check(store.drain(1) == 0);
        const auto next = store.borrow().cut + 1;
        std::array<Change, 1> update{{{A, custom(next)}}}; publish(store, update);
      });
      return encode(store, A, cut, cut, std::move(body));
    };
    std::weak_ptr<const std::uint64_t> old_b;
    {
      auto body = std::make_shared<const std::uint64_t>(100); old_b = body;
      std::array<Change, 2> initial{{{A, custom(1)}, {B, encode(store, B, 1, 100, body)}}};
      publish(store, initial);
    }
    std::array<Change, 2> changes{{{A, custom(2)}, {B, encode(store, B, 2, 200)}}}; publish(store, changes);
    changes = {}; armed = true;
    std::size_t calls = 0;
    while (!old_b.expired()) {
      const auto before = store.statistics().reclamation_work;
      check(store.drain(1) == 1 && store.statistics().reclamation_work == before + 1);
      check(++calls < 200);
    }
    check(callbacks && callbacks < 32); // A cannot monopolize every restart.
    finish(store, 7);
    check(callbacks == 32 && store.statistics().maintenance_restarts >= callbacks);
    check(store.statistics().versions == 2 && word(store.borrow().bind(B)) == 200);
    armed = false;
  }
  check(!memory.live);
}

template<class Store> void long_chain() {
  Memory memory;
  {
    Store store(memory); update(store, A, 1); update(store, B, 2);
    for (std::uint64_t n = 3; n < 1027; ++n) update(store, A, n);
    update(store, B, 1027);
    const auto queued = store.statistics().debt_objects;
    check(queued == 2 && finish(store, 3) > 300);
    check(store.statistics().versions == 2);
    check(word(store.borrow().bind(A)) == 1026 && word(store.borrow().bind(B)) == 1027);
    const auto reservation_work = store.statistics().debt_reservation_work;
    for (std::uint64_t n = 0; n < 32; ++n) { update(store, A, n); finish(store); }
    check(store.statistics().debt_reservation_work == reservation_work); // Reuse scans no high-water holes.
  }
  check(!memory.live);
}

template<class Store> void continuous_writes() {
  for (bool same_object : {false, true}) {
    Memory memory;
    {
      Store store(memory);
      std::weak_ptr<const std::uint64_t> obsolete;
      {
        auto body = std::make_shared<const std::uint64_t>(10); obsolete = body;
        std::array<Change, 2> initial{{{A, encode(store, A, 1, 10, body)}, {B, encode(store, B, 1, 100)}}};
        publish(store, initial);
      }
      update(store, A, 20);
      for (std::uint64_t n = 0; n < 400; ++n) {
        update(store, same_object ? A : B, 1000 + n);
        const auto before = store.statistics().reclamation_work;
        const auto work = store.drain(1);
        check(work == 1 && store.statistics().reclamation_work == before + 1);
        if (n == 63) check(obsolete.expired()); // Progress while writes keep arriving.
      }
      check(obsolete.expired() && store.statistics().version_visits > 0);
      check(word(store.borrow().bind(same_object ? A : B)) == 1399);
      finish(store, 17); check(store.statistics().versions == 2);
    }
    check(!memory.live);
  }
}

template<class Store> void interleaved_readers() {
  Memory memory;
  {
    Store store(memory);
    constexpr std::array<ObjectId, 3> ids{{A, B, {31, 3}}};
    std::array<std::uint64_t, 3> current{};
    struct Saved {
      typename Store::Scope scope;
      std::array<std::uint64_t, 3> expected;
      Saved(const Store& owner, std::array<std::uint64_t, 3> values) : scope(owner), expected(values) {}
    };
    std::vector<std::unique_ptr<Saved>> readers;
    std::uint64_t random = 0x157b62de;
    for (std::uint64_t n = 0; n < 1200; ++n) {
      random = random * 6364136223846793005ULL + 1;
      const auto action = (random >> 32) % 7, index = (random >> 40) % 3;
      if (action < 3) {
        const auto value = action ? n + 1 : 0;
        std::array<Change, 1> change{{{ids[index], value ? encode(store, ids[index], store.borrow().cut + 1, value) : DescriptorOwner{}}}};
        if (action == 2) {
          std::optional<typename Store::Prepared> prepared;
          {
            typename Store::Scope basis(store);
            auto accepted = store.prepare(basis, change); check(accepted);
            prepared.emplace(std::move(*accepted));
          }
          check(store.drain(1) <= 1); // Accepted entry pins span a partial sweep.
          const auto allocations = store.statistics().allocation_calls;
          check(store.install(*prepared)); check(store.statistics().allocation_calls == allocations);
        } else publish(store, change);
        current[index] = value;
      } else if (action == 3 && readers.size() < 32)
        readers.push_back(std::make_unique<Saved>(store, current));
      else if (action == 4 && !readers.empty())
        readers.erase(readers.begin() + static_cast<std::ptrdiff_t>((random >> 48) % readers.size()));
      else {
        const auto budget = (random >> 48) % 8;
        const auto before = store.statistics().reclamation_work;
        const auto work = store.drain(budget);
        check(work <= budget && store.statistics().reclamation_work == before + work);
      }
      for (const auto& reader : readers) for (std::size_t i = 0; i < ids.size(); ++i) {
        auto selected = reader->scope.bind(ids[i]);
        if (reader->expected[i]) check(selected && word(selected) == reader->expected[i]);
        else check(!selected);
      }
      for (std::size_t i = 0; i < ids.size(); ++i) {
        auto selected = store.borrow().bind(ids[i]);
        if (current[i]) check(selected && word(selected) == current[i]); else check(!selected);
      }
    }
    readers.clear(); finish(store, 17);
    const auto present = std::count_if(current.begin(), current.end(), [](auto value) { return value != 0; });
    check(store.statistics().versions == static_cast<std::size_t>(present));
    check(store.statistics().objects == static_cast<std::size_t>(present));
  }
  check(!memory.live);
}

template<class Store> void run(const char* name) {
  limits_and_cuts<Store>(); sparse_predecessors<Store>(); interrupted_plan<Store>();
  refusal_and_reuse<Store>(); accepted_and_recycled<Store>(); callbacks_and_fairness<Store>(); long_chain<Store>();
  continuous_writes<Store>(); interleaved_readers<Store>();
  std::printf("%s: nine bounded maintenance histories passed\n", name);
}

int main() {
  run<Store32>("v32-floor"); run<Store16>("v16-floor"); run<Store8>("v8-floor");
  run<Store<Slab<Version32>, compact_retention::DistinctRetention>>("v32-distinct");
  run<Store<Slab<Version16>, compact_retention::DistinctRetention>>("v16-distinct");
  run<Store<Chunks8, compact_retention::DistinctRetention>>("v8-distinct");
  run<Store<Slab<Version32>, compact_retention::FloorRetention, DescriptorPhysical, CompactObjects>>("compact-v32-floor");
  run<Store<Slab<Version16>, compact_retention::FloorRetention, DescriptorPhysical, CompactObjects>>("compact-v16-floor");
  run<Store<Chunks8, compact_retention::FloorRetention, DescriptorPhysical, CompactObjects>>("compact-v8-floor");
  run<Store<Slab<Version32>, compact_retention::DistinctRetention, DescriptorPhysical, CompactObjects>>("compact-v32-distinct");
  run<Store<Slab<Version16>, compact_retention::DistinctRetention, DescriptorPhysical, CompactObjects>>("compact-v16-distinct");
  run<Store<Chunks8, compact_retention::DistinctRetention, DescriptorPhysical, CompactObjects>>("compact-v8-distinct");
  run<Store<Slab<Version16>, compact_retention::FloorRetention, DescriptorPhysical, TipObjects>>("inline-v16-floor");
  run<Store<Slab<Version16>, compact_retention::DistinctRetention, DescriptorPhysical, TipObjects>>("inline-v16-distinct");
}
