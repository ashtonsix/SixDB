#include <loom/ring.h>
#include <orbital/object.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <source_location>
#include <thread>
#include <type_traits>
#include <vector>

namespace object_access_check {
namespace tp = ikea::tuplepack;
namespace obj = orbital::object;
using Word = std::uint64_t;
constexpr orbital::ObjectId first_object{0, 17}, second_object{0, 19};
constexpr std::array<std::size_t, 3> lengths{17, 23, 27};

void check(bool valid, const char* expression,
           std::source_location where = std::source_location::current()) {
  if (valid) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), expression);
  std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)
template<class T, class E> T required(std::expected<T, E> result) {
  CHECK(result); return std::move(*result);
}
template<class T> constexpr bool fixed_record =
    !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T> &&
    !std::is_move_constructible_v<T> && !std::is_move_assignable_v<T>;

struct Fixture {
  orbital::Ref<orbital::allocation::Arena> arena;
  loom::Worker worker;
  explicit Fixture(Word capacity = 4)
      : arena(required(orbital::allocation::open_arena(1 << 20))) {
    CHECK(loom::adopt(worker));
    CHECK(loom::configure(worker, arena, capacity));
  }
  ~Fixture() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto progress = loom::shutdown(worker);
      CHECK(progress.kind() != orbital::Progress::Kind::failed);
      if (progress.kind() == orbital::Progress::Kind::done) break;
      CHECK(std::chrono::steady_clock::now() < deadline);
      if (!loom::poll(worker, {128, 0})) std::this_thread::yield();
    }
    CHECK(!worker.memory && !worker.active_tasks && !orbital::worker::pending(worker.lane));
    CHECK(arena->charged_bytes.load() == 0);
    loom::relinquish(worker);
  }
};

// Authored opaque data/codec fixture only. Production Orbital must provide all
// version selection, interpretation binding, publication and access retention.
struct Metadata { Word first{}, rows{}, shift{}; };
void fill(orbital::MutableBytes bytes, Metadata shape, unsigned seed) {
  CHECK(shape.shift <= 1 && shape.rows && shape.rows <= 128 && bytes.size() == shape.rows * 8);
  for (Word row = 0; row < shape.rows; ++row)
    for (unsigned code = 0; code < 8; ++code)
      bytes[row * 8 + code] =
          std::byte(((shape.first + row) * 17 + code * 13 + seed) % 128 << shape.shift);
}
struct OwnedPart {
  orbital::allocation::Allocation storage;
  Metadata metadata;
  OwnedPart(loom::Worker& worker, unsigned seed, unsigned shift, Word first, Word rows)
      : storage(required(loom::allocate(worker, {.bytes = rows * 8}))),
        metadata{first, rows, shift} {
    fill(loom::bytes(storage), metadata, seed);
  }
  orbital::Bytes body() const {
    return {static_cast<const std::byte*>(storage.address), std::size_t(metadata.rows * 8)};
  }
  orbital::Bytes interpretation() const {
    return {reinterpret_cast<const std::byte*>(&metadata), sizeof(metadata)};
  }
};
struct Payload {
  // Each published Part has independent physical/interpretation ownership.
  // No production capture receives this aggregate as its backing owner.
  std::vector<std::shared_ptr<const OwnedPart>> parts;
  Payload(loom::Worker& worker, unsigned seed, unsigned shift,
          std::span<const std::size_t> lengths) : parts(lengths.size()) {
    CHECK(shift <= 1);
    Word first = 0;
    for (std::size_t p = 0; p < lengths.size(); ++p) {
      CHECK(lengths[p] && lengths[p] <= 128);
      parts[p] = std::make_shared<const OwnedPart>(worker, seed, shift, first, lengths[p]);
      first += lengths[p];
    }
  }
  Payload(const Payload&) = delete;
};

Metadata metadata(orbital::Bytes encoded) {
  CHECK(encoded.size() == sizeof(Metadata));
  Metadata value; std::memcpy(&value, encoded.data(), sizeof(value));
  CHECK(value.shift <= 1 && value.rows > 0 && value.rows <= 128);
  return value;
}
tp::layout format(unsigned shift) {
  std::array<tp::code, 8> codes;
  for (unsigned i = 0; i < codes.size(); ++i) codes[i] = {tp::byte(i), tp::byte(shift), 7};
  return required(tp::layout::make(8, codes));
}
tp::reader<8> plan(const tp::layout& layout) {
  std::array<tp::byte, 8> map;
  for (unsigned i = 0; i < map.size(); ++i) map[i] = i;
  return required(tp::reader<8>::make(layout, map));
}

