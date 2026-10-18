#include <orbital/instantiate/representation.h>
#include <allocation/internal.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <memory_resource>
#include <source_location>
#include <thread>

namespace {
using namespace orbital;
namespace alloc = orbital::allocation;
namespace ad = orbital::allocation::detail;
namespace od = orbital::object_detail;

void check(bool condition, const char* reason,
    std::source_location where = std::source_location::current()) {
  if (condition) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), reason);
  std::abort();
}

template<class T> T take(Result<T> result,
    std::source_location where = std::source_location::current()) {
  check(result.has_value(), "unexpected operation refusal", where);
  return std::move(*result);
}

template<class T> void refused(const Result<T>& result, Error error, const char* reason) {
  check(!result && result.error() == error, reason);
}

struct Memory final : std::pmr::memory_resource {
  std::size_t live{}, blocks{}, limit{SIZE_MAX};
  void (*hook)(void*) noexcept{};
  void* context{};
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (const auto action = std::exchange(hook, nullptr)) action(context);
    if (bytes > limit || live > limit - bytes) throw std::bad_alloc{};
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes; ++blocks;
    return result;
  }
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
    check(blocks && live >= bytes, "metadata released twice");
    --blocks; live -= bytes;
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

struct Counters { unsigned retains{}, releases{}, selected_copies{}, selected_drops{}; };
struct Field {
  PartId id{};
  std::size_t bytes{};
  std::size_t padding{};
  std::size_t alignment{1};
};
struct Proof {
  Memory* memory{};
  const Field* fields{};
  std::size_t count{};
  unsigned uses{};
  Counters* counters{};
  bool selected{};
  void (*retain_hook)(void*) noexcept{};
  void* context{};
};
struct SelectedProof { Proof proof; Field field; };
struct Geometry {
  std::array<Field, 32> fields{};
  Proof proof;
  Geometry(Memory& memory, Counters& counters, std::initializer_list<Field> input)
      : proof{&memory, fields.data(), input.size(), 0, &counters} {
    check(input.size() <= fields.size(), "too many fixture fields");
    std::copy(input.begin(), input.end(), fields.begin());
  }
  Geometry(const Geometry&) = delete;
  Geometry& operator=(const Geometry&) = delete;
};

template<unsigned Tag> struct Control {
  Proof* proof{};
  bool valid(std::pmr::memory_resource& memory) const noexcept { return proof && proof->memory == &memory; }
  std::pmr::memory_resource* resource() const noexcept { return proof->memory; }
  std::size_t part_count() const noexcept { return proof->count; }
  PartId id(std::size_t index) const noexcept { return proof->fields[index].id; }
  Result<std::size_t> extent(std::size_t index) const noexcept { return proof->fields[index].bytes; }
  std::size_t padding(std::size_t index) const noexcept { return proof->fields[index].padding; }
  std::size_t alignment(std::size_t index) const noexcept { return proof->fields[index].alignment; }
  void retain() const noexcept {
    ++proof->uses; ++proof->counters->retains;
    if (const auto action = std::exchange(proof->retain_hook, nullptr)) action(proof->context);
  }
  void release() const noexcept {
    check(proof->uses != 0, "proof lifetime released twice");
    --proof->uses; ++proof->counters->releases;
  }
  std::size_t selected_bytes(std::size_t) const noexcept { return sizeof(SelectedProof); }
  Control copy_selected(std::byte* destination, std::size_t index) const noexcept {
    auto* copy = std::construct_at(reinterpret_cast<SelectedProof*>(destination));
    copy->field = proof->fields[index];
    copy->proof = {proof->memory, &copy->field, 1, 0, proof->counters, true};
    ++proof->counters->selected_copies;
    return {&copy->proof};
  }
  std::size_t metadata_bytes() const noexcept { return sizeof(SelectedProof); }
  void destroy_selected() const noexcept {
    check(proof->selected, "selected proof disposal used for shared proof");
    ++proof->counters->selected_drops;
    std::destroy_at(reinterpret_cast<SelectedProof*>(proof));
  }
};
using OldControl = Control<17>;
using NewControl = Control<93>;
static_assert(std::is_trivially_copyable_v<OldControl> && std::is_trivially_copyable_v<NewControl>);
static_assert(sizeof(Representation<OldControl>) == 16 && sizeof(Part<OldControl>) == 72 && sizeof(View) == 8);

