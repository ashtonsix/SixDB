#include "../metadata/composed/catalog.h"
#include "../metadata/orbital/candidates/common.h"
#include <loom/worker.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <semaphore>
#include <source_location>
#include <thread>
#include <type_traits>

namespace {
namespace cs = compact_shape;
namespace v = orbital_bench::versioned;
namespace oa = orbital::allocation;
using Catalog = orbital_mvcc::composed::Catalog;
using orbital::Progress;
constexpr orbital::ObjectId a{17, 101}, b{19, 103}, born{23, 107};
constexpr orbital::ProgramId program{29, 109};
std::size_t checks{}, resumed{}, detached{}, completions{};

void check(bool value, const char* expression,
    std::source_location at = std::source_location::current()) noexcept {
  ++checks;
  if (value) return;
  std::fprintf(stderr, "%s:%u: %s\n", at.file_name(), at.line(), expression);
  std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)
template<class T> T take(orbital::Result<T> value) { CHECK(value); return std::move(*value); }

struct Worker {
  orbital::Ref<oa::Arena> arena{take(oa::open_arena(8 << 20))};
  loom::Worker worker;
  explicit Worker(std::size_t task_capacity) {
    CHECK(loom::adopt(worker)); CHECK(loom::configure(worker, arena, task_capacity));
  }
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;
  ~Worker() {
    CHECK(!worker.active_tasks);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto state = loom::shutdown(worker);
      CHECK(state.kind() != Progress::Kind::failed);
      if (state.kind() == Progress::Kind::done) break;
      CHECK(std::chrono::steady_clock::now() < deadline);
      loom::poll(worker, {.messages=64, .tasks=0}); std::this_thread::yield();
    }
    CHECK(!worker.memory && !worker.arena && !orbital::worker::pending(worker.lane));
    CHECK(arena->charged_bytes.load() == 0); loom::relinquish(worker);
  }
};

// Construct through the existing codec, then end Schema, descriptors and every
// writable alias. Only admitted Shape interpretation and Orbital body owners live.
Catalog::Owner construct(Worker& worker, compact_version_probe::Memory& memory,
    Catalog& store, std::uint32_t value) {
  v::Schema schema;
  const std::array<orbital_bench::Row,2> rows{{{11,{31,value,71}}, {31,{11,value+1,73}}}};
  auto shape = take(v::shape(schema, v::Format::rows, rows.size()));
  CHECK(shape.part_count == 1);
  auto allocation = take(loom::allocate(worker.worker, {.bytes=shape.parts[0].body_bytes,
      .alignment=shape.parts[0].alignment, .readable_padding=shape.parts[0].readable_padding}));
  cs::Shape* proof{};
  {
    const std::array<v::WritePart,1> writable{{{shape.parts[0].id,loom::bytes(allocation)}}};
    v::KernelCosts costs;
    CHECK(v::construct_into(schema, shape, writable, rows, costs));
    const std::array<compact_descriptor::PartSource,1> descriptions{{{shape.parts[0].id,
        loom::bytes(allocation),shape.parts[0].interpretation,shape.parts[0].readable_padding,
        shape.parts[0].alignment,{}}}};
    proof = take(cs::prove_fixture(memory,schema,program,shape.interpretation,descriptions));
  }
  const std::array<orbital::PartSource,1> input{{{shape.parts[0].id,loom::bytes(allocation),{},&allocation}}};
  auto result = store.capture({proof,rows.size(),orbital::PartOwners::independent_allocation},input);
  cs::release(proof);
  CHECK(result && !allocation); // Successful capture consumes the mutable owner.
  return std::move(*result);
}

void expect(cs::Selection selected, std::uint32_t value) {
  CHECK(selected && selected.records() == 2);
  const orbital::View view = take(selected.part(0));
  const auto* bytes = reinterpret_cast<const v::tp::byte*>(view.address());
  const auto& law = cs::tuple_law(selected.shape());
  // Independent source rows, including the non-leading row and all fields.
  CHECK(law.read_unchecked<0>(bytes) == 11 && law.read_unchecked<1>(bytes) == 31);
  CHECK(law.read_unchecked<2>(bytes) == value && law.read_unchecked<3>(bytes) == 71);
  bytes += v::tuple_bytes;
  CHECK(law.read_unchecked<0>(bytes) == 31 && law.read_unchecked<1>(bytes) == 11);
  CHECK(law.read_unchecked<2>(bytes) == value+1 && law.read_unchecked<3>(bytes) == 73);
}

void publish(Catalog& store, std::span<const Catalog::Change> changes) {
  Catalog::Prepared prepared(store);
  {
    Catalog::Scope basis(store);
    CHECK(store.prepare(basis,changes,prepared));
    CHECK(prepared.disposition() == orbital::PublicationState::accepted);
  }
  CHECK(store.install(prepared));
  CHECK(prepared.disposition() == orbital::PublicationState::installed);
  // Even this installed packet pins Core until the helper returns.
}

