#include <orbital/mvcc_baseline.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <span>
#include <thread>
#include <vector>

using namespace orbital;
namespace obj = orbital::object;

namespace {
constexpr ObjectId a{0, 1}, b{0, 2}, missing{0, 99};
constexpr ProgramId columns{0, 1}, rows{0, 2};

template<class T>
T required(Result<T> result) {
  if (!result) {
    std::cerr << "unexpected Orbital error " << static_cast<int>(result.error()) << '\n';
    std::abort();
  }
  return std::move(*result);
}

void required(Result<void> result) {
  if (!result) {
    std::cerr << "unexpected Orbital error " << static_cast<int>(result.error()) << '\n';
    std::abort();
  }
}

template<class T>
void refused(const Result<T>& result, Error error) {
  assert(!result && result.error() == error);
}

struct Runtime {
  worker::Lane lane;
  Ref<allocation::Arena> arena;
  allocation::Pool* pool{};
  Runtime() {
    required(worker::adopt(lane));
    arena = required(allocation::open_arena(16 * 1024 * 1024));
    pool = required(allocation::add_pool(*arena, lane));
  }
  ~Runtime() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (allocation::close(*pool).kind() != Progress::Kind::done) {
      assert(std::chrono::steady_clock::now() < deadline);
      worker::poll(lane, 1024);
      std::this_thread::yield();
    }
    assert(arena->charged_bytes.load() == 0);
    required(allocation::remove_pool(*pool));
    arena = {};
    worker::relinquish(lane);
  }
};

void drain(obj::Store& store) {
  for (unsigned pass = 0; pass < 64; ++pass)
    if (store.drain(4096) == 0) break;
  assert(store.statistics().retirement_records == 0);
}

struct Body {
  std::vector<std::uint64_t> values;
  int* destroyed{};
  Body(std::span<const std::uint64_t> input, int* counter)
      : values(input.begin(), input.end()), destroyed(counter) {}
  ~Body() { if (destroyed) ++*destroyed; }
};

struct Dependency {
  int* destroyed;
  ~Dependency() { ++*destroyed; }
};

obj::Representation representation(obj::Store& store, ObjectId id,
    std::uint64_t version, std::span<const std::uint64_t> values,
    bool row = false, std::span<int> destroyed = {},
    std::shared_ptr<const void> dependencies = {}, std::uint64_t logical = 77) {
  std::array<std::uint64_t, 1> metadata{logical};
  std::array<std::byte, 1> encoding{row ? std::byte{2} : std::byte{1}};
  std::vector<obj::PartSource> parts;
  auto add = [&](std::span<const std::uint64_t> input, unsigned index) {
    auto owner = std::make_shared<const Body>(input, destroyed.empty() ? nullptr : &destroyed[index]);
    parts.push_back({{0, index}, std::as_bytes(std::span(owner->values)), encoding,
        0, alignof(std::uint64_t), std::move(owner)});
  };
  if (row) add(values, 0);
  else for (unsigned index = 0; index < values.size(); ++index) add(values.subspan(index, 1), index);
  return required(store.capture({id, version, {0, 1}, row ? rows : columns,
      std::as_bytes(std::span(metadata)), encoding, std::move(dependencies)}, parts));
}

void put(obj::Store& store, obj::Draft& draft, ObjectId id,
    std::initializer_list<std::uint64_t> values) {
  required(draft.put(representation(store, id, draft.version(), values)));
}

void publish(obj::Store& store, obj::Draft& draft) {
  auto prepared = required(store.prepare(draft));
  assert(prepared.disposition() == obj::Disposition::accepted);
  required(store.install(prepared));
  assert(prepared.disposition() == obj::Disposition::installed);
  prepared.retire();
}

std::uint64_t word(obj::View view, std::size_t index = 0) {
  assert(view.body().size() >= (index + 1) * sizeof(std::uint64_t));
  std::uint64_t value;
  std::memcpy(&value, view.body().data() + index * sizeof(value), sizeof(value));
  return value;
}

std::uint64_t word(obj::Selection selection, std::size_t index) {
  return word(required(selection.part(index)));
}