struct Fixture {
  worker::Lane lane;
  Ref<alloc::Arena> arena;
  alloc::Pool* pool{};
  Fixture() {
    check(worker::adopt(lane).has_value(), "adopt owner Lane");
    arena = take(alloc::open_arena(32 * 1024 * 1024));
    pool = take(alloc::add_pool(*arena, lane));
  }
  alloc::Allocation create(std::size_t bytes = 128, std::size_t padding = 0) {
    return take(alloc::allocate(*pool, {.bytes = bytes, .alignment = 64,
        .readable_padding = padding, .initialization = alloc::Initialization::zero}));
  }
  ~Fixture() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto result = alloc::close(*pool);
      check(result.kind() != Progress::Kind::failed, "Pool close failed");
      if (result.kind() == Progress::Kind::done) break;
      check(std::chrono::steady_clock::now() < deadline, "Pool close did not settle");
      worker::poll(lane, 32); std::this_thread::yield();
    }
    check(arena->charged_bytes.load() == 0 && !worker::pending(lane), "Pool close stranded charges or Messages");
    check(alloc::remove_pool(*pool).has_value(), "remove closed Pool");
    arena.reset(); worker::relinquish(lane);
  }
};

std::uint32_t users(alloc::Storage* storage) { return storage->users.load(std::memory_order_relaxed); }
std::byte pattern(std::size_t index) { return std::byte((index * 37 + 19) % 251); }
void fill(alloc::Allocation& value) {
  for (std::size_t index = 0; index < value.size; ++index) value.address[index] = pattern(index);
}
void verify(const std::byte* address, std::size_t amount, std::size_t offset = 0) {
  for (std::size_t index = 0; index < amount; ++index)
    check(address[index] == pattern(index + offset), "retained body bytes changed");
}

template<class C> Representation<C>* capture(Memory& memory, Geometry& geometry,
    PartOwners owners, std::span<const PartSource> input) {
  return take(capture_representation(memory, C{&geometry.proof}, owners, input));
}

