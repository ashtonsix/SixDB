#include "object_material.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <source_location>
#include <vector>

// Observe final history accounting without giving the fixture a private path
// for reads, publication, retention or reclamation.
namespace orbital::object_detail {
template<> struct Access<object_test::Physical> {
  static auto inspect(const object_test::Store& store) noexcept { return store.statistics(); }
};
}

namespace {
using namespace object_test;
using Inspect = orbital::object_detail::Access<Physical>;
constexpr orbital::ObjectId a{91, 0}, b{91, 1}, c{91, 2};

void check(bool condition, const char* expression,
    std::source_location where = std::source_location::current()) noexcept {
  if (condition) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), expression);
  std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)

template<class T> T take(orbital::Result<T> result) { CHECK(result); return std::move(*result); }

struct Memory final : std::pmr::memory_resource {
  std::size_t live{};
  ~Memory() { CHECK(!live); }
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes;
    return result;
  }
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
    CHECK(live >= bytes);
    live -= bytes;
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

struct Disposal {
  unsigned calls{};
  static void dispose(void* context) noexcept { ++static_cast<Disposal*>(context)->calls; }
};

Store::Owner capture(Store& store, std::uint64_t number, Disposal* disposed = nullptr,
    bool complemented = false) {
  return take(store.capture({number, complemented, disposed ? Disposal::dispose : nullptr, disposed}, {}));
}

void publish(Store& store, std::span<const Store::Change> changes, bool close_before_install = false) {
  Store::Prepared packet(store);
  if (close_before_install) {
    { Store::Scope basis(store); CHECK(store.prepare(basis, changes, packet)); }
    CHECK(store.install(packet));
  } else {
    Store::Scope basis(store);
    CHECK(store.prepare(basis, changes, packet));
    CHECK(store.install(packet));
  }
}

void update(Store& store, orbital::ObjectId id, std::uint64_t number,
    bool close_before_install = false) {
  const std::array<Store::Change, 1> change{{{id, capture(store, number)}}};
  publish(store, change, close_before_install);
}

void expect(Selection selected, std::uint64_t number) { CHECK(selected && selected.number() == number); }

std::size_t settle(Store& store, std::size_t budget = 7) {
  std::size_t work{}, calls{};
  for (;;) {
    const auto before = Inspect::inspect(store).reclamation_work;
    const auto done = store.drain(budget);
    CHECK(done <= budget && Inspect::inspect(store).reclamation_work == before + done);
    work += done;
    CHECK(++calls < 1000000);
    if (!done) break;
  }
  CHECK(!Inspect::inspect(store).maintenance_pending);
  return work;
}