// Stable named native plan/source closure. The containing operation supplies
// production ownership of these qualified bytes before construction/admission.
struct NativeBody {
  Metadata shape;
  tp::layout layout;
  tp::reader<8> prepared;
  tp::const_view source;
  tp::read_operation<8, const tp::byte> operation;
  tp::native_reader<8, const tp::byte> reader;
  NativeBody(orbital::Bytes bytes, orbital::Bytes interpretation)
      : shape(metadata(interpretation)), layout(format(shape.shift)), prepared(plan(layout)),
        source(required(tp::const_view::bind(layout,
            {reinterpret_cast<const tp::byte*>(bytes.data()), bytes.size()}, shape.rows, 8))),
        operation(required(tp::bind_reader(prepared, source))), reader(operation) {
    CHECK(reader.admit(shape.rows - 1));
  }
  NativeBody(const NativeBody&) = delete;
  NativeBody(NativeBody&&) = delete;
  NativeBody& operator=(const NativeBody&) = delete;
  NativeBody& operator=(NativeBody&&) = delete;
};
static_assert(fixed_record<NativeBody>);

Word expected(unsigned seed, Word row) {
  Word result = 0;
  for (unsigned code = 0; code < 8; ++code)
    result |= ((row * 17 + code * 13 + seed) & 127) << (code * 8);
  return result;
}

void fixture_control() {
  Fixture fixture(0);
  for (const auto shift : {0u, 1u}) {
    Payload payload(fixture.worker, 71, shift, lengths);
    for (const auto& part : payload.parts) {
      NativeBody body(part->body(), part->interpretation());
      for (Word row = 0; row < body.shape.rows; ++row)
        CHECK(body.reader.get_unchecked(row) == expected(71, body.shape.first + row));
    }
  }
  CHECK(!fixture.worker.active_tasks);
}

struct DecoderOwner { std::array<std::byte, 1> fields{std::byte{8}}; };
obj::Representation capture(obj::Store& store, const Payload& payload,
    orbital::ObjectId object, Word version, Word replica, unsigned seed,
    std::shared_ptr<const DecoderOwner> decoder = std::make_shared<const DecoderOwner>()) {
  std::vector<obj::PartSource> parts;
  for (std::size_t i = 0; i < payload.parts.size(); ++i) {
    const auto& part = payload.parts[i];
    parts.push_back({{0, i + 1}, part->body(), part->interpretation(), 0, 1, part});
  }
  const std::array logical{std::byte(seed)};
  return required(store.capture({.object = object, .version = version,
      .replica = {0, replica}, .format = {0, 710 + payload.parts.front()->metadata.shift},
      .logical_metadata = logical, .interpretation = decoder->fields, .dependencies = decoder}, parts));
}
struct Published {
  Word version{};
  std::array<std::weak_ptr<const OwnedPart>, 3> first, second;
};
Published publish(obj::Store& store, loom::Worker& worker, unsigned first_seed, unsigned second_seed) {
  auto draft = required(store.edit());
  Payload a(worker, first_seed, 1, lengths), b(worker, second_seed, 1, lengths);
  Published result{draft.version(), {}, {}};
  for (unsigned i = 0; i < 3; ++i) { result.first[i] = a.parts[i]; result.second[i] = b.parts[i]; }
  CHECK(draft.put(capture(store, a, first_object, draft.version(), 101, first_seed)));
  CHECK(draft.put(capture(store, b, second_object, draft.version(), 201, second_seed)));
  auto accepted = required(store.prepare(draft));
  CHECK(accepted.disposition() == obj::Disposition::accepted);
  CHECK(store.install(accepted));
  CHECK(accepted.disposition() == obj::Disposition::installed);
  accepted.retire();
  return result;
}
void erase_published(obj::Store& store) {
  auto draft = required(store.edit());
  CHECK(draft.erase(first_object)); CHECK(draft.erase(second_object));
  auto accepted = required(store.prepare(draft));
  CHECK(store.install(accepted)); accepted.retire();
}
void drain(obj::Store& store) {
  // Small finite histories. Account for cascading retirement independently of
  // completion of Tasks, Signals and native bodies.
  for (unsigned i = 0; i < 32; ++i) store.drain(256);
  CHECK(!store.statistics().retirement_records);
}
void description(obj::View view, unsigned seed, Word version) {
  const auto& desc = view.description();
  CHECK(desc.version == version && desc.logical_metadata.size() == 1 &&
        desc.logical_metadata[0] == std::byte(seed));
  CHECK(desc.interpretation.size() == 1 && desc.interpretation[0] == std::byte{8});
  CHECK(view.alignment() == 1 && view.readable_padding() == 0);
  const auto shape = metadata(view.interpretation());
  CHECK(desc.format == orbital::ProgramId{0, 710 + shape.shift});
}
Word read(obj::View view, unsigned seed, Word version) {
  description(view, seed, version);
  NativeBody body(view.body(), view.interpretation());
  Word sum = 0;
  for (Word row = 0; row < body.shape.rows; ++row) {
    const auto value = body.reader.get_unchecked(row);
    CHECK(value == expected(seed, body.shape.first + row)); sum += value;
  }
  return sum;
}