void cross_control_slices_and_padding() {
  Fixture fixture;
  Memory old_memory, new_memory;
  Counters old_counters, new_counters;
  Geometry old_geometry(old_memory, old_counters, {{{17, 0}, 128, 32, 64}});
  auto allocation = fixture.create(128, 32); fill(allocation);
  auto* storage = allocation.storage;
  auto* address = allocation.address;
  const std::array initial{PartSource{{17, 0}, alloc::bytes(allocation), {}, &allocation}};
  old_geometry.fields[0].padding = (std::size_t{1} << 32) + 32;
  refused(capture_representation(old_memory, OldControl{&old_geometry.proof}, PartOwners::independent_allocation, initial),
      Error::invalid_argument, "wide fresh padding truncated at Allocation boundary");
  check(allocation && users(storage) == 1 && !old_memory.live && !old_geometry.proof.uses,
      "wide fresh-padding refusal consumed ownership");
  old_geometry.fields[0].padding = 32;
  auto* old = capture<OldControl>(old_memory, old_geometry, PartOwners::independent_allocation, initial);
  const ObjectBinding original{{5, 7}, 41};
  auto* part = take(od::retain_part(RepresentationSelection<OldControl>{old}, 0, original,
      PartRetention::selected_copy));
  const auto before_factory = users(storage);
  auto retained = take(orbital::source(part));
  check(users(storage) == before_factory && old_memory.blocks == 2,
      "source factory retained or allocated instead of borrowing");
  retained.id = {93, 9}; retained.body = {address + 64, 80};
  auto fresh = fixture.create(64); fill(fresh);
  auto* fresh_storage = fresh.storage;
  Geometry geometry(new_memory, new_counters, {{{93, 9}, 80, 16, 64}, {{93, 2}, 64, 0, 64}});
  std::array input{retained, PartSource{{93, 2}, alloc::bytes(fresh), {}, &fresh}};
  const auto untouched = [&] {
    check(fresh && users(storage) == before_factory && users(fresh_storage) == 1 &&
        !new_memory.live && !geometry.proof.uses, "failed slice capture changed source ownership");
  };
  geometry.fields[0].padding = 17;
  refused(capture_representation(new_memory, NewControl{&geometry.proof}, PartOwners::independent_allocation, input),
      Error::invalid_argument, "new padding crossed certified old footprint"); untouched();
  geometry.fields[0].padding = (std::size_t{1} << 32) + 16;
  refused(capture_representation(new_memory, NewControl{&geometry.proof}, PartOwners::independent_allocation, input),
      Error::invalid_argument, "wide padding truncated at retained footprint boundary"); untouched();
  geometry.fields[0].padding = 16; input[0].body = {address + 65, 80};
  refused(capture_representation(new_memory, NewControl{&geometry.proof}, PartOwners::independent_allocation, input),
      Error::invalid_argument, "misaligned retained slice accepted"); untouched();
  input[0] = retained; input[0].id = {93, 10};
  refused(capture_representation(new_memory, NewControl{&geometry.proof}, PartOwners::independent_allocation, input),
      Error::invalid_argument, "new Control identity mismatch accepted"); untouched();
  input[0] = retained;
  auto* newer = capture<NewControl>(new_memory, geometry, PartOwners::independent_allocation, input);
  check(!fresh && users(storage) == before_factory + 1 && users(fresh_storage) == 1,
      "capture did not pin retained bytes and transfer fresh use exactly once");
  orbital::release(old);
  check(old_geometry.proof.uses == 0 && old_memory.blocks == 1,
      "selected source kept the old whole-geometry proof alive");
  orbital::release(part);
  check(old_memory.live == 0 && old_counters.selected_copies == old_counters.selected_drops && users(storage) == 1,
      "new capture retained source Part or old proof metadata");
  const auto selected = RepresentationSelection<NewControl>{newer};
  const auto view = take(selected.part(std::size_t{0}));
  check(view.address() == address + 64 && selected.control().id(0) == PartId{93, 9},
      "capture did not use new Control geometry and original address");
  verify(view.address(), 64, 64);
  for (std::size_t index = 64; index < 96; ++index)
    check(view.address()[index] == std::byte{0}, "old initialized padding was not preserved");
  auto* escaped = take(od::retain_part(selected, 0, ObjectBinding{{8, 9}, 57}, PartRetention::selected_copy));
  check(binding(escaped).object == ObjectId{8, 9} && binding(escaped).version == 57,
      "new export inherited source ObjectBinding");
  orbital::release(newer); verify(orbital::view(escaped).address(), 64, 64);
  orbital::release(escaped);
  check(!new_memory.live && !geometry.proof.uses && users(storage) == 0 && users(fresh_storage) == 0,
      "cross-Control disposal leaked physical or proof ownership");
  refused(orbital::source(static_cast<Part<OldControl>*>(nullptr)), Error::absent, "null Part source accepted");
  refused(orbital::source(RepresentationSelection<OldControl>{}, 0), Error::absent, "empty selection source accepted");
}