// Settled versions protected by one old reader are storage debt, not fresh work
// on every unrelated publication. Compare public charged work, not nanoseconds
// or a particular planner's number of states.
std::size_t settled_cold_pairs(std::size_t objects, unsigned basis_lifetime) {
  Memory memory;
  std::vector<Disposal> original(objects);
  std::optional<Export> escaped;
  std::size_t hot_work{}, initial_late_park_release_work{};
  {
    Store store(memory);
    std::vector<Store::Change> changes;
    for (std::size_t i = 0; i < objects; ++i)
      changes.push_back({{91, i}, capture(store, 1000 + i, &original[i])});
    publish(store, changes, basis_lifetime == 1);
    changes.clear();
    std::optional<Store::Scope> old(std::in_place, store);
    for (std::size_t i = 0; i < objects; ++i)
      changes.push_back({{91, i}, capture(store, 2000 + i)});
    publish(store, changes, basis_lifetime == 1);
    changes.clear();
    if (basis_lifetime == 2) {
      // Cold tips predate this writer's cut. The ordered parking-time wakeup
      // is conservative: this first later-reader release can revisit them.
      // Keep that extra population pass visible and paid, separate from the
      // repeated steady-state hot-write work tested below.
      const std::array<Store::Change, 1> change{{{a, capture(store, 2999)}}};
      Store::Prepared packet(store);
      {
        Store::Scope basis(store);
        CHECK(store.prepare(basis, change, packet)); CHECK(store.install(packet));
        settle(store); // Initial population work is owed once, outside hot_work.
      }
      initial_late_park_release_work = settle(store);
    } else settle(store);
    CHECK(Inspect::inspect(store).versions == 2 * objects);

    for (unsigned n = 0; n < 64; ++n) {
      if (basis_lifetime == 2) {
        // Drain while the writer's predecessor cut still exists, then close
        // that cut and pay its release. Cold pairs do not depend on this cut.
        const std::array<Store::Change, 1> change{{{a, capture(store, 3000 + n)}}};
        Store::Prepared packet(store);
        {
          Store::Scope basis(store);
          CHECK(store.prepare(basis, change, packet)); CHECK(store.install(packet));
          hot_work += settle(store);
        }
      } else update(store, a, 3000 + n, basis_lifetime == 1);
      hot_work += settle(store);
      CHECK(Inspect::inspect(store).versions == 2 * objects);
    }
    // First discovery of these old objects happens after all maintenance.
    for (std::size_t i = 0; i < objects; ++i) {
      expect(old->bind({91, i}), 1000 + i);
      expect(store.borrow().bind({91, i}), i ? 2000 + i : 3063);
      CHECK(!original[i].calls);
    }

    {
      auto target = take(store.conversion(*old, a));
      auto alternate = capture(store, 1000, nullptr, true);
      CHECK(store.adopt(target, alternate));
      auto exact = take(store.bound_alternative(*old, a));
      escaped.emplace(take(exact.export_part(0)));
      expect(old->bind(a), 1000);
      expect(exact.selection(), 1000);
    }
    CHECK(Inspect::inspect(store).alternative_count == 1);
    old.reset();
    CHECK(settle(store) > 0); // Reader release and all resulting debt are paid.
    CHECK(Inspect::inspect(store).versions == objects);
    CHECK(!Inspect::inspect(store).alternative_count);
    for (const auto& disposed : original) CHECK(disposed.calls == 1);
    CHECK(escaped->number() == 1000);
  }
  CHECK(escaped->number() == 1000 && memory.live == sizeof(Value));
  escaped.reset();
  CHECK(!memory.live);
  constexpr std::array names{"close after install", "close before install", "drain before close"};
  std::printf("settled history: objects=%zu basis=%s initial_late_park_release_work=%zu hot_work=%zu\n",
      objects, names[basis_lifetime], initial_late_park_release_work, hot_work);
  return hot_work;
}

void settled_debt_does_not_rescan() {
  for (unsigned basis_lifetime : {0, 1, 2}) {
    const auto one = settled_cold_pairs(1, basis_lifetime);
    for (const std::size_t objects : {64u, 1024u}) {
      const auto many = settled_cold_pairs(objects, basis_lifetime);
      // Allow 32 additional metadata steps per hot update, independent of the
      // protected population. A full retained-population scan cannot fit this.
      if (many > one + 64 * 32) {
        std::fprintf(stderr, "settled cold debt rescanned: D=1 work=%zu, D=%zu work=%zu\n",
            one, objects, many);
        CHECK(false);
      }
    }
  }
}

void interrupted_reader_release() {
  // Interrupt at successive public one-step boundaries. No private state enum
  // selects a convenient interruption point for this history.
  for (unsigned pause = 0; pause <= 32; ++pause) {
    Memory memory;
    Disposal middle_a, middle_b;
    Store store(memory);
    {
      const std::array<Store::Change, 2> initial{{{a, capture(store, 10)}, {b, capture(store, 100)}}};
      publish(store, initial);
    }
    std::optional<Store::Scope> old(std::in_place, store);
    {
      const std::array<Store::Change, 2> second{{
          {a, capture(store, 20, &middle_a)}, {b, capture(store, 200, &middle_b)}}};
      publish(store, second);
    }
    std::optional<Store::Scope> middle(std::in_place, store);
    {
      const std::array<Store::Change, 2> third{{{a, capture(store, 30)}, {b, {}}}};
      publish(store, third);
    }
    std::optional<Store::Scope> deleted(std::in_place, store);
    {
      const std::array<Store::Change, 2> fourth{{{a, capture(store, 40)}, {b, capture(store, 400)}}};
      publish(store, fourth);
    }
    for (unsigned n = 0; n < pause; ++n) CHECK(store.drain(1) <= 1);
    if (pause == 32) settle(store, 1); // Also release a reader after a settled sweep.
    {
      const std::array<Store::Change, 2> fifth{{{a, capture(store, 50)}, {c, capture(store, 300)}}};
      publish(store, fifth, pause % 2);
    }
    middle.reset(); // A nonfloor cut disappears while the oldest reader remains.
    settle(store, 1);
    CHECK(middle_a.calls == 1 && middle_b.calls == 1);
    expect(old->bind(a), 10); expect(old->bind(b), 100); CHECK(!old->bind(c));
    expect(deleted->bind(a), 30); CHECK(!deleted->bind(b) && !deleted->bind(c));
    expect(store.borrow().bind(a), 50); expect(store.borrow().bind(b), 400);
    expect(store.borrow().bind(c), 300);
    deleted.reset(); settle(store, 1);
    CHECK(Inspect::inspect(store).versions == 5);
    old.reset(); settle(store, 1);
    CHECK(Inspect::inspect(store).versions == 3);
  }
}