void direct_ready() {
  Fixture fixture(0);
  auto store = required(obj::Store::open(fixture.worker.lane, 1 << 20));
  const auto initial = publish(store, fixture.worker, 23, 41);
  {
    const auto context = store.borrow();
    const auto selected = required(obj::select(context, first_object));
    CHECK(selected.part_count() == 3);
    for (unsigned i = 0; i < 3; ++i) {
      const auto view = required(selected.part(std::size_t(i)));
      const auto owner = initial.first[i].lock(); CHECK(owner);
      CHECK(view.body().data() == owner->body().data()); // Capture never copies payload.
      read(view, 23, initial.version);
    }
    CHECK(!selected.part(std::size_t(3)) && !selected.part(orbital::PartId{0, 99}));
  }
  CHECK(!fixture.worker.active_tasks && !fixture.worker.first && !fixture.worker.last);
}

struct Compound {
  obj::ReadContext context;
  obj::Selection selected;
  obj::View early;
  std::unique_ptr<NativeBody> early_body;
  loom::Task task;
  orbital::Signal gate;
  std::array<Word, 67> result{};
  Word version{};
  unsigned calls{};
  bool opened{}, ready{}, completed{};
  Compound(loom::Worker& worker, obj::Store& store)
      : context(required(store.read())), selected(required(obj::select(context, first_object))),
        early(required(selected.part(std::size_t(0)))),
        early_body(std::make_unique<NativeBody>(early.body(), early.interpretation())),
        version(selected.description().version) {
    gate.lane = &worker.lane; gate.state = orbital::Readiness::pending;
    task.context = this;
    task.step = [](loom::Task& task) noexcept {
      auto& frame = *static_cast<Compound*>(task.context); ++frame.calls;
      if (!frame.opened) {
        for (Word row = 0; row < frame.early_body->shape.rows; ++row)
          frame.result[row] = frame.early_body->reader.get_unchecked(row);
        frame.opened = true;
      }
      if (!frame.ready) return orbital::Progress::wait(frame.gate);
      // These Parts and the second Object are first discovered after the wait.
      // Existing selection and retained coherent context supply their meaning.
      for (unsigned p = 1; p < 3; ++p) {
        const auto view = required(frame.selected.part(orbital::PartId{0, p + 1}));
        description(view, 23, frame.version);
        NativeBody body(view.body(), view.interpretation());
        for (Word row = 0; row < body.shape.rows; ++row)
          frame.result[body.shape.first + row] = body.reader.get_unchecked(row);
      }
      const auto other = required(obj::select(frame.context, second_object));
      CHECK(other.description().version == frame.version);
      read(required(other.part(std::size_t(2))), 41, frame.version);
      frame.completed = true;
      return orbital::Progress::done();
    };
  }
  // The Task callback is bound to this frame from construction onward.
  Compound(const Compound&) = delete;
  Compound(Compound&&) = delete;
  Compound& operator=(const Compound&) = delete;
  Compound& operator=(Compound&&) = delete;
};
static_assert(fixed_record<Compound>);