void mixed_export_and_flat_generations() {
  Fixture fixture;
  Memory memory;
  Counters counters;
  unsigned disposed{};
  auto backing = std::shared_ptr<const std::array<std::byte, 64>>(
      new std::array<std::byte, 64>{}, [&disposed](const auto* pointer) { ++disposed; delete pointer; });
  std::weak_ptr<const void> weak = backing;
  Geometry original(memory, counters, {{{17, 0}, 64, 0, 1}});
  std::array shared_source{PartSource{{17, 0}, {backing->data(), 64}, backing, nullptr}};
  auto* old = capture<OldControl>(memory, original, PartOwners::independent_shared, shared_source);
  shared_source[0].backing.reset();
  auto retained = take(orbital::source(RepresentationSelection<OldControl>{old}, 0));
  retained.id = {93, 0};
  auto fresh = fixture.create(64); fill(fresh);
  auto* native = fresh.storage;
  std::array input{retained, PartSource{{93, 1}, alloc::bytes(fresh), {}, &fresh}};
  auto geometry = std::make_unique<Geometry>(memory, counters,
      std::initializer_list<Field>{{{93, 0}, 64, 0, 1}, {{93, 1}, 64, 0, 64}});
  refused(capture_representation(memory, NewControl{&geometry->proof}, PartOwners::independent_allocation, input),
      Error::invalid_argument, "shared retained source accepted as native owner");
  check(fresh && users(native) == 1 && memory.blocks == 1 && !geometry->proof.uses,
      "owner-kind refusal consumed fresh source or retained destination proof");
  auto* current = capture<NewControl>(memory, *geometry, PartOwners::independent_mixed, input);
  orbital::release(old); backing.reset();
  check(original.proof.uses == 0 && memory.blocks == 1 && users(native) == 1,
      "mixed capture retained parent metadata or extra native tokens");
  const auto current_selection = RepresentationSelection<NewControl>{current};
  auto* shared_part = take(od::retain_part(current_selection, 0, ObjectBinding{{1, 2}, 3}, PartRetention::selected_copy));
  auto* native_part = take(od::retain_part(current_selection, 1, ObjectBinding{{1, 2}, 3}, PartRetention::selected_copy));
  check(shared_part->owners == PartOwners::independent_shared && native_part->owners == PartOwners::independent_allocation,
      "mixed export failed to canonicalize actual physical owner kind");
  std::array from_parts{take(orbital::source(shared_part)), take(orbital::source(native_part))};
  auto next_geometry = std::make_unique<Geometry>(memory, counters,
      std::initializer_list<Field>{{{93, 0}, 64, 0, 1}, {{93, 1}, 64, 0, 64}});
  auto* next = capture<NewControl>(memory, *next_geometry, PartOwners::independent_mixed, from_parts);
  orbital::release(current); orbital::release(shared_part); orbital::release(native_part);
  check(geometry->proof.uses == 0, "round-trip through Parts retained old proof");
  geometry = std::move(next_geometry); current = next;
  for (unsigned generation = 0; generation < 32; ++generation) {
    const auto selection = RepresentationSelection<NewControl>{current};
    std::array sources{take(orbital::source(selection, 0)), take(orbital::source(selection, 1))};
    auto next_proof = std::make_unique<Geometry>(memory, counters,
        std::initializer_list<Field>{{{93, 0}, 64, 0, 1}, {{93, 1}, 64, 0, 64}});
    auto* replacement = capture<NewControl>(memory, *next_proof, PartOwners::independent_mixed, sources);
    orbital::release(current);
    check(geometry->proof.uses == 0 && memory.blocks == 1 && users(native) == 1 &&
        weak.use_count() == 1 && !disposed,
        "retained generations accumulated parent metadata or owner tokens");
    geometry = std::move(next_proof); current = replacement;
    const auto view = take(RepresentationSelection<NewControl>{current}.part(std::size_t{1}));
    verify(view.address(), 64);
  }
  orbital::release(current);
  check(!memory.live && !geometry->proof.uses && users(native) == 0 && disposed == 1,
      "flat native chain did not release final owner or proof");
  check(disposed == 1 && weak.expired() && counters.selected_copies == counters.selected_drops,
      "mixed shared or selected-proof owner leaked");
}