void expect(obj::Selection selection, std::initializer_list<std::uint64_t> values) {
  assert(selection.part_count() == values.size());
  unsigned index = 0;
  for (auto value : values) assert(word(selection, index++) == value);
}

void initialize(obj::Store& store) {
  auto draft = required(store.edit());
  put(store, draft, a, {10, 20});
  put(store, draft, b, {100, 200});
  publish(store, draft);
}

void coherent_reads_and_private_results() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 8 * 1024 * 1024));
  initialize(store);
  {
    auto before = required(store.read());
    const auto selected = required(obj::select(before, a));
    assert(word(selected, 0) == 10);
    auto draft = required(store.edit());
    put(store, draft, a, {30, 40});
    auto private_a = required(draft.select(a));
    put(store, draft, b, {word(private_a, 0) * 10, word(private_a, 1) * 10});
    expect(required(draft.select(b)), {300, 400});
    publish(store, draft);
    // No old View is open: the context still protects future discovery.
    expect(required(obj::select(before, a)), {10, 20});
    expect(required(obj::select(before, b)), {100, 200});
    auto current = store.borrow();
    expect(required(obj::select(current, a)), {30, 40});
    expect(required(obj::select(current, b)), {300, 400});
    refused(obj::select(before, missing), Error::absent);
    auto added = required(store.edit());
    put(store, added, missing, {900});
    publish(store, added);
    refused(obj::select(before, missing), Error::absent);
  }
  drain(store);
}

void physical_adoption_and_logical_authority() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 8 * 1024 * 1024));
  initialize(store);
  int converted_destroyed{};
  {
    auto before = required(store.read());
    auto selected = required(obj::select(before, a));
    auto draft = required(store.edit());
    put(store, draft, a, {30, 40});
    auto prepared = required(store.prepare(draft));
    const std::array old_values{std::uint64_t{10}, std::uint64_t{20}};
    auto converted = representation(store, a, selected.description().version, old_values,
        true, std::span(&converted_destroyed, 1));
    required(store.adopt(selected, std::move(converted)));
    expect(selected, {10, 20});
    auto alternative = required(obj::select(before, a, true));
    assert(alternative.part_count() == 1 && alternative.description().format == rows);
    auto body = required(alternative.part(0));
    assert(word(body) == 10 && word(body, 1) == 20);
    assert(body.interpretation().size() == 1 && body.interpretation()[0] == std::byte{2});
    required(store.install(prepared));
    prepared.retire();
    auto late = representation(store, a, selected.description().version, old_values, true);
    required(store.adopt(selected, std::move(late)));
    drain(store);
    assert(converted_destroyed == 0);
    auto current = store.borrow();
    expect(required(obj::select(current, a)), {30, 40});
    expect(required(obj::select(before, b)), {100, 200});
    auto wrong_metadata = representation(store, a, selected.description().version,
        old_values, true, {}, {}, 88);
    assert(!store.adopt(selected, std::move(wrong_metadata)));
    auto wrong_version = representation(store, a, selected.description().version + 100, old_values, true);
    assert(!store.adopt(selected, std::move(wrong_version)));
    expect(selected, {10, 20});
    assert(word(body, 1) == 20);
  }
  drain(store);
  assert(converted_destroyed == 1);
}