void coherent_wait() {
  Fixture fixture;
  auto store = required(obj::Store::open(fixture.worker.lane, 1 << 20));
  const auto initial = publish(store, fixture.worker, 23, 41);
  std::weak_ptr<const OwnedPart> converted_lifetime;
  {
    Compound frame(fixture.worker, store);
    CHECK(loom::start(fixture.worker, frame.task));
    CHECK(loom::poll(fixture.worker, {0, 1}) == 1 && frame.task.state == loom::TaskState::waiting);
    constexpr std::array<std::size_t, 1> joined{67};
    Payload converted(fixture.worker, 23, 0, joined);
    converted_lifetime = converted.parts[0];
    auto alternative = capture(store, converted, first_object, initial.version, 102, 23);
    const auto cut = store.statistics().completed_cut;
    CHECK(store.adopt(frame.selected, alternative));
    CHECK(store.statistics().completed_cut == cut && frame.selected.part_count() == 3);
    const auto another = required(obj::select(frame.context, first_object, true));
    CHECK(another.part_count() == 1 && another.description().replica == orbital::ReplicaId{0, 102});
    read(required(another.part(std::size_t(0))), 23, initial.version);
    const auto changed = publish(store, fixture.worker, 71, 87);
    CHECK(changed.version != initial.version);
    {
      const auto current = required(store.read());
      const auto latest = required(obj::select(current, first_object));
      CHECK(latest.part_count() == 3);
      CHECK(!obj::select(current, first_object, true));
      read(required(latest.part(std::size_t(1))), 71, changed.version);
      CHECK(!store.adopt(latest, alternative)); // Exact version, even after late conversion.
    }
    {
      Payload replacement(fixture.worker, 23, 1, lengths);
      CHECK(store.adopt(frame.selected,
          capture(store, replacement, first_object, initial.version, 103, 23)));
    }
    alternative = {}; converted.parts.clear();
    drain(store);
    // Context capture preserves the selected physical interpretation after its
    // catalogue alternative and every explicit fixture owner have been replaced.
    CHECK(!converted_lifetime.expired() && another.part_count() == 1);
    read(required(another.part(std::size_t(0))), 23, initial.version);
    const auto replacement = required(obj::select(frame.context, first_object, true));
    CHECK(replacement.part_count() == 3 &&
          replacement.description().replica == orbital::ReplicaId{0, 103});
    read(required(replacement.part(std::size_t(2))), 23, initial.version);
    for (const auto& owner : initial.first) CHECK(!owner.expired());
    for (const auto& owner : initial.second) CHECK(!owner.expired());
    orbital::pulse(frame.gate);
    CHECK(loom::poll(fixture.worker, {0, 1}) == 1 && frame.calls == 2 &&
          frame.task.state == loom::TaskState::waiting && !frame.completed);
    frame.ready = true; orbital::pulse(frame.gate);
    CHECK(loom::poll(fixture.worker, {0, 1}) == 1 && frame.completed &&
          frame.task.state == loom::TaskState::done && !frame.gate.waiters);
    for (Word row = 0; row < frame.result.size(); ++row) CHECK(frame.result[row] == expected(23, row));
    read(frame.early, 23, initial.version);
  }
  drain(store);
  CHECK(converted_lifetime.expired());
  for (const auto& owner : initial.first) CHECK(owner.expired());
  for (const auto& owner : initial.second) CHECK(owner.expired());
  CHECK(!fixture.worker.active_tasks);
}

void narrow_resolved_retention() {
  Fixture fixture;
  auto store = required(obj::Store::open(fixture.worker.lane, 1 << 20));
  auto draft = required(store.edit());
  Payload payload(fixture.worker, 23, 1, lengths), other(fixture.worker, 41, 1, lengths);
  auto decoder = std::make_shared<const DecoderOwner>();
  const std::weak_ptr<const DecoderOwner> decoder_lifetime = decoder;
  const std::weak_ptr<const OwnedPart> kept = payload.parts[0], unrelated = payload.parts[1],
      other_object = other.parts[0];
  const auto version = draft.version();
  CHECK(draft.put(capture(store, payload, first_object, version, 101, 23, decoder)));
  CHECK(draft.put(capture(store, other, second_object, version, 201, 41)));
  auto accepted = required(store.prepare(draft)); CHECK(store.install(accepted)); accepted.retire();
  obj::RetainedView escaped;
  obj::RetainedSelection selected_owner;
  {
    const auto context = required(store.read());
    const auto selected = required(obj::select(context, first_object));
    selected_owner = obj::retain(selected);
    escaped = obj::retain(required(selected.part(std::size_t(0))));
  }
  decoder.reset(); payload.parts.clear(); other.parts.clear();
  publish(store, fixture.worker, 71, 87); drain(store);
  CHECK(!kept.expired() && !unrelated.expired() && other_object.expired());
  // Exact descriptor retention permits later Part discovery without retaining
  // the broader coherent context or the unrelated old Object.
  read(required(selected_owner.selection().part(std::size_t(2))), 23, version);
  selected_owner = {}; drain(store);
  CHECK(!kept.expired() && unrelated.expired());
  CHECK(!decoder_lifetime.expired());
  read(escaped.view(), 23, version);
  auto moved = std::move(escaped); CHECK(!escaped && moved);
  read(moved.view(), 23, version);
  moved = {}; drain(store);
  CHECK(kept.expired() && decoder_lifetime.expired());
}