void grouped_and_independent_retained_owners() {
  Fixture fixture;
  Memory memory;
  Counters counters;
  Geometry old_geometry(memory, counters, {{{17, 0}, 64, 0, 1}, {{17, 1}, 64, 0, 1}});
  auto allocation = fixture.create(); fill(allocation);
  auto* storage = allocation.storage;
  auto* address = allocation.address;
  const std::array initial{PartSource{{17, 0}, {address, 64}, {}, &allocation},
      PartSource{{17, 1}, {address + 64, 64}, {}, &allocation}};
  auto* old = capture<OldControl>(memory, old_geometry, PartOwners::grouped_allocation, initial);
  const auto selected = RepresentationSelection<OldControl>{old};
  std::array sources{take(orbital::source(selected, 0)), take(orbital::source(selected, 1))};
  Geometry newer(memory, counters, {{{17, 0}, 64, 0, 1}, {{17, 1}, 64, 0, 1}});
  auto* independent = capture<NewControl>(memory, newer, PartOwners::independent_allocation, sources);
  check(users(storage) == 3, "independent retained entries did not take distinct physical tokens");
  auto* grouped = capture<NewControl>(memory, newer, PartOwners::grouped_allocation, sources);
  check(users(storage) == 4, "grouped retained entries took more than one physical token");
  orbital::release(old);
  check(old_geometry.proof.uses == 0 && users(storage) == 3, "grouped source parent remained retained");
  verify(take(RepresentationSelection<NewControl>{grouped}.part(std::size_t{1})).address(), 64, 64);
  orbital::release(independent); check(users(storage) == 1, "independent retained disposal dropped wrong token count");
  orbital::release(grouped);
  check(users(storage) == 0 && !memory.live && !newer.proof.uses, "grouped direct owner leaked");

  unsigned disposed{};
  auto bytes = std::shared_ptr<const std::array<std::byte, 128>>(
      new std::array<std::byte, 128>{}, [&disposed](const auto* pointer) { ++disposed; delete pointer; });
  std::array shared_input{PartSource{{17, 0}, {bytes->data(), 64}, bytes, nullptr},
      PartSource{{17, 1}, {bytes->data() + 64, 64}, bytes, nullptr}};
  auto* shared = capture<OldControl>(memory, old_geometry, PartOwners::grouped_shared, shared_input);
  sources = {take(orbital::source(RepresentationSelection<OldControl>{shared}, 0)),
      take(orbital::source(RepresentationSelection<OldControl>{shared}, 1))};
  auto* shared_group = capture<NewControl>(memory, newer, PartOwners::grouped_shared, sources);
  shared_input[0].backing.reset(); shared_input[1].backing.reset(); bytes.reset();
  orbital::release(shared);
  check(!disposed && old_geometry.proof.uses == 0 && memory.blocks == 1,
      "grouped shared retention kept parent metadata or lost backing");
  orbital::release(shared_group);
  check(disposed == 1 && !memory.live && !newer.proof.uses, "grouped shared backing leaked");
}