void rejected_and_aborted_episodes() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 8 * 1024 * 1024));
  auto foreign = required(obj::Store::open(runtime.lane, 8 * 1024 * 1024));
  initialize(store);
  initialize(foreign);
  {
    auto stale = required(store.edit());
    put(store, stale, a, {50, 60});
    auto changed = required(store.edit());
    put(store, changed, a, {30, 40});
    publish(store, changed);
    refused(store.prepare(stale), Error::stale);
    expect(required(stale.select(a)), {50, 60});
    auto draft = required(store.edit());
    const std::array values{std::uint64_t{70}, std::uint64_t{80}};
    auto other_rep = representation(foreign, a, draft.version(), values);
    assert(!draft.put(std::move(other_rep)));
    auto wrong_version = representation(store, a, draft.version() + 1, values);
    assert(!draft.put(std::move(wrong_version)));
    expect(required(draft.select(a)), {30, 40});
    put(store, draft, a, {70, 80});
    assert(!foreign.prepare(draft));
    auto prepared = required(store.prepare(draft));
    auto moved = std::move(prepared);
    assert(prepared.disposition() == obj::Disposition::empty);
    assert(moved.disposition() == obj::Disposition::accepted);
    assert(!foreign.install(moved));
    assert(!store.install(prepared));
    required(store.install(moved));
    assert(!store.install(moved));
    moved.retire();
    auto borrow = store.borrow();
    expect(required(obj::select(borrow, a)), {70, 80});
  }
  {
    auto draft = required(store.edit());
    put(store, draft, a, {90, 100});
    auto prepared = required(store.prepare(draft));
    prepared.abort();
    assert(prepared.disposition() == obj::Disposition::aborted);
    assert(!store.install(prepared));
    prepared.abort();
    auto borrow = store.borrow();
    expect(required(obj::select(borrow, a)), {70, 80});
  }
  const auto aborted = store.statistics().aborted;
  {
    auto draft = required(store.edit());
    put(store, draft, a, {110, 120});
    auto prepared = required(store.prepare(draft));
    assert(prepared.disposition() == obj::Disposition::accepted);
  }
  assert(store.statistics().aborted == aborted + 1);
  {
    auto draft = required(store.edit());
    put(store, draft, b, {500, 600});
    publish(store, draft);
    auto borrow = store.borrow();
    expect(required(obj::select(borrow, a)), {70, 80});
    expect(required(obj::select(borrow, b)), {500, 600});
    obj::Prepared empty;
    assert(empty.disposition() == obj::Disposition::empty && !store.install(empty));
  }
  {
    auto first = required(store.edit());
    put(store, first, a, {900, 1000});
    auto accepted = required(store.prepare(first));
    auto second = required(store.edit());
    put(store, second, b, {700, 800});
    refused(store.prepare(second), Error::unavailable);
    expect(required(second.select(b)), {700, 800});
    accepted.abort();
    publish(store, second);
    auto current = store.borrow();
    expect(required(obj::select(current, a)), {70, 80});
    expect(required(obj::select(current, b)), {700, 800});
  }
  drain(store);
  drain(foreign);
}

void narrow_retention() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 8 * 1024 * 1024));
  std::array<int, 4> destroyed{};
  int dependency_destroyed{};
  obj::RetainedView held;
  {
    auto draft = required(store.edit());
    const std::array av{std::uint64_t{10}, std::uint64_t{20}};
    const std::array bv{std::uint64_t{100}, std::uint64_t{200}};
    auto dependency = std::shared_ptr<const void>(new Dependency{&dependency_destroyed});
    required(draft.put(representation(store, a, draft.version(), av, false,
        std::span(destroyed).first(2), std::move(dependency))));
    required(draft.put(representation(store, b, draft.version(), bv, false,
        std::span(destroyed).last(2))));
    publish(store, draft);
    auto context = required(store.read());
    auto selection = required(obj::select(context, a));
    held = obj::retain(required(selection.part(0)));
    auto removed = required(store.edit());
    required(removed.erase(a));
    required(removed.erase(b));
    publish(store, removed);
    auto current = store.borrow();
    refused(obj::select(current, a), Error::absent);
    refused(obj::select(current, b), Error::absent);
    drain(store);
    assert((destroyed == std::array<int, 4>{0, 0, 0, 0}));
    expect(required(obj::select(context, b)), {100, 200});
  }
  drain(store);
  assert((destroyed == std::array<int, 4>{0, 1, 1, 1}));
  assert(dependency_destroyed == 0);
  assert(word(held.view()) == 10);
  assert(held.view().description().format == columns);
  auto moved = std::move(held);
  assert(!held && word(moved.view()) == 10);
  moved = {};
  drain(store);
  assert((destroyed == std::array<int, 4>{1, 1, 1, 1}));
  assert(dependency_destroyed == 1);
  int escaped_destroyed{};
  obj::ReadContext escaped_context;
  {
    auto temporary = required(obj::Store::open(runtime.lane, 32 * 1024));
    auto draft = required(temporary.edit());
    const std::array values{std::uint64_t{900}};
    required(draft.put(representation(temporary, a, draft.version(), values,
        false, std::span(&escaped_destroyed, 1))));
    publish(temporary, draft);
    escaped_context = required(temporary.read());
    held = obj::retain(required(required(obj::select(escaped_context, a)).part(0)));
  }
  assert(escaped_destroyed == 0);
  expect(required(obj::select(escaped_context, a)), {900});
  escaped_context = {};
  assert(escaped_destroyed == 0 && word(held.view()) == 900);
  held = {};
  assert(escaped_destroyed == 1);
}