struct PhysicalUse {
  obj::RetainedView access;
  std::unique_ptr<NativeBody> body;
  orbital::Signal done;
  orbital::worker::Message completion;
  std::atomic<unsigned> references{1};
  Word result{};
  unsigned handled{};
  static const orbital::worker::MessageOps ops;
  PhysicalUse(loom::Worker& worker, obj::View view)
      : access(obj::retain(view)), body(std::make_unique<NativeBody>(view.body(), view.interpretation())) {
    done.lane = &worker.lane; done.state = orbital::Readiness::pending;
    completion.context = this; completion.ops = &ops;
  }
  PhysicalUse(const PhysicalUse&) = delete;
  PhysicalUse(PhysicalUse&&) = delete;
  PhysicalUse& operator=(const PhysicalUse&) = delete;
  PhysicalUse& operator=(PhysicalUse&&) = delete;
};
static_assert(fixed_record<PhysicalUse>);
const orbital::worker::MessageOps PhysicalUse::ops{
    [](orbital::worker::Message& message) noexcept {
      auto& use = *static_cast<PhysicalUse*>(message.context);
      CHECK(use.references.fetch_add(1, std::memory_order_relaxed) == 1);
    },
    [](orbital::worker::Message& message, orbital::worker::Lane& lane) noexcept {
      auto& use = *static_cast<PhysicalUse*>(message.context);
      CHECK(use.done.lane == &lane && orbital::worker::on_lane(lane));
      ++use.handled;
      CHECK(use.result == expected(23, use.body->shape.first));
      use.body.reset(); use.access = {};
      orbital::complete(use.done, {});
    },
    [](orbital::worker::Message& message) noexcept {
      auto& use = *static_cast<PhysicalUse*>(message.context);
      CHECK(use.references.fetch_sub(1, std::memory_order_acq_rel) == 2);
    }};

void detach_and_physical_owner() {
  Fixture fixture;
  auto store = required(obj::Store::open(fixture.worker.lane, 1 << 20));
  const auto initial = publish(store, fixture.worker, 23, 41);
  std::unique_ptr<PhysicalUse> physical;
  {
    const auto context = required(store.read());
    const auto selected = required(obj::select(context, first_object));
    physical = std::make_unique<PhysicalUse>(fixture.worker, required(selected.part(std::size_t(0))));
  }
  struct Caller {
    loom::Task task;
    PhysicalUse* physical{};
    explicit Caller(PhysicalUse& use) : physical(&use) {
      task.context = this;
      task.step = [](loom::Task& task) noexcept {
        return orbital::Progress::wait(static_cast<Caller*>(task.context)->physical->done);
      };
    }
    Caller(const Caller&) = delete;
    Caller(Caller&&) = delete;
    Caller& operator=(const Caller&) = delete;
    Caller& operator=(Caller&&) = delete;
  };
  static_assert(fixed_record<Caller>);
  auto caller = std::make_unique<Caller>(*physical);
  CHECK(loom::start(fixture.worker, caller->task));
  CHECK(loom::poll(fixture.worker, {0, 1}) == 1 && caller->task.state == loom::TaskState::waiting);
  loom::detach(caller->task); caller.reset();
  CHECK(!fixture.worker.active_tasks && !physical->done.waiters && physical->access);
  erase_published(store); drain(store);
  CHECK(!initial.first[0].expired() && initial.first[1].expired() && initial.second[0].expired());
  CHECK(loom::shutdown(fixture.worker).kind() == orbital::Progress::Kind::waiting);
  // Real immutable native use, not a synthetic Port request. Production handles
  // stay on their owning Lane; the physical record protects ordinary foreign
  // byte use. Its actual completion Message is independent of the destroyed Task.
  std::thread foreign([&] {
    physical->result = physical->body->reader.get_unchecked(0);
    orbital::worker::post(fixture.worker.lane, physical->completion);
  });
  foreign.join(); // Every post/wake callback has returned before record destruction.
  CHECK(loom::poll(fixture.worker, {16, 0}) >= 1 && physical->handled == 1);
  CHECK(physical->references == 1 && !physical->access && !physical->done.waiters);
  drain(store); CHECK(initial.first[0].expired());
  CHECK(!fixture.worker.active_tasks && !fixture.worker.first && !fixture.worker.last);
}