void rollback_and_saturation() {
  Fixture first, second;
  Memory old_memory, destination;
  Counters counters;
  Geometry old_geometry(old_memory, counters, {{{17, 0}, 64, 0, 1}, {{17, 1}, 64, 0, 1}});
  auto a = first.create(64), b = second.create(64), fresh = first.create(64);
  auto* a_use = a.storage; auto* b_use = b.storage; auto* fresh_use = fresh.storage;
  const std::array initial{PartSource{{17, 0}, alloc::bytes(a), {}, &a},
      PartSource{{17, 1}, alloc::bytes(b), {}, &b}};
  auto* old = capture<OldControl>(old_memory, old_geometry, PartOwners::independent_allocation, initial);
  const auto selection = RepresentationSelection<OldControl>{old};
  std::array sources{take(orbital::source(selection, 0)), take(orbital::source(selection, 1)),
      PartSource{{17, 2}, alloc::bytes(fresh), {}, &fresh}};
  Geometry newer(destination, counters, {{{17, 0}, 64, 0, 1}, {{17, 1}, 64, 0, 1}, {{17, 2}, 64, 0, 1}});
  const auto intact = [&] {
    check(fresh && fresh.storage == fresh_use && users(fresh_use) == 1 && !destination.live &&
        !newer.proof.uses && users(a_use) == 1, "refused capture consumed fresh source or leaked prior pin/proof");
  };
  destination.limit = 0;
  refused(capture_representation(destination, NewControl{&newer.proof}, PartOwners::independent_allocation, sources),
      Error::capacity, "metadata exhaustion accepted"); intact();
  destination.limit = SIZE_MAX;
  worker::relinquish(second.lane);
  refused(capture_representation(destination, NewControl{&newer.proof}, PartOwners::independent_allocation, sources),
      Error::conflict, "foreign retained owner accepted"); intact();
  check(users(b_use) == 1 && worker::adopt(second.lane).has_value(), "Lane refusal changed second token");
  b_use->users.store(ad::returning - 1, std::memory_order_relaxed);
  refused(capture_representation(destination, NewControl{&newer.proof}, PartOwners::independent_allocation, sources),
      Error::capacity, "late retained-pin saturation accepted"); intact();
  check(users(b_use) == ad::returning - 1, "saturation refusal changed saturated token");
  b_use->users.store(1, std::memory_order_relaxed);

  sources[1] = sources[0]; sources[1].id = {17, 1};
  a_use->users.store(ad::returning - 2, std::memory_order_relaxed);
  refused(capture_representation(destination, NewControl{&newer.proof}, PartOwners::independent_allocation, sources),
      Error::capacity, "duplicate retained pins exceeded aggregate remaining capacity");
  check(users(a_use) == ad::returning - 2 && fresh && users(fresh_use) == 1 &&
      !destination.live && !newer.proof.uses, "duplicate-pin failure did not undo earlier successful pin");
  Geometry grouped_geometry(destination, counters, {{{17, 0}, 64, 0, 1}, {{17, 1}, 64, 0, 1}});
  const auto group_input = std::span<const PartSource>{sources.data(), 2};
  auto* group = capture<NewControl>(destination, grouped_geometry, PartOwners::grouped_allocation, group_input);
  check(users(a_use) == ad::returning - 1, "grouped duplicates retained more than one pin");
  orbital::release(group);
  check(users(a_use) == ad::returning - 2, "grouped saturation-boundary release was inaccurate");
  a_use->users.store(1, std::memory_order_relaxed);

  sources[1] = take(orbital::source(selection, 1));
  destination.hook = +[](void* input) noexcept {
    auto& body = *static_cast<PartSource*>(input);
    body.body = {body.body.data(), body.body.size() + 1};
  };
  destination.context = &sources[1];
  refused(capture_representation(destination, NewControl{&newer.proof}, PartOwners::independent_allocation, sources),
      Error::invalid_argument, "metadata callback mutation escaped final source validation"); intact();
  sources[1] = take(orbital::source(selection, 1));
  newer.proof.retain_hook = +[](void* input) noexcept { worker::relinquish(*static_cast<worker::Lane*>(input)); };
  newer.proof.context = &second.lane;
  refused(capture_representation(destination, NewControl{&newer.proof}, PartOwners::independent_allocation, sources),
      Error::conflict, "proof callback Lane change escaped final native pin"); intact();
  check(worker::adopt(second.lane).has_value(), "readopt source Lane");
  auto* success = capture<NewControl>(destination, newer, PartOwners::independent_allocation, sources);
  check(!fresh && users(a_use) == 2 && users(b_use) == 2 && users(fresh_use) == 1,
      "retry did not retain both old owners and transfer one fresh source");
  orbital::release(old); orbital::release(success);
  check(users(a_use) == 0 && users(b_use) == 0 && users(fresh_use) == 0 &&
      !old_memory.live && !destination.live, "rollback/retry leaked owners or metadata");
}