void allocation_rollback_and_writer_lifetime() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 8 * 1024 * 1024));
  initialize(store);
  {
    auto draft = required(store.edit());
    const std::array<std::uint64_t, 1> metadata{77};
    const obj::RepresentationSpec spec{a, draft.version(), {0, 1}, columns,
        std::as_bytes(std::span(metadata)), {}, {}};
    const std::array<obj::PartPlan, 2> plans{{
        {{0, 0}, {8, 0, 8, 16, allocation::Initialization::zero}, {}},
        {{0, 1}, {8, 0, 8, 16, allocation::Initialization::zero}, {}}}};
    auto builder = required(store.build(spec, *runtime.pool, plans));
    auto writer = required(builder.write(0));
    const auto forty = std::uint64_t{40};
    std::memcpy(required(writer.bytes()).data(), &forty, sizeof(forty));
    assert(!builder.freeze());
    assert(!builder.write(0));
    auto moved_writer = std::move(writer);
    assert(!writer.bytes());
    moved_writer = {};
    {
      auto second = required(builder.write(1));
      const auto eighty = std::uint64_t{80};
      std::memcpy(required(second.bytes()).data(), &eighty, sizeof(eighty));
    }
    auto frozen = required(builder.freeze());
    assert(!builder.write(0));
    required(draft.put(std::move(frozen)));
    expect(required(draft.select(a)), {40, 80});
    // First B Part can allocate; the second exceeds the shared Arena budget.
    const obj::RepresentationSpec bspec{b, draft.version(), {0, 1}, columns,
        std::as_bytes(std::span(metadata)), {}, {}};
    auto denied_plans = plans;
    denied_plans[1].allocation.bytes = 32 * 1024 * 1024;
    refused(store.build(bspec, *runtime.pool, denied_plans), Error::capacity);
    auto current = store.borrow();
    expect(required(obj::select(current, a)), {10, 20});
    expect(required(obj::select(current, b)), {100, 200});
  }
  drain(store);
  {
    auto draft = required(store.edit());
    const obj::RepresentationSpec spec{a, draft.version(), {}, columns, {}, {}, {}};
    const std::array<obj::PartPlan, 1> plans{{
        {{0, 0}, {8, 0, 8, 0, allocation::Initialization::zero}, {}}}};
    auto builder = required(store.build(spec, *runtime.pool, plans));
    auto writer = required(builder.write(0));
    auto* original = required(writer.bytes()).data();
    original[0] = std::byte{0x54};
    builder = {};
    assert(!writer.bytes());
    drain(store);
    auto other = required(allocation::allocate(*runtime.pool,
        {8, 0, 8, 0, allocation::Initialization::zero}));
    assert(other.address != original && original[0] == std::byte{0x54});
    other = {};
    writer = {};
  }
  drain(store);
}

void capacity_refusal_preserves_private_work() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 32 * 1024));
  initialize(store);
  {
    auto draft = required(store.edit());
    put(store, draft, a, {30, 40});
    std::vector<obj::ReadContext> occupied;
    bool exhausted{};
    for (unsigned count = 0; count < 4096; ++count) {
      auto context = store.read();
      if (!context) {
        refused(context, Error::capacity);
        exhausted = true;
        break;
      }
      occupied.push_back(std::move(*context));
    }
    assert(exhausted);
    refused(store.prepare(draft), Error::capacity);
    expect(required(draft.select(a)), {30, 40});
    auto borrowed = store.borrow();
    expect(required(obj::select(borrowed, a)), {10, 20});
    occupied.clear();
    drain(store);
    publish(store, draft);
  }
  auto current = store.borrow();
  expect(required(obj::select(current, a)), {30, 40});
  drain(store);
}