constexpr Metadata fill_shape{0, 17, 1};
constexpr unsigned fill_seed = 53;
obj::Builder build_part(obj::Store& store, loom::Worker& worker, Word version, unsigned seed) {
  const std::array logical{std::byte(seed)}, interpretation{std::byte{8}};
  // Independent windows make live mapped bytes meaningful for each backing,
  // rather than counting a finite page shared by several small allocations.
  const std::array<obj::PartPlan, 1> parts{{{{0, 1},
      {.bytes = fill_shape.rows * 8, .reserved_bytes = 4096, .alignment = 1,
       .initialization = orbital::allocation::Initialization::zero},
      {reinterpret_cast<const std::byte*>(&fill_shape), sizeof(fill_shape)}}}};
  return required(store.build({.object = first_object, .version = version,
      .replica = {0, 301}, .format = {0, 711}, .logical_metadata = logical,
      .interpretation = interpretation, .dependencies = {}}, *worker.memory, parts));
}
Word publish_built(obj::Store& store, loom::Worker& worker, unsigned seed) {
  auto draft = required(store.edit());
  const auto version = draft.version();
  auto builder = build_part(store, worker, version, seed);
  {
    auto writer = required(builder.write(0));
    fill(required(writer.bytes()), fill_shape, seed);
  }
  CHECK(draft.put(required(builder.freeze())));
  auto accepted = required(store.prepare(draft));
  CHECK(store.install(accepted)); accepted.retire();
  return version;
}
std::uintptr_t current_body(obj::Store& store, unsigned seed, Word version) {
  const auto context = store.borrow();
  const auto selected = required(obj::select(context, first_object));
  CHECK(selected.part_count() == 1);
  const auto view = required(selected.part(std::size_t(0)));
  read(view, seed, version);
  return reinterpret_cast<std::uintptr_t>(view.body().data());
}
Word live_payload(const loom::Worker& worker) {
  const auto stats = orbital::allocation::statistics(*worker.memory);
  CHECK(!stats.preparing_bytes && !stats.uncertain_bytes && stats.mapped_bytes >= stats.idle_bytes);
  return stats.mapped_bytes - stats.idle_bytes;
}

enum class FillResult { pending, discarded, stale, installed };
struct DelayedFill {
  obj::Store* store;
  obj::Draft draft;
  obj::Builder builder;
  obj::WriteView writer;
  orbital::MutableBytes target;
  orbital::Signal done;
  orbital::worker::Message completion;
  std::atomic<unsigned> references{1};
  bool interested{true};
  unsigned handled{};
  FillResult result{FillResult::pending};
  static const orbital::worker::MessageOps ops;
  DelayedFill(loom::Worker& worker, obj::Store& owner)
      : store(&owner), draft(required(owner.edit())),
        builder(build_part(owner, worker, draft.version(), fill_seed)),
        writer(required(builder.write(0))), target(required(writer.bytes())) {
    done.lane = &worker.lane; done.state = orbital::Readiness::pending;
    completion.context = this; completion.ops = &ops;
  }
  DelayedFill(const DelayedFill&) = delete;
  DelayedFill(DelayedFill&&) = delete;
  DelayedFill& operator=(const DelayedFill&) = delete;
  DelayedFill& operator=(DelayedFill&&) = delete;
};
static_assert(fixed_record<DelayedFill>);
const orbital::worker::MessageOps DelayedFill::ops{
    [](orbital::worker::Message& message) noexcept {
      auto& use = *static_cast<DelayedFill*>(message.context);
      CHECK(use.references.fetch_add(1, std::memory_order_relaxed) == 1);
    },
    [](orbital::worker::Message& message, orbital::worker::Lane& lane) noexcept {
      auto& use = *static_cast<DelayedFill*>(message.context);
      CHECK(use.done.lane == &lane && orbital::worker::on_lane(lane) && !use.handled++);
      // Physical raw-span use has ended. Only now may the owner close/revoke
      // private write access, independently of scheduling interest.
      use.target = {}; use.writer = {};
      if (!use.interested) {
        use.builder = {}; use.draft = {}; use.result = FillResult::discarded;
      } else {
        auto frozen = required(use.builder.freeze());
        read(required(frozen.part(std::size_t(0))), fill_seed, use.draft.version());
        CHECK(use.draft.put(std::move(frozen)));
        auto prepared = use.store->prepare(use.draft);
        if (prepared) {
          CHECK(use.store->install(*prepared)); prepared->retire();
          use.result = FillResult::installed;
        } else {
          CHECK(prepared.error() == orbital::Error::stale);
          // Refusal preserved the filled private result but granted no authority.
          read(required(required(use.draft.select(first_object)).part(std::size_t(0))),
              fill_seed, use.draft.version());
          use.draft = {}; use.result = FillResult::stale;
        }
      }
      orbital::complete(use.done, {});
    },
    [](orbital::worker::Message& message) noexcept {
      auto& use = *static_cast<DelayedFill*>(message.context);
      CHECK(use.references.fetch_sub(1, std::memory_order_acq_rel) == 2);
    }};