void reader_releases_during_wake() {
  constexpr std::size_t objects = 64;
  for (bool earlier_first : {false, true}) {
    Memory memory;
    std::array<std::array<Disposal, objects>, 4> disposed;
    Store store(memory);
    std::array<std::optional<Store::Scope>, 4> readers;
    for (unsigned generation = 0; generation < 5; ++generation) {
      std::vector<Store::Change> changes;
      for (std::size_t i = 0; i < objects; ++i)
        changes.push_back({{91, i}, capture(store, 1000 * (generation + 1) + i,
            generation < 4 ? &disposed[generation][i] : nullptr)});
      publish(store, changes);
      if (generation < 4) readers[generation].emplace(store);
    }
    settle(store, 1);
    // First enqueue a wake, partially service it, then lower/raise the released
    // cut while that wake is incomplete. The oldest reader remains enrolled.
    readers[earlier_first ? 1 : 2].reset(); CHECK(store.drain(1) == 1);
    readers[earlier_first ? 2 : 1].reset(); CHECK(store.drain(1) == 1);
    readers[3].reset();
    settle(store, 1);
    CHECK(Inspect::inspect(store).versions == 2 * objects);
    for (std::size_t i = 0; i < objects; ++i) {
      expect(readers[0]->bind({91, i}), 1000 + i);
      expect(store.borrow().bind({91, i}), 5000 + i);
      CHECK(!disposed[0][i].calls);
      for (unsigned generation = 1; generation < 4; ++generation)
        CHECK(disposed[generation][i].calls == 1);
    }
    readers[0].reset(); settle(store, 1);
    CHECK(Inspect::inspect(store).versions == objects);
    for (const auto& original : disposed[0]) CHECK(original.calls == 1);
  }
}

void accepted_work_across_waiting_debt() {
  for (bool install : {false, true}) for (bool release_old : {false, true})
    for (unsigned pause : {0, 1, 3, 7, 31}) {
      Memory memory;
      Disposal original_a, original_b, candidate;
      Store store(memory);
      {
        const std::array<Store::Change, 2> initial{{
            {a, capture(store, 10, &original_a)}, {b, capture(store, 100, &original_b)}}};
        publish(store, initial);
      }
      std::optional<Store::Scope> old(std::in_place, store);
      {
        const std::array<Store::Change, 2> second{{{a, capture(store, 20)}, {b, capture(store, 200)}}};
        publish(store, second);
      }
      settle(store, 1);
      Store::Prepared packet(store);
      {
        Store::Scope basis(store);
        const std::array<Store::Change, 3> changes{{
            {a, {}}, {b, capture(store, 300, &candidate)}, {c, capture(store, 30)}}};
        CHECK(store.prepare(basis, changes, packet));
      }
      if (release_old) old.reset();
      for (unsigned n = 0; n < pause; ++n) CHECK(store.drain(1) <= 1);
      if (install) CHECK(store.install(packet)); else packet.abort();
      settle(store, 1);
      if (old) { expect(old->bind(a), 10); expect(old->bind(b), 100); CHECK(!old->bind(c)); }
      if (install) {
        CHECK(!store.borrow().bind(a));
        expect(store.borrow().bind(b), 300); expect(store.borrow().bind(c), 30);
        CHECK(!candidate.calls);
      } else {
        expect(store.borrow().bind(a), 20); expect(store.borrow().bind(b), 200);
        CHECK(!store.borrow().bind(c) && candidate.calls == 1);
      }
      old.reset(); settle(store, 1);
      CHECK(original_a.calls == 1 && original_b.calls == 1);
      CHECK(Inspect::inspect(store).versions == 2 && Inspect::inspect(store).objects == 2);
      // Reuse the erased/newly-aborted identity after slot cleanup, too.
      update(store, install ? a : c, 77); settle(store, 1);
      expect(store.borrow().bind(install ? a : c), 77);
      CHECK(Inspect::inspect(store).versions == 3);
    }
}