void retained_history_budget_and_recovery() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 24 * 1024));
  {
    auto initial = required(store.edit());
    put(store, initial, a, {10, 20});
    publish(store, initial);
  }
  {
    auto oldest = required(store.read());
    bool exhausted{};
    std::uint64_t last = 10;
    for (unsigned iteration = 0; iteration < 4096; ++iteration) {
      auto result = store.edit();
      if (!result) {
        refused(result, Error::capacity);
        exhausted = true;
        break;
      }
      auto draft = std::move(*result);
      const std::array values{std::uint64_t{30 + iteration}, std::uint64_t{40 + iteration}};
      const std::array<std::uint64_t, 1> metadata{77};
      const std::array<std::byte, 1> encoding{std::byte{1}};
      auto first = std::make_shared<const Body>(std::span(values).first(1), nullptr);
      auto second = std::make_shared<const Body>(std::span(values).last(1), nullptr);
      const std::array<obj::PartSource, 2> parts{{
          {{0, 0}, std::as_bytes(std::span(first->values)), encoding, 0, 8, first},
          {{0, 1}, std::as_bytes(std::span(second->values)), encoding, 0, 8, second}}};
      auto rep = store.capture({a, draft.version(), {}, columns,
          std::as_bytes(std::span(metadata)), encoding, {}}, parts);
      if (!rep) {
        refused(rep, Error::capacity);
        exhausted = true;
        break;
      }
      auto staged = draft.put(std::move(*rep));
      if (!staged) {
        refused(staged, Error::capacity);
        exhausted = true;
        break;
      }
      auto prepared = store.prepare(draft);
      if (!prepared) {
        refused(prepared, Error::capacity);
        exhausted = true;
        break;
      }
      required(store.install(*prepared));
      prepared->retire();
      last = values[0];
      drain(store);
    }
    assert(exhausted && last != 10);
    expect(required(obj::select(oldest, a)), {10, 20});
    auto current = store.borrow();
    assert(word(required(obj::select(current, a)), 0) == last);
    assert(store.statistics().metadata_bytes <= store.statistics().metadata_limit);
  }
  drain(store);
  {
    auto recovered = required(store.edit());
    put(store, recovered, a, {700, 800});
    publish(store, recovered);
    auto current = store.borrow();
    expect(required(obj::select(current, a)), {700, 800});
  }
  drain(store);
}

void deleted_id_reclamation_and_old_absence() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 32 * 1024));
  {
    auto empty = required(store.read());
    auto removed = required(store.edit());
    required(removed.erase(missing));
    publish(store, removed);
    drain(store);
    refused(obj::select(empty, missing), Error::absent);
  }
  drain(store);
  assert(store.statistics().objects == 0 && store.statistics().versions == 0);
  for (unsigned round = 0; round < 256; ++round) {
    const ObjectId id{91, round};
    auto created = required(store.edit());
    put(store, created, id, {100 + round});
    publish(store, created);
    auto before = required(store.read());
    auto removed = required(store.edit());
    required(removed.erase(id));
    publish(store, removed);
    auto absent = required(store.read());
    refused(obj::select(absent, id), Error::absent);
    // These contexts have not resolved a View; they still retain both meanings.
    auto recreated = required(store.edit());
    put(store, recreated, id, {900 + round});
    publish(store, recreated);
    expect(required(obj::select(before, id)), {100 + round});
    refused(obj::select(absent, id), Error::absent);
    before = {};
    absent = {};
    auto final_delete = required(store.edit());
    required(final_delete.erase(id));
    publish(store, final_delete);
    drain(store);
    assert(store.statistics().objects == 0 && store.statistics().versions == 0);
    assert(store.statistics().metadata_bytes <= store.statistics().metadata_limit);
  }
}