void scratch_retained_duplicates() {
  Fixture fixture;
  Memory memory;
  Counters counters;
  Geometry old_geometry(memory, counters, {{{17, 0}, 64, 0, 1}});
  auto allocation = fixture.create(64);
  auto* storage = allocation.storage;
  const std::array initial{PartSource{{17, 0}, alloc::bytes(allocation), {}, &allocation}};
  auto* old = capture<OldControl>(memory, old_geometry, PartOwners::independent_allocation, initial);
  std::array<PartSource, 17> sources;
  Geometry newer(memory, counters, {});
  newer.proof.count = sources.size();
  for (std::size_t index = 0; index < sources.size(); ++index) {
    sources[index] = take(orbital::source(RepresentationSelection<OldControl>{old}, 0));
    sources[index].id = {93, index};
    newer.fields[index] = {{93, index}, 64, 0, 1};
  }
  auto* replacement = capture<NewControl>(memory, newer, PartOwners::independent_allocation, sources);
  check(users(storage) == 18 && memory.blocks == 2,
      "scratch alias check rejected retained duplicates or leaked temporary metadata");
  orbital::release(old); orbital::release(replacement);
  check(users(storage) == 0 && !memory.live, "scratch retained capture leaked direct tokens");
}

void original_pool_return_routes() {
  Fixture first, second;
  Memory memory;
  Counters counters;
  Geometry old_geometry(memory, counters, {{{17, 0}, 64, 0, 1}});
  auto allocation = first.create(64);
  auto* old_storage = allocation.storage;
  auto old_guard = take(alloc::hold(allocation, {0, 64}));
  const std::array initial{PartSource{{17, 0}, alloc::bytes(allocation), {}, &allocation}};
  auto* old = capture<OldControl>(memory, old_geometry, PartOwners::independent_allocation, initial);
  auto retained = take(orbital::source(RepresentationSelection<OldControl>{old}, 0));
  auto fresh = second.create(64);
  auto* new_storage = fresh.storage;
  auto new_guard = take(alloc::hold(fresh, {0, 64}));
  const std::array sources{retained, PartSource{{17, 1}, alloc::bytes(fresh), {}, &fresh}};
  Geometry newer(memory, counters, {{{17, 0}, 64, 0, 1}, {{17, 1}, 64, 0, 1}});
  auto* composed = capture<NewControl>(memory, newer, PartOwners::independent_allocation, sources);
  orbital::release(old);
  check(users(old_storage) == 2 && users(new_storage) == 2,
      "composition did not retain both original physical owners alongside guards");
  check(alloc::close(*first.pool).kind() == Progress::Kind::waiting,
      "new capture lost original Pool retention");
  const auto first_returns = alloc::statistics(*first.pool).foreign_returns;
  const auto second_returns = alloc::statistics(*second.pool).foreign_returns;
  orbital::release(composed);
  check(!memory.live && !newer.proof.uses && users(old_storage) == 1 && users(new_storage) == 1,
      "owner disposal failed to leave exactly the independent physical Holds");
  std::thread foreign([old_use = std::move(old_guard), new_use = std::move(new_guard)]() mutable {
    alloc::release(old_use); alloc::release(new_use);
  });
  foreign.join();
  check(worker::pending(first.lane) && worker::pending(second.lane) &&
      users(old_storage) == ad::returning && users(new_storage) == ad::returning,
      "mixed-Pool final disposal failed to queue original return routes");
  worker::poll(second.lane, 32);
  check(users(new_storage) == 0 && users(old_storage) == ad::returning &&
      alloc::statistics(*second.pool).foreign_returns == second_returns + 1 &&
      alloc::statistics(*first.pool).foreign_returns == first_returns,
      "destination Lane incorrectly settled original Pool's retained bytes");
  worker::poll(first.lane, 32);
  check(users(old_storage) == 0 && alloc::statistics(*first.pool).foreign_returns == first_returns + 1 &&
      !memory.live && !newer.proof.uses, "original Pool return or proof disposal leaked");
}
} // namespace

int main() {
  cross_control_slices_and_padding();
  mixed_export_and_flat_generations();
  grouped_and_independent_retained_owners();
  rollback_and_saturation();
  scratch_retained_duplicates();
  original_pool_return_routes();
  std::puts("{\"case\":\"retained-part-capture-geometry-rollback-owners-and-return-routes\",\"passed\":true}");
}