void resumed_tip_with_waiting_wake() {
  for (std::size_t budget : {2u, 7u}) for (unsigned pause = 0; pause < 32; ++pause) {
    Memory memory;
    Disposal middle_a, middle_b;
    Store store(memory);
    {
      const std::array<Store::Change, 2> initial{{{a, capture(store, 10)}, {b, capture(store, 100)}}};
      publish(store, initial);
    }
    std::optional<Store::Scope> old(std::in_place, store);
    {
      const std::array<Store::Change, 1> second{{{b, capture(store, 200, &middle_b)}}};
      publish(store, second);
    }
    std::optional<Store::Scope> closing(std::in_place, store);
    update(store, b, 300);
    settle(store, 1); // B now has settled history before A becomes active.
    {
      const std::array<Store::Change, 1> fourth{{{a, capture(store, 20, &middle_a)}}};
      publish(store, fourth);
    }
    std::optional<Store::Scope> middle(std::in_place, store);
    update(store, a, 30);
    for (unsigned n = 0; n < pause; ++n) CHECK(store.drain(1) <= 1);
    update(store, a, 40); // A's inline tip moves while a prior traversal may be paused.
    closing.reset(); // Wake B before the next call resumes A, using a budget > 1.
    settle(store, budget);
    CHECK(middle_b.calls == 1 && !middle_a.calls);
    expect(old->bind(a), 10); expect(old->bind(b), 100);
    expect(middle->bind(a), 20); expect(middle->bind(b), 300);
    expect(store.borrow().bind(a), 40); expect(store.borrow().bind(b), 300);
    middle.reset(); settle(store, budget);
    CHECK(middle_a.calls == 1 && Inspect::inspect(store).versions == 4);
    old.reset(); settle(store, budget);
    CHECK(Inspect::inspect(store).versions == 2);
  }
}

struct Reentry {
  Store* store{};
  std::optional<Store::Scope>* closing{};
  std::optional<Store::Scope>* opened{};
  unsigned calls{};
  static void dispose(void* context) noexcept {
    auto& self = *static_cast<Reentry*>(context);
    CHECK(++self.calls == 1);
    CHECK(self.store->drain(1) == 0);
    self.closing->reset();
    self.opened->emplace(*self.store);
    const std::array<Store::Change, 2> changes{{
        {a, capture(*self.store, 40)}, {b, capture(*self.store, 400)}}};
    publish(*self.store, changes);
  }
};

void disposal_can_change_readers_and_publish() {
  Memory memory;
  std::optional<Store::Scope> closing, opened;
  Store store(memory);
  Reentry context{&store, &closing, &opened};
  {
    const std::array<Store::Change, 2> initial{{
        {a, take(store.capture({10, false, Reentry::dispose, &context}, {}))},
        {b, capture(store, 100)}}};
    publish(store, initial);
  }
  update(store, a, 20);
  closing.emplace(store);
  {
    const std::array<Store::Change, 2> changes{{{a, capture(store, 30)}, {b, capture(store, 300)}}};
    publish(store, changes);
  }
  settle(store, 1);
  CHECK(context.calls == 1 && !closing && opened);
  expect(opened->bind(a), 30); expect(opened->bind(b), 300);
  expect(store.borrow().bind(a), 40); expect(store.borrow().bind(b), 400);
  opened.reset(); settle(store, 1);
  CHECK(Inspect::inspect(store).versions == 2);
}
} // namespace

int main() {
  settled_debt_does_not_rescan();
  interrupted_reader_release();
  reader_releases_during_wake();
  accepted_work_across_waiting_debt();
  resumed_tip_with_waiting_wake();
  disposal_can_change_readers_and_publish();
  std::puts("Object reclamation: settled debt, interrupted cuts and disposal reentry passed");
}