void sparse_update_maintenance() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 8 * 1024 * 1024));
  constexpr unsigned count = 256;
  {
    auto draft = required(store.edit());
    for (unsigned index = 0; index < count; ++index)
      put(store, draft, {42, index}, {1000 + index});
    publish(store, draft);
  }
  drain(store);
  assert(store.statistics().objects == count && store.statistics().versions == count);
  {
    auto draft = required(store.edit());
    put(store, draft, {42, 123}, {9999});
    publish(store, draft);
  }
  std::size_t examined{};
  bool complete{};
  for (unsigned pass = 0; pass < 64; ++pass) {
    const auto work = store.drain(8);
    assert(work <= 8);
    examined += work;
    if (!work) { complete = true; break; }
  }
  assert(complete && examined < 50);
  assert(store.statistics().objects == count && store.statistics().versions == count);
  auto current = store.borrow();
  expect(required(obj::select(current, {42, 123})), {9999});
  expect(required(obj::select(current, {42, 124})), {1124});
  {
    auto oldest = required(store.read());
    const auto floor = store.statistics().oldest_cut;
    auto changed = required(store.edit());
    put(store, changed, {42, 123}, {8888});
    publish(store, changed);
    drain(store);
    {
      auto transient = required(store.read());
      expect(required(obj::select(transient, {42, 123})), {8888});
    }
    assert(store.statistics().oldest_cut == floor);
    assert(store.drain(4096) == 0);
    expect(required(obj::select(oldest, {42, 123})), {9999});
  }
  drain(store);
  assert(store.statistics().versions == count);
}

void many_parts_and_identity_lookup() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 8 * 1024 * 1024));
  constexpr unsigned count = 33;
  {
    auto draft = required(store.edit());
    std::vector<obj::PartSource> sources;
    std::vector<obj::PartPlan> plans;
    for (unsigned index = 0; index < count; ++index) {
      const std::array value{std::uint64_t{1000 + index}};
      auto owner = std::make_shared<const Body>(value, nullptr);
      const PartId id{17, index * 7 % count};
      sources.push_back({id, std::as_bytes(std::span(owner->values)), {}, 0, 8, std::move(owner)});
      plans.push_back({id, {8, 0, 8, 16, allocation::Initialization::zero}, {}});
    }
    const obj::RepresentationSpec spec{a, draft.version(), {}, columns, {}, {}, {}};
    auto duplicated = sources;
    duplicated.back().id = duplicated.front().id;
    refused(store.capture(spec, duplicated), Error::invalid_argument);
    required(draft.put(required(store.capture(spec, sources))));
    auto captured = required(draft.select(a));
    assert(captured.part_count() == count);
    for (unsigned index = 0; index < count; ++index) {
      assert(word(captured, index) == 1000 + index);
      assert(word(required(captured.part(PartId{17, index * 7 % count}))) == 1000 + index);
    }
    auto bad_plans = plans;
    bad_plans.back().id = bad_plans.front().id;
    auto bspec = spec;
    bspec.object = b;
    refused(store.build(bspec, *runtime.pool, bad_plans), Error::invalid_argument);
    auto builder = required(store.build(bspec, *runtime.pool, plans));
    for (unsigned index = 0; index < count; ++index) {
      auto writer = required(builder.write(index));
      const auto value = std::uint64_t{2000 + index};
      std::memcpy(required(writer.bytes()).data(), &value, sizeof(value));
    }
    required(draft.put(required(builder.freeze())));
    publish(store, draft);
    auto current = store.borrow();
    auto built = required(obj::select(current, b));
    for (unsigned index = 0; index < count; ++index) {
      auto view = required(built.part(PartId{17, index * 7 % count}));
      assert(view.id() == PartId(17, index * 7 % count));
      assert(word(view) == 2000 + index && word(built, index) == 2000 + index);
      assert(view.readable_padding() == 16 && view.alignment() == 8);
      for (unsigned tail = 8; tail < 24; ++tail) assert(view.body().data()[tail] == std::byte{0});
    }
    refused(built.part(PartId{17, 1000}), Error::absent);
    refused(built.part(count), Error::absent);
  }
  drain(store);
}