// All catalog/physical refcount operations run on the assigned owner. The foreign
// reader receives only immutable bytes and a copied law, and posts a retained
// record; its owner-Lane handler releases the escaping Part after actual use.
struct PhysicalUse {
  orbital::Part<cs::Control>* part;
  orbital::worker::Lane* lane;
  const v::tp::byte* bytes;
  cs::ReadLaw law;
  std::atomic<unsigned> references{1};
  orbital::worker::Message returned;
  bool handled{};
  PhysicalUse(cs::Export* owned, loom::Worker& worker) : part(owned),lane(&worker.lane),
      bytes(reinterpret_cast<const v::tp::byte*>(orbital::view(owned).address())),law(cs::tuple_law(owned)) {
    static const orbital::worker::MessageOps ops{
      [](auto& message) noexcept {
        auto& use = *static_cast<PhysicalUse*>(message.context);
        CHECK(use.references.fetch_add(1,std::memory_order_relaxed) == 1);
      },
      [](auto& message, auto& lane) noexcept {
        auto& use = *static_cast<PhysicalUse*>(message.context);
        CHECK(use.lane == &lane && orbital::worker::on_lane(lane));
        CHECK(use.part && !use.handled);
        orbital::release(std::exchange(use.part,nullptr));
        use.handled = true; ++completions;
      },
      [](auto& message) noexcept {
        auto& use = *static_cast<PhysicalUse*>(message.context);
        CHECK(use.references.fetch_sub(1,std::memory_order_acq_rel) == 2);
      }};
    returned.context = this; returned.ops = &ops;
  }
  PhysicalUse(const PhysicalUse&) = delete;
  PhysicalUse& operator=(const PhysicalUse&) = delete;
  ~PhysicalUse() { CHECK(!part && handled && references.load() == 1); }
};

struct Reader {
  Catalog* store;
  std::optional<Catalog::Scope> scope;
  std::optional<Catalog::BoundSelection> primary, alternative;
  orbital::Signal changed;
  loom::Task task;
  bool ready{}, observed{};
  unsigned visits{};
  const std::byte* alternative_address{};
  Reader(Catalog& catalog, loom::Worker& worker) : store(&catalog),scope(std::in_place,catalog) {
    changed.lane = &worker.lane; task.context = this;
    task.step = [](loom::Task& task) noexcept {
      auto& reader = *static_cast<Reader*>(task.context);
      ++reader.visits;
      const auto generation = reader.changed.generation;
      if (!reader.primary) {
        expect(reader.scope->bind(a),101);
        reader.primary.emplace(take(reader.store->bound(*reader.scope,a)));
        reader.alternative.emplace(take(reader.store->bound_alternative(*reader.scope,a)));
        expect(reader.alternative->selection(),101);
        reader.alternative_address = take(reader.alternative->selection().part(0)).address();
      }
      if (!reader.ready) return Progress::wait(reader.changed,generation);
      // First discovery of B and born is after publication and Store reset.
      expect(reader.scope->bind(b),201); CHECK(!reader.scope->bind(born));
      expect(reader.primary->selection(),101); expect(reader.alternative->selection(),101);
      reader.observed = true; ++resumed;
      return Progress::done();
    };
  }
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;
  ~Reader() { CHECK(!task.worker); }
};

void direct_ready() {
  Worker owner(0);
  compact_version_probe::Memory memory;
  {
    Catalog store(memory);
    {
      const std::array<Catalog::Change,1> changes{{{a,construct(owner,memory,store,101)}}};
      publish(store,changes);
    }
    Catalog::Scope scope(store); expect(scope.bind(a),101);
    const auto borrowed = store.borrow(); expect(borrowed.bind(a),101);
    loom::Task refused; refused.step = [](auto&) noexcept { return Progress::done(); };
    CHECK(!loom::start(owner.worker,refused));
    CHECK(refused.state == loom::TaskState::idle && !owner.worker.active_tasks);
  }
  CHECK(!memory.live);
}