void delayed_fill_completion() {
  struct Case { bool cancel, replace; FillResult result; };
  constexpr std::array cases{
      Case{false, false, FillResult::installed}, Case{true, false, FillResult::discarded},
      Case{false, true, FillResult::stale}, Case{true, true, FillResult::discarded}};
  for (const auto history : cases) {
    Fixture fixture;
    auto store = required(obj::Store::open(fixture.worker.lane, 1 << 20));
    const auto initial_version = publish_built(store, fixture.worker, 23);
    drain(store);
    const auto one_backing = live_payload(fixture.worker); CHECK(one_backing > 0);
    auto physical = std::make_unique<DelayedFill>(fixture.worker, store);
    const auto pending_version = physical->draft.version();
    const auto old_address = reinterpret_cast<std::uintptr_t>(physical->target.data());
    drain(store); CHECK(live_payload(fixture.worker) == 2 * one_backing);
    struct Caller {
      loom::Task task;
      DelayedFill* physical;
      unsigned calls{};
      FillResult observed{FillResult::pending};
      explicit Caller(DelayedFill& use) : physical(&use) {
        task.context = this;
        task.step = [](loom::Task& task) noexcept {
          auto& frame = *static_cast<Caller*>(task.context); ++frame.calls;
          if (!frame.physical->handled) return orbital::Progress::wait(frame.physical->done);
          frame.observed = frame.physical->result;
          return orbital::Progress::done();
        };
      }
      Caller(const Caller&) = delete;
      Caller(Caller&&) = delete;
      Caller& operator=(const Caller&) = delete;
      Caller& operator=(Caller&&) = delete;
    };
    static_assert(fixed_record<Caller>);
    auto caller = std::make_unique<Caller>(*physical);
    CHECK(loom::start(fixture.worker, caller->task));
    CHECK(loom::poll(fixture.worker, {0, 1}) == 1 && caller->task.state == loom::TaskState::waiting);
    std::atomic<bool> entered{}, release{};
    std::thread foreign([target = physical->target, completion = &physical->completion,
                         lane = &fixture.worker.lane, &entered, &release] {
      entered.store(true, std::memory_order_release); entered.notify_one();
      release.wait(false, std::memory_order_acquire);
      fill(target, fill_shape, fill_seed);
      orbital::worker::post(*lane, *completion);
    });
    entered.wait(false, std::memory_order_acquire);
    if (history.cancel) {
      loom::detach(caller->task); caller.reset(); physical->interested = false;
      CHECK(!fixture.worker.active_tasks && !physical->done.waiters);
    }
    const auto current_version = history.replace
        ? publish_built(store, fixture.worker, 71) : initial_version;
    const auto current_seed = history.replace ? 71u : 23u;
    if (history.replace) CHECK(current_version == pending_version);
    else CHECK(store.statistics().completed_cut + 1 == pending_version);
    drain(store);
    CHECK(live_payload(fixture.worker) == (history.replace ? 3 : 2) * one_backing);
    const auto before = store.statistics();
    const auto replacement_address = current_body(store, current_seed, current_version);
    CHECK(old_address + physical->target.size() <= replacement_address ||
          replacement_address + fill_shape.rows * 8 <= old_address);
    CHECK(reinterpret_cast<std::uintptr_t>(required(physical->writer.bytes()).data()) == old_address);
    const auto unfinished = physical->builder.freeze();
    CHECK(!unfinished && unfinished.error() == orbital::Error::conflict);
    release.store(true, std::memory_order_release); release.notify_one();
    foreign.join(); // Raw use and the actual post/wake finish before receipt.
    {
      NativeBody filled(physical->target,
          {reinterpret_cast<const std::byte*>(&fill_shape), sizeof(fill_shape)});
      for (Word row = 0; row < fill_shape.rows; ++row)
        CHECK(filled.reader.get_unchecked(row) == expected(fill_seed, row));
    }
    CHECK(current_body(store, current_seed, current_version) == replacement_address);
    CHECK(loom::poll(fixture.worker, {16, history.cancel ? 0u : 1u}) >= 1);
    CHECK(physical->handled == 1 && physical->references == 1 && physical->result == history.result &&
          physical->target.empty() && !physical->writer.bytes() && !physical->done.waiters);
    if (caller) CHECK(caller->task.state == loom::TaskState::done && caller->calls == 2 &&
                      caller->observed == history.result);
    const bool installed = history.result == FillResult::installed;
    const auto after = store.statistics();
    CHECK(after.completed_cut == before.completed_cut + installed &&
          after.accepted == before.accepted + installed &&
          after.installed == before.installed + installed && after.aborted == before.aborted);
    CHECK(current_body(store, installed ? fill_seed : current_seed,
          installed ? pending_version : current_version) == (installed ? old_address : replacement_address));
    caller.reset(); physical.reset(); drain(store);
    CHECK(live_payload(fixture.worker) == one_backing && !store.statistics().contexts);
    auto erase = required(store.edit()); CHECK(erase.erase(first_object));
    auto accepted = required(store.prepare(erase)); CHECK(store.install(accepted)); accepted.retire();
    drain(store); CHECK(live_payload(fixture.worker) == 0);
    store = {};
  }
}