void write_owners_outlive_store() {
  Runtime runtime;
  const std::array value{std::uint64_t{50}};
  int private_destroyed{}, accepted_destroyed{};
  obj::Draft private_work;
  {
    auto store = required(obj::Store::open(runtime.lane, 32 * 1024));
    private_work = required(store.edit());
    required(private_work.put(representation(store, a, private_work.version(), value,
        false, std::span(&private_destroyed, 1))));
  }
  assert(private_destroyed == 0);
  expect(required(private_work.select(a)), {50});
  private_work = {};
  assert(private_destroyed == 1);
  obj::Prepared accepted;
  {
    auto store = required(obj::Store::open(runtime.lane, 32 * 1024));
    auto draft = required(store.edit());
    required(draft.put(representation(store, a, draft.version(), value,
        false, std::span(&accepted_destroyed, 1))));
    accepted = required(store.prepare(draft));
  }
  assert(accepted.disposition() == obj::Disposition::accepted && accepted_destroyed == 0);
  accepted = {};
  assert(accepted_destroyed == 1);
}

void accepted_recreation_pins_deleted_entry() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 32 * 1024));
  {
    auto created = required(store.edit());
    put(store, created, a, {50});
    publish(store, created);
    auto removed = required(store.edit());
    required(removed.erase(a));
    publish(store, removed);
    auto recreated = required(store.edit());
    put(store, recreated, a, {60});
    auto accepted = required(store.prepare(recreated));
    drain(store);
    required(store.install(accepted));
    accepted.retire();
    auto current = store.borrow();
    expect(required(obj::select(current, a)), {60});
  }
  drain(store);
  assert(store.statistics().objects == 1 && store.statistics().versions == 1);
  {
    auto removed = required(store.edit());
    required(removed.erase(a));
    publish(store, removed);
    auto recreated = required(store.edit());
    put(store, recreated, a, {70});
    auto accepted = required(store.prepare(recreated));
    drain(store);
    accepted.abort();
    accepted.retire();
  }
  drain(store);
  assert(store.statistics().objects == 0 && store.statistics().versions == 0);
}

void empty_part_builds() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 32 * 1024));
  for (unsigned count : {1u, 4u}) {
    auto draft = required(store.edit());
    std::vector<obj::PartPlan> plans;
    for (unsigned index = 0; index < count; ++index)
      plans.push_back({{0, index}, {0, 0, 8, 0, allocation::Initialization::zero}, {}});
    auto builder = required(store.build({a, draft.version(), {}, columns, {}, {}, {}},
        *runtime.pool, plans));
    for (unsigned index = 0; index < count; ++index) {
      auto writer = required(builder.write(index));
      assert(required(writer.bytes()).empty());
    }
    required(draft.put(required(builder.freeze())));
    publish(store, draft);
    auto current = store.borrow();
    auto selected = required(obj::select(current, a));
    assert(selected.part_count() == count);
    for (unsigned index = 0; index < count; ++index) {
      auto view = required(selected.part(index));
      assert(view.body().empty() && view.readable_padding() == 0);
    }
  }
  {
    auto draft = required(store.edit());
    const std::array<obj::PartPlan, 1> padded{{
        {{0, 0}, {0, 0, 8, 16, allocation::Initialization::zero}, {}}}};
    auto builder = required(store.build({a, draft.version(), {}, columns, {}, {}, {}},
        *runtime.pool, padded));
    {
      auto writer = required(builder.write(0));
      assert(required(writer.bytes()).empty());
    }
    required(draft.put(required(builder.freeze())));
    publish(store, draft);
    auto current = store.borrow();
    auto view = required(required(obj::select(current, a)).part(0));
    assert(view.body().empty() && view.body().data() && view.readable_padding() == 16);
    for (unsigned index = 0; index < 16; ++index) assert(view.body().data()[index] == std::byte{0});
  }
  drain(store);
}