void history(bool cancel) {
  Worker owner(1);
  compact_version_probe::Memory memory;
  Catalog store(memory);
  {
    const std::array<Catalog::Change,2> initial{{{a,construct(owner,memory,store,101)},
        {b,construct(owner,memory,store,201)}}};
    publish(store,initial);
  }
  std::optional<Reader> caller(std::in_place,store,owner.worker);
  auto& reader = *caller;
  CHECK(reader.scope->cut() == 1);
  {
    auto conversion = take(store.conversion(*reader.scope,a));
    auto replacement = construct(owner,memory,store,101);
    CHECK(store.adopt(conversion,replacement));
  }
  CHECK(loom::start(owner.worker,reader.task));
  CHECK(loom::poll(owner.worker,{.messages=0,.tasks=1}) == 1);
  CHECK(reader.task.state == loom::TaskState::waiting && reader.visits == 1);
  // A wake is not readiness; the same enrolled Scope parks again.
  orbital::pulse(reader.changed);
  CHECK(loom::poll(owner.worker,{.messages=0,.tasks=1}) == 1);
  CHECK(reader.task.state == loom::TaskState::waiting && reader.visits == 2);
  {
    auto conversion = take(store.conversion(*reader.scope,a));
    auto replacement = construct(owner,memory,store,101);
    CHECK(store.adopt(conversion,replacement));
    CHECK(take(reader.scope->bind_alternative(a)).realization != reader.alternative->selection().realization);
  }
  {
    const std::array<Catalog::Change,3> changes{{{a,construct(owner,memory,store,301)},
        {b,construct(owner,memory,store,401)}, {born,construct(owner,memory,store,501)}}};
    publish(store,changes);
  }
  {
    Catalog::Scope current(store); CHECK(current.cut() == 2);
    expect(current.bind(a),301); expect(current.bind(b),401); expect(current.bind(born),501);
  }
  store.drain(); // Old unread B and the captured replaced alternative must survive.
  store = {}; // The stationary reader's Scope pins this original incarnation.
  CHECK(!store.borrow().core);
  if (cancel) {
    loom::detach(reader.task);
    CHECK(reader.task.state == loom::TaskState::detached && !reader.changed.waiters);
    ++detached;
  } else {
    reader.ready = true; orbital::pulse(reader.changed);
    CHECK(loom::poll(owner.worker,{.messages=0,.tasks=1}) == 1);
    CHECK(reader.task.state == loom::TaskState::done && reader.observed && reader.visits == 3);
  }
  CHECK(!reader.task.worker && !owner.worker.active_tasks);
  // Both capabilities were sealed before Store reset. Exact primary association
  // and the replaced alternative can escape while their issuing Scope is live.
  auto* primary = take(reader.primary->export_part(0,orbital::PartRetention::selected_copy));
  auto* alternative = take(reader.alternative->export_part(0));
  CHECK(orbital::binding(primary).object == a && orbital::binding(primary).version == 1);
  CHECK(orbital::binding(alternative).object == a && orbital::binding(alternative).version == 1);
  CHECK(orbital::view(alternative).address() == reader.alternative_address);
  CHECK(orbital::view(primary).address() != orbital::view(alternative).address());
  PhysicalUse use(alternative,owner.worker);
  std::binary_semaphore proceed(0);
  // Base record ownership stays with this owner until the posting call joins and
  // the queued reference retires; there is no foreign Part retain/release.
  std::thread foreign([&] {
    proceed.acquire();
    if (use.law.read_unchecked<0>(use.bytes) != 11 || use.law.read_unchecked<2>(use.bytes) != 101 ||
        use.law.read_unchecked<2>(use.bytes+v::tuple_bytes) != 102) std::abort();
    orbital::worker::post(*use.lane,use.returned);
  });
  CHECK(reader.visits == (cancel ? 2U : 3U));
  caller.reset(); // Destroy Task, Signal and coherent Scope before physical use ends.
  CHECK(!owner.worker.first && !owner.worker.last && !owner.worker.active_tasks);
  // The primary selected-copy Part also survives its issuing Scope and caller.
  const auto* primary_bytes = reinterpret_cast<const v::tp::byte*>(orbital::view(primary).address());
  CHECK(cs::tuple_law(primary).read_unchecked<2>(primary_bytes) == 101);
  CHECK(cs::tuple_law(primary).read_unchecked<2>(primary_bytes+v::tuple_bytes) == 102);
  CHECK(orbital::binding(primary).object == a && orbital::binding(primary).version == 1);
  orbital::release(primary);
  const auto escaped_bytes = cs::footprint(alternative);
  CHECK(memory.live == escaped_bytes.requested_bytes + escaped_bytes.shared_shape_bytes);
  CHECK(cs::records(alternative) == 2);
  CHECK(loom::shutdown(owner.worker).kind() == Progress::Kind::waiting);
  CHECK(owner.worker.memory && memory.live && !use.handled);
  proceed.release(); foreign.join();
  CHECK(orbital::worker::pending(owner.worker.lane));
  CHECK(loom::poll(owner.worker,{.messages=64,.tasks=0}) >= 1);
  CHECK(use.handled && !memory.live);
}
} // namespace

int main() {
  static_assert(std::is_same_v<cs::Realization,orbital::Representation<cs::Control>>);
  static_assert(std::is_same_v<cs::Export,orbital::Part<cs::Control>>);
  static_assert(!std::is_move_constructible_v<Reader> && !std::is_move_constructible_v<Catalog::Scope>);
  direct_ready(); history(false); history(true);
  std::printf("{\"passed\":true,\"checks\":%zu,\"resumed\":%zu,\"detached\":%zu,\"owner_completions\":%zu,\"timing\":false}\n",
      checks,resumed,detached,completions);
}