struct RingFrame {
  obj::RetainedView access;
  std::unique_ptr<NativeBody> body;
  loom::DemandClass demand;
  Word owner{}, steps{}, sum{};
  bool completed{};
};
using Ring = loom::Ring<RingFrame*, Word, Word, Word>;
LOOM_NATIVE_CC void native(Ring* ring, Ring::Slot* slot, RingFrame* frame,
                          Word row, Word remaining, Word sum) {
  sum += frame->body->reader.get_unchecked(row);
  if (++row == frame->body->shape.rows) row = 0;
  if (!--remaining) {
    frame->sum = sum; frame->completed = true; ring->complete(*slot);
  } else ring->prefetch(frame->demand, frame->body->source.row_unchecked(row));
  [[clang::musttail]] return Ring::yield(ring, slot, frame, row, remaining, sum);
}
void ring_retained_frames() {
  Fixture fixture;
  auto store = required(obj::Store::open(fixture.worker.lane, 1 << 20));
  const auto initial = publish(store, fixture.worker, 23, 41);
  Ring ring; CHECK(ring.configure(fixture.worker, {2, 4}));
  std::array<RingFrame, 6> frames;
  constexpr std::array<Word, 6> steps{11, 39, 13, 7, 17, 3};
  {
    const auto context = required(store.read());
    for (unsigned i = 0; i < frames.size(); ++i) {
      auto& frame = frames[i]; frame.owner = i & 1; frame.steps = steps[i];
      const auto selected = required(obj::select(context, frame.owner ? first_object : second_object));
      const auto view = required(selected.part(std::size_t(i / 2)));
      frame.access = obj::retain(view);
      frame.body = std::make_unique<NativeBody>(view.body(), view.interpretation());
      frame.demand = required(loom::bind_demand({loom::MemoryPattern::irregular, 64}, 8));
      CHECK(ring.push(native, {&frame, 0, frame.steps, 0},
          loom::MemoryDependency{frame.body->source.row_unchecked(0), frame.demand}));
    }
  }
  CHECK(ring.spin({3}) == 3 && !ring.close());
  publish(store, fixture.worker, 71, 87); drain(store);
  CHECK(ring.release_if([](const Ring::Arguments& args) noexcept {
    return std::get<0>(args)->owner == 0;
  }) == 3);
  for (unsigned i = 0; i < frames.size(); i += 2) {
    frames[i].body.reset(); frames[i].access = {};
  }
  drain(store);
  for (const auto& owner : initial.second) CHECK(owner.expired());
  while (ring.pending()) CHECK(ring.spin({2}) > 0);
  for (unsigned i = 0; i < frames.size(); ++i) {
    const auto& frame = frames[i];
    if (!(i & 1)) { CHECK(!frame.completed); continue; }
    description(frame.access.view(), 23, initial.version);
    Word oracle = 0;
    for (Word step = 0; step < frame.steps; ++step)
      oracle += expected(23, frame.body->shape.first + step % frame.body->shape.rows);
    CHECK(frame.completed && frame.sum == oracle);
  }
  CHECK(ring.counters().visits == 51 && ring.counters().completed == 3 &&
        ring.counters().abandoned == 3 && ring.close());
  for (auto& frame : frames) { frame.body.reset(); frame.access = {}; }
  drain(store);
  for (const auto& owner : initial.first) CHECK(owner.expired());
}
} // namespace object_access_check

int main() {
  using namespace object_access_check;
  fixture_control(); direct_ready(); coherent_wait(); narrow_resolved_retention();
  detach_and_physical_owner(); delayed_fill_completion(); ring_retained_frames();
  std::puts("loom_object_access_check: passed");
}