void retained_selection_releases_unselected_formats() {
  Runtime runtime;
  auto store = required(obj::Store::open(runtime.lane, 32 * 1024));
  std::array<int, 4> destroyed{};
  obj::RetainedSelection held;
  const std::array values{std::uint64_t{10}, std::uint64_t{20}};
  {
    auto created = required(store.edit());
    required(created.put(representation(store, a, created.version(), values, false,
        std::span(destroyed).first(2))));
    publish(store, created);
    auto context = required(store.read());
    auto initial = required(obj::select(context, a));
    required(store.adopt(initial, representation(store, a, initial.description().version,
        values, true, std::span(destroyed).subspan(2, 1))));
    held = obj::retain(required(obj::select(context, a, true)));
    required(store.adopt(initial, representation(store, a, initial.description().version,
        values, true, std::span(destroyed).last(1))));
  }
  {
    auto changed = required(store.edit());
    put(store, changed, a, {30, 40});
    publish(store, changed);
  }
  drain(store);
  assert((destroyed == std::array<int, 4>{1, 1, 0, 1}));
  assert(held.selection().part_count() == 1);
  assert(word(required(held.selection().part(0)), 1) == 20);
  {
    auto candidate = representation(store, a, held.selection().description().version, values, true);
    assert(!store.adopt(held.selection(), candidate));
    assert(word(required(candidate.part(0)), 1) == 20);
    auto current = store.borrow();
    expect(required(obj::select(current, a)), {30, 40});
  }
  held = {};
  drain(store);
  assert((destroyed == std::array<int, 4>{1, 1, 1, 1}));
}

struct CallbackBody {
  std::uint64_t value{42};
  std::function<void()> callback;
  explicit CallbackBody(std::function<void()> action) : callback(std::move(action)) {}
  ~CallbackBody() { callback(); }
};

void queue_callback(obj::Store& store, std::function<void()> action) {
  auto owner = std::make_shared<const CallbackBody>(std::move(action));
  const std::array<obj::PartSource, 1> parts{{
      {{0, 0}, std::as_bytes(std::span(&owner->value, 1)), {}, 0, 8, owner}}};
  auto unpublished = required(store.capture({missing, 0, {}, columns, {}, {}, {}}, parts));
}

void drain_owner_callbacks() {
  Runtime runtime;
  {
    auto store = required(obj::Store::open(runtime.lane, 32 * 1024));
    int called{}, published_destroyed{};
    auto draft = required(store.edit());
    const std::array value{std::uint64_t{100}};
    required(draft.put(representation(store, a, draft.version(), value, false,
        std::span(&published_destroyed, 1))));
    publish(store, draft);
    drain(store);
    queue_callback(store, [&] {
      ++called;
      store = {};
      assert(published_destroyed == 0); // The active drain still owns the catalog.
    });
    assert(store.drain(0) == 0 && called == 0);
    assert(store.drain(4096) > 0);
    assert(!store && called == 1 && published_destroyed == 1);
  }
  {
    auto store = required(obj::Store::open(runtime.lane, 32 * 1024));
    initialize(store);
    auto before = required(store.read());
    int called{};
    queue_callback(store, [&] {
      ++called;
      auto draft = required(store.edit());
      put(store, draft, a, {70, 80});
      publish(store, draft);
      drain(store); // Safely nests while the calling Part is already unlinked.
      auto current = store.borrow();
      expect(required(obj::select(current, a)), {70, 80});
    });
    drain(store);
    assert(called == 1);
    expect(required(obj::select(before, a)), {10, 20});
    auto current = store.borrow();
    expect(required(obj::select(current, a)), {70, 80});
    before = {};
    drain(store);
    assert(store.statistics().versions == 2);
  }
}
} // namespace

int main() {
  coherent_reads_and_private_results();
  physical_adoption_and_logical_authority();
  rejected_and_aborted_episodes();
  narrow_retention();
  allocation_rollback_and_writer_lifetime();
  capacity_refusal_preserves_private_work();
  retained_history_budget_and_recovery();
  deleted_id_reclamation_and_old_absence();
  sparse_update_maintenance();
  many_parts_and_identity_lookup();
  write_owners_outlive_store();
  accepted_recreation_pins_deleted_entry();
  empty_part_builds();
  retained_selection_releases_unselected_formats();
  drain_owner_callbacks();
  std::cout << "object: fifteen coherent lifetime/publication histories passed\n";
}
