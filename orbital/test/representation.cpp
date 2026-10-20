#include <orbital/allocation.h>
#include <orbital/detail/allocation/capture.h>
#include <allocation/internal.h>
#include <orbital/instantiate/representation.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory_resource>
#include <source_location>
#include <thread>
#include <unistd.h>

using namespace orbital;
namespace alloc = orbital::allocation;
namespace ad = orbital::allocation::detail;

namespace {
void check(bool condition, const char* message,
           std::source_location location = std::source_location::current()) {
  if (condition) return;
  std::fprintf(stderr, "%s:%u: %s\n", location.file_name(), location.line(), message);
  std::abort();
}

template<class T> T take(Result<T> result,
                        std::source_location location = std::source_location::current()) {
  check(bool(result), "unexpected refusal", location);
  return std::move(*result);
}

template<class T> void refusal(const Result<T>& result, Error error, const char* message) {
  check(!result && result.error() == error, message);
}

struct Memory final : std::pmr::memory_resource {
  std::size_t live{}, limit{SIZE_MAX}, calls{};
  std::size_t hook_call{};
  void (*hook)(void*) noexcept{};
  void* context{};
  void arm(void (*action)(void*) noexcept, void* input, std::size_t call = 1) {
    hook = action; context = input; hook_call = calls + call;
  }
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (++calls == hook_call && hook) {
      const auto action = std::exchange(hook, nullptr);
      action(context);
    }
    if (bytes > limit || live > limit - bytes) throw std::bad_alloc{};
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes;
    return result;
  }
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
    check(live >= bytes, "metadata deallocated twice");
    live -= bytes;
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

struct Geometry {
  Memory* memory;
  std::array<std::size_t, 17> sizes{};
  std::size_t count{1};
  unsigned uses{};
  void (*retain_hook)(void*) noexcept{};
  void* hook_context{};
};

// Deliberately unrelated to Ikea's interpretation. These checks isolate the
// physical capture contract; existing composition checks cover actual formats.
struct Control {
  Geometry* geometry;
  bool valid(std::pmr::memory_resource& memory) const noexcept { return geometry && geometry->memory == &memory; }
  std::pmr::memory_resource* resource() const noexcept { return geometry->memory; }
  std::size_t part_count() const noexcept { return geometry->count; }
  PartId id(std::size_t index) const noexcept { return {17, index}; }
  Result<std::size_t> extent(std::size_t index) const noexcept { return geometry->sizes[index]; }
  std::uint32_t padding(std::size_t) const noexcept { return 0; }
  std::size_t alignment(std::size_t) const noexcept { return 1; }
  void retain() const noexcept {
    ++geometry->uses;
    if (const auto hook = std::exchange(geometry->retain_hook, nullptr)) hook(geometry->hook_context);
  }
  void release() const noexcept {
    check(geometry->uses > 0, "interpretation lifetime released twice");
    --geometry->uses;
  }
};
static_assert(std::is_trivially_copyable_v<Control>);

struct Fixture {
  worker::Lane lane;
  Ref<alloc::Arena> arena;
  alloc::Pool* pool{};
  Fixture() {
    check(bool(worker::adopt(lane)), "adopt owner Lane");
    arena = take(alloc::open_arena(32 * 1024 * 1024));
    pool = take(alloc::add_pool(*arena, lane));
  }
  alloc::Allocation create(std::size_t bytes = 64, std::size_t padding = 0) {
    return take(alloc::allocate(*pool, {.bytes = bytes, .alignment = 64,
        .readable_padding = padding, .initialization = alloc::Initialization::zero}));
  }
  ~Fixture() {
    check(worker::on_lane(lane), "check left owner Lane relinquished");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto progress = alloc::close(*pool);
      check(progress.kind() != Progress::Kind::failed, "Pool close failed");
      if (progress.kind() == Progress::Kind::done) break;
      check(std::chrono::steady_clock::now() < deadline, "Pool close did not drain");
      worker::poll(lane, 32); std::this_thread::yield();
    }
    check(arena->charged_bytes.load() == 0, "close stranded physical or metadata charge");
    check(!worker::pending(lane), "close stranded a return Message");
    check(bool(alloc::remove_pool(*pool)), "remove closed Pool");
    arena.reset(); worker::relinquish(lane);
  }
};

std::uint32_t users(const alloc::Storage* storage) { return storage->users.load(std::memory_order_relaxed); }

PartSource source(Control control, std::size_t index, alloc::Allocation* allocation, Bytes body) {
  return {control.id(index), body, {}, allocation};
}

void existing_hold_and_foreign_return(bool window) {
  Fixture fixture;
  Memory memory;
  const auto size = window ? 2 * static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)) : 64;
  auto allocation = fixture.create(size, 16);
  auto* storage = allocation.storage;
  auto* address = allocation.address;
  address[0] = std::byte{0x79};
  check(ad::route(*storage)->finite != window, "check failed to exercise intended backend");
  auto existing = take(alloc::hold(allocation, {0, size + 16}));
  Geometry geometry{&memory, {size}};
  const Control control{&geometry};
  const std::array bodies{source(control, 0, &allocation, alloc::bytes(allocation))};
  const auto pins = fixture.arena->references.load();
  auto* value = take(capture_representation(memory, control, PartOwners::independent_allocation, bodies));
  check(!allocation && !allocation.address && !allocation.size && !allocation.readable_padding,
        "capture did not clear source ownership");
  check(users(storage) == 2 && fixture.arena->references.load() == pins,
        "capture changed preexisting physical uses or Arena pins");
  const auto view = take(RepresentationSelection<Control>{value}.part(std::size_t{0}));
  check(view.address() == address && view.address()[0] == std::byte{0x79}, "capture changed body address or data");
  orbital::release(value);
  check(users(storage) == 1 && !memory.live && !geometry.uses,
        "Representation disposal did not leave exactly the preexisting Hold");
  check(existing.address[0] == std::byte{0x79} && existing.address[size] == std::byte{0},
        "preexisting Hold lost body or readable padding");
  auto* arena = fixture.arena.get();
  fixture.arena.reset();
  check(arena->references.load() > 0, "last physical Hold failed to pin Arena");
  const auto returned = alloc::statistics(*fixture.pool).foreign_returns;
  std::thread foreign([owned = std::move(existing)]() mutable {
    check(owned.address[0] == std::byte{0x79}, "foreign Hold lost contents");
    alloc::release(owned);
  });
  foreign.join();
  check(worker::pending(fixture.lane) && users(storage) == ad::returning,
        "final foreign Hold did not defer physical return to owner");
  check(arena->references.load() > 0 && arena->charged_bytes.load() > 0,
        "queued foreign return lost route or Arena pin");
  fixture.arena = Ref<alloc::Arena>::retain(*arena);
  check(worker::poll(fixture.lane, 1) == 1, "final foreign return was not drainable");
  check(users(storage) == 0 && alloc::statistics(*fixture.pool).foreign_returns == returned + 1,
        "owner drain did not retire exactly one physical use");
}

void readiness_refusals_and_transfer() {
  Fixture fixture;
  auto allocation = fixture.create();
  auto* storage = allocation.storage;
  const auto original_size = allocation.size;
  allocation.size = 0;
  refusal(ad::ready_for_capture(allocation), Error::invalid_argument, "zero total extent accepted");
  refusal(alloc::hold(allocation, {0, 0}), Error::invalid_argument, "legacy zero extent comparison changed");
  allocation.size = original_size;
  // Diagnostic state injection isolates saturation without creating 8M Holds.
  storage->users.store(ad::returning - 1, std::memory_order_relaxed);
  refusal(ad::ready_for_capture(allocation), Error::capacity, "saturated source accepted");
  refusal(alloc::hold(allocation, {0, original_size}), Error::capacity, "legacy saturation comparison changed");
  check(users(storage) == ad::returning - 1, "saturation refusal changed physical count");
  storage->users.store(1, std::memory_order_relaxed);
  worker::relinquish(fixture.lane);
  refusal(ad::ready_for_capture(allocation), Error::conflict, "foreign owner Lane accepted");
  check(allocation && users(storage) == 1, "foreign-Lane refusal consumed ownership");
  check(bool(worker::adopt(fixture.lane)), "readopt owner Lane");
  check(bool(ad::ready_for_capture(allocation)), "ordinary source readiness refused");
  auto* transferred = ad::take_for_capture(allocation);
  check(transferred == storage && users(storage) == 1 && !allocation && !allocation.address && !allocation.size,
        "take seam changed use count or left source ownership");
  alloc::Hold destination; destination.storage = transferred;
  alloc::release(destination);
}

void grouped_aliases_and_empty_parts() {
  Fixture fixture;
  Memory memory;
  auto allocation = fixture.create();
  auto* storage = allocation.storage;
  auto existing = take(alloc::hold(allocation, {0, allocation.size}));
  Geometry geometry{&memory, {8, 8}, 2};
  const Control control{&geometry};
  const std::array bodies{source(control, 0, &allocation, {allocation.address, 8}),
      source(control, 1, &allocation, {allocation.address + 8, 8})};
  refusal(capture_representation(memory, control, PartOwners::independent_allocation, bodies),
          Error::invalid_argument, "independent aliases accepted");
  check(allocation && users(storage) == 2 && !memory.live && !geometry.uses,
        "alias refusal consumed or retained a source");
  auto* grouped = take(capture_representation(memory, control, PartOwners::grouped_allocation, bodies));
  check(!allocation && users(storage) == 2, "grouped capture consumed source more than once");
  orbital::release(grouped);
  check(users(storage) == 1 && existing.address[0] == std::byte{0}, "grouped release damaged existing Hold");
  alloc::release(existing);

  geometry.count = 1; geometry.sizes[0] = 0;
  const std::array null_body{source(control, 0, nullptr, {})};
  orbital::release(take(capture_representation(memory, control, PartOwners::independent_allocation, null_body)));
  auto live = fixture.create();
  storage = live.storage;
  const std::array empty_body{source(control, 0, &live, {})};
  auto* empty = take(capture_representation(memory, control, PartOwners::independent_allocation, empty_body));
  check(!live && users(storage) == 1, "empty Part did not transfer supplied live Allocation");
  orbital::release(empty);
  check(!memory.live && !geometry.uses, "empty-Part capture leaked metadata or interpretation");
}

void late_refusal_and_metadata_failure() {
  Fixture first_fixture, second_fixture;
  Memory memory;
  auto first = first_fixture.create(8), second = second_fixture.create(8);
  auto* first_storage = first.storage; auto* second_storage = second.storage;
  Geometry geometry{&memory, {8, 8}, 2};
  const Control control{&geometry};
  const std::array bodies{source(control, 0, &first, alloc::bytes(first)),
      source(control, 1, &second, alloc::bytes(second))};
  const auto originals_intact = [&] {
    check(first && second && users(first_storage) == 1 && users(second_storage) == 1,
          "late refusal partially consumed sources");
    check(!memory.live && !geometry.uses, "late refusal leaked metadata or proof");
  };
  worker::relinquish(second_fixture.lane);
  refusal(capture_representation(memory, control, PartOwners::independent_allocation, bodies),
          Error::conflict, "late foreign source accepted");
  originals_intact();
  check(bool(worker::adopt(second_fixture.lane)), "readopt second owner Lane");
  memory.limit = 0;
  refusal(capture_representation(memory, control, PartOwners::independent_allocation, bodies),
          Error::capacity, "metadata exhaustion accepted");
  originals_intact(); memory.limit = SIZE_MAX;
  auto* value = take(capture_representation(memory, control, PartOwners::independent_allocation, bodies));
  check(!first && !second && users(first_storage) == 1 && users(second_storage) == 1,
        "retry failed to transfer both original uses");
  orbital::release(value);
}

void closing_pool_capture() {
  Fixture fixture;
  Memory memory;
  auto allocation = fixture.create();
  auto* storage = allocation.storage;
  Geometry geometry{&memory, {allocation.size}};
  const Control control{&geometry};
  const std::array bodies{source(control, 0, &allocation, alloc::bytes(allocation))};
  const auto progress = alloc::close(*fixture.pool);
  check(progress.kind() == Progress::Kind::waiting && fixture.pool->state == alloc::Pool::State::closing,
        "close did not wait for physical source");
  auto* value = take(capture_representation(memory, control, PartOwners::independent_allocation, bodies));
  check(!allocation && users(storage) == 1, "closing-Pool capture failed to transfer existing use");
  orbital::release(value);
}

void final_preflight_after_callbacks() {
  Fixture first_fixture, second_fixture;
  Memory memory;
  auto first = first_fixture.create(8), second = second_fixture.create(8);
  auto* first_storage = first.storage; auto* second_storage = second.storage;
  Geometry geometry{&memory, {8, 8}, 2};
  const Control control{&geometry};
  std::array bodies{source(control, 0, &first, alloc::bytes(first)),
      source(control, 1, &second, alloc::bytes(second))};
  const auto originals_intact = [&] {
    check(first && second && users(first_storage) == 1 && users(second_storage) == 1,
          "callback-induced refusal partially consumed sources");
    check(!memory.live && !geometry.uses, "callback-induced refusal leaked metadata or proof");
  };
  const auto relinquish = +[](void* input) noexcept { worker::relinquish(*static_cast<worker::Lane*>(input)); };
  memory.arm(relinquish, &second_fixture.lane);
  refusal(capture_representation(memory, control, PartOwners::independent_allocation, bodies),
          Error::conflict, "metadata callback's late Lane change escaped final preflight");
  originals_intact();
  check(bool(worker::adopt(second_fixture.lane)), "readopt callback-relinquished Lane");
  geometry.retain_hook = relinquish; geometry.hook_context = &second_fixture.lane;
  refusal(capture_representation(memory, control, PartOwners::independent_allocation, bodies),
          Error::conflict, "Control retain's late Lane change escaped final preflight");
  originals_intact();
  check(bool(worker::adopt(second_fixture.lane)), "readopt retain-relinquished Lane");
  memory.arm(+[](void* input) noexcept { static_cast<alloc::Allocation*>(input)->size = 7; }, &second);
  refusal(capture_representation(memory, control, PartOwners::independent_allocation, bodies),
          Error::invalid_argument, "metadata callback's changed source extent escaped final preflight");
  originals_intact(); second.size = 8;
  memory.arm(+[](void* input) noexcept {
    auto& sources = *static_cast<std::array<PartSource, 2>*>(input);
    sources[1].allocation = sources[0].allocation; sources[1].body = sources[0].body;
  }, &bodies);
  refusal(capture_representation(memory, control, PartOwners::independent_allocation, bodies),
          Error::invalid_argument, "metadata callback's duplicate source escaped final alias check");
  originals_intact();
}

void scratch_branch_final_alias_check() {
  Fixture fixture;
  Memory memory;
  std::array<alloc::Allocation, 17> allocations;
  std::array<alloc::Storage*, 17> storages{};
  Geometry geometry{&memory, {}, 17};
  const Control control{&geometry};
  std::array<PartSource, 17> bodies;
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    allocations[i] = fixture.create();
    storages[i] = allocations[i].storage;
    bodies[i] = source(control, i, &allocations[i], {});
  }
  // Scratch reserves first; Representation allocation can still change sources.
  // Empty Parts leave alias uniqueness as the sole reason this input is invalid.
  memory.arm(+[](void* input) noexcept {
    auto& sources = *static_cast<std::array<PartSource, 17>*>(input);
    sources.back().allocation = sources.front().allocation;
  }, &bodies, 2);
  refusal(capture_representation(memory, control, PartOwners::independent_allocation, bodies),
          Error::invalid_argument, "large-Part scratch reused stale alias identities");
  check(memory.calls == 2, "large-Part check did not reach scratch then metadata callback");
  for (std::size_t i = 0; i < allocations.size(); ++i)
    check(allocations[i] && allocations[i].storage == storages[i] && users(storages[i]) == 1,
          "large-Part alias refusal partially consumed or retained a source");
  check(!memory.live && !geometry.uses, "large-Part alias refusal leaked scratch, metadata or proof");
  bodies.back().allocation = &allocations.back();
  auto* value = take(capture_representation(memory, control, PartOwners::independent_allocation, bodies));
  for (std::size_t i = 0; i < allocations.size(); ++i)
    check(!allocations[i] && !allocations[i].address && !allocations[i].size && users(storages[i]) == 1,
          "large-Part retry did not transfer every original use exactly once");
  orbital::release(value);
  for (auto* storage : storages) check(users(storage) == 0, "large-Part disposal retained a transferred use");
  check(!memory.live && !geometry.uses, "large-Part retry leaked scratch, metadata or proof");
}

void retained_parts_fit_descriptor_budget() {
  Fixture fixture;
  Memory memory;
  Geometry geometry{&memory, {}, 17};
  geometry.sizes.fill(1);
  const Control control{&geometry};
  auto original = fixture.create();
  auto* old_storage = original.storage;
  std::array<PartSource, 17> bodies;
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    original.address[i] = std::byte(i);
    bodies[i] = source(control, i, &original, {original.address + i, 1});
  }
  auto* old = take(capture_representation(memory, control, PartOwners::grouped_allocation, bodies));
  const RepresentationSelection<Control> selected{old};
  for (std::size_t i = 0; i + 1 < bodies.size(); ++i) bodies[i] = take(orbital::source(selected, i));
  auto fresh = fixture.create();
  auto* new_storage = fresh.storage;
  fresh.address[0] = std::byte{0x79};
  bodies.back() = source(control, bodies.size() - 1, &fresh, {fresh.address, 1});
  // Old bytes plus the final independent descriptor fit; a temporary alias
  // array for retained Parts would make this otherwise valid capture refuse.
  memory.limit = memory.live + sizeof(Representation<Control>) + bodies.size() * sizeof(object_detail::AllocatedBody);
  auto* next = take(capture_representation(memory, control, PartOwners::independent_allocation, bodies));
  check(!fresh && users(old_storage) == 17 && users(new_storage) == 1,
        "partial copy did not retain sixteen sources and transfer one fresh use");
  orbital::release(old);
  const RepresentationSelection<Control> result{next};
  for (std::size_t i = 0; i < bodies.size(); ++i)
    check(take(result.part(i)).address()[0] == (i + 1 == bodies.size() ? std::byte{0x79} : std::byte(i)),
          "partial copy lost retained or replacement bytes after old descriptor release");
  orbital::release(next);
  check(!users(old_storage) && !users(new_storage) && !memory.live && !geometry.uses,
        "partial copy leaked backing, metadata or interpretation");
}

void callback_expands_fresh_suppliers() {
  for (const std::size_t initial : {1, 9}) {
    Fixture fixture;
    Memory memory;
    Geometry geometry{&memory, {}, 17};
    const Control control{&geometry};
    std::array<alloc::Allocation, 17> allocations;
    std::array<alloc::Storage*, 17> storages;
    std::array<PartSource, 17> bodies;
    for (std::size_t i = 0; i < allocations.size(); ++i) {
      allocations[i] = fixture.create();
      storages[i] = allocations[i].storage;
    }
    const auto reset = [&] {
      for (std::size_t i = 0; i < bodies.size(); ++i)
        bodies[i] = source(control, i, i < initial ? &allocations[i] : nullptr, {});
    };
    // Deliberately duplicate authority in a DIFFERENT wrapper: alias detection
    // must compare Storage identities. Clear this injected wrapper after refusal.
    alloc::Allocation alias;
    alias.storage = allocations[0].storage; alias.address = allocations[0].address;
    alias.size = allocations[0].size; alias.readable_padding = allocations[0].readable_padding;
    struct Expansion {
      std::span<PartSource> bodies;
      std::span<alloc::Allocation> allocations;
      alloc::Allocation* duplicate;
    } expansion{bodies, allocations, &alias};
    const auto expand = +[](void* input) noexcept {
      auto& e = *static_cast<Expansion*>(input);
      for (std::size_t i = 0; i < e.bodies.size(); ++i) e.bodies[i].allocation = &e.allocations[i];
      if (e.duplicate) e.bodies.back().allocation = e.duplicate;
    };
    reset();
    const auto calls = initial > 8 ? 2 : 1;
    memory.arm(expand, &expansion, calls);
    refusal(capture_representation(memory, control, PartOwners::independent_allocation, bodies),
            Error::invalid_argument, "callback expansion missed a late duplicate Storage");
    check(memory.calls == std::size_t(calls), "expanded alias check performed another metadata allocation");
    for (std::size_t i = 0; i < allocations.size(); ++i)
      check(allocations[i] && users(storages[i]) == 1, "expanded alias refusal consumed or retained a source");
    check(!memory.live && !geometry.uses, "expanded alias refusal leaked scratch, metadata or proof");
    alias.storage = nullptr;

    reset(); expansion.duplicate = nullptr;
    memory.arm(expand, &expansion, calls);
    auto* value = take(capture_representation(memory, control, PartOwners::independent_allocation, bodies));
    check(memory.calls == 2 * std::size_t(calls), "valid expansion allocated beyond its advisory reservation");
    for (std::size_t i = 0; i < allocations.size(); ++i)
      check(!allocations[i] && users(storages[i]) == 1, "valid expansion did not transfer every source once");
    orbital::release(value);
    for (auto* storage : storages) check(!users(storage), "expanded capture did not return a source");
    check(!memory.live && !geometry.uses, "valid expansion leaked scratch, metadata or proof");
  }
}

void shared_backing_prefix_growth() {
  Fixture fixture;
  Memory memory;
  const auto system_page = ::sysconf(_SC_PAGESIZE);
  check(system_page > 0, "runtime page size unavailable");
  const auto page = static_cast<std::size_t>(system_page);
  struct Backing { alloc::Allocation allocation; alloc::Hold guard; };
  unsigned destroyed = 0;
  auto writer = std::shared_ptr<Backing>(new Backing{
      take(alloc::allocate(*fixture.pool, {.bytes = page, .reserved_bytes = 3 * page,
          .alignment = 64, .initialization = alloc::Initialization::zero})), {}},
      [&destroyed](auto* pointer) { ++destroyed; delete pointer; });
  std::weak_ptr<Backing> lifetime = writer;
  auto& allocation = writer->allocation;
  auto* address = allocation.address;
  auto* storage = allocation.storage;
  check(alloc::reserved_bytes(allocation) == 3 * page, "fixed reservation changed");
  for (std::size_t i = 0; i < page; ++i) address[i] = std::byte{0x29};
  // Shared ownership retains storage; this Hold independently prevents relocation.
  writer->guard = take(alloc::hold(allocation, {0, page}));
  Geometry short_geometry{&memory, {page}}, long_geometry{&memory, {2 * page}};
  const auto capture = [&](Control control) {
    const std::array bodies{PartSource{control.id(0),
        {address, take(control.extent(0))}, writer, nullptr}};
    return take(capture_representation(memory, control, PartOwners::independent_shared, bodies));
  };
  auto* old = capture({&short_geometry});
  const RepresentationSelection<Control> old_selection{old};
  const auto old_view = take(old_selection.part(std::size_t{0}));
  check(bool(alloc::grow(allocation, {.bytes = 2 * page, .reserved_bytes = 3 * page,
        .alignment = 64})), "held in-reservation growth refused");
  check(allocation.address == address, "held growth relocated the shared backing");
  // Only the disjoint, previously uncaptured suffix is writable.
  for (std::size_t i = page; i < 2 * page; ++i) address[i] = std::byte{0x92};
  auto* newer = capture({&long_geometry});
  const RepresentationSelection<Control> new_selection{newer};
  const auto new_view = take(new_selection.part(std::size_t{0}));
  const auto before = alloc::statistics(*fixture.pool);
  const auto charge = fixture.arena->charged_bytes.load();
  refusal(alloc::grow(allocation, {.bytes = 4 * page, .reserved_bytes = 4 * page,
      .alignment = 64}), Error::conflict, "held beyond-reservation growth relocated backing");
  const auto after = alloc::statistics(*fixture.pool);
  check(allocation.address == address && allocation.size == 2 * page && !allocation.readable_padding &&
        alloc::reserved_bytes(allocation) == 3 * page && fixture.arena->charged_bytes.load() == charge &&
        after.allocations == before.allocations && after.growths == before.growths &&
        after.relocations == before.relocations, "refused growth changed storage or extent");
  check(old_view.address() == address && new_view.address() == address &&
        take(old_selection.control().extent(0)) == page &&
        take(new_selection.control().extent(0)) == 2 * page, "growth changed immutable captured geometry");
  for (std::size_t i = 0; i < 2 * page; ++i)
    check(new_view.address()[i] == (i < page ? std::byte{0x29} : std::byte{0x92}),
          "growth or refusal changed sealed prefix or initialized suffix");
  writer.reset(); orbital::release(newer);
  check(!lifetime.expired() && !destroyed && short_geometry.uses == 1 && !long_geometry.uses &&
        users(storage) == 2, "old prefix failed to retain backing and address guard");
  check(old_view.address() == address && take(old_selection.control().extent(0)) == page,
        "writer release changed old address or extent");
  for (std::size_t i = 0; i < page; ++i)
    check(old_view.address()[i] == std::byte{0x29}, "old prefix lost its sealed bytes");
  orbital::release(old);
  check(lifetime.expired() && destroyed == 1 && !short_geometry.uses && !long_geometry.uses &&
        !memory.live && users(storage) == 0, "final prefix did not settle owner, Hold, proof and metadata once");
}

void shared_body_ownership() {
  Memory memory;
  Geometry geometry{&memory, {8, 8}, 2};
  const Control control{&geometry};
  unsigned destroyed = 0;
  auto owner = std::shared_ptr<std::array<std::byte, 16>>(
      new std::array<std::byte, 16>{}, [&destroyed](auto* pointer) {
        ++destroyed; delete pointer;
      });
  (*owner)[0] = std::byte{0x25}; (*owner)[8] = std::byte{0x68};
  std::weak_ptr<std::array<std::byte, 16>> lifetime = owner;
  std::shared_ptr<const void> alias(owner, nullptr);
  check(!alias && alias.use_count(), "shared ownership check needs a null-pointee alias");
  std::array<PartSource, 2> bodies{{
      {control.id(0), {owner->data(), 8}, alias, nullptr},
      {control.id(1), {owner->data() + 8, 8}, alias, nullptr}}};
  auto distinct = std::make_shared<unsigned>(1);
  bodies[1].backing = distinct;
  const auto original_uses = owner.use_count(), distinct_uses = distinct.use_count();
  refusal(capture_representation(memory, control, PartOwners::grouped_shared, bodies),
          Error::invalid_argument, "grouped distinct shared owners accepted");
  check(owner.use_count() == original_uses && distinct.use_count() == distinct_uses &&
        !memory.live && !geometry.uses && !destroyed,
        "grouped shared refusal retained an owner or leaked empty metadata slots");
  bodies[1].backing.reset();
  const auto missing_uses = owner.use_count();
  for (auto policy : {PartOwners::grouped_shared, PartOwners::independent_shared}) {
    refusal(capture_representation(memory, control, policy, bodies), Error::invalid_argument,
            "nonempty shared body without backing accepted");
    check(owner.use_count() == missing_uses && !memory.live && !geometry.uses && !destroyed,
          "missing shared backing leaked owner, metadata or interpretation");
  }
  bodies[1].backing = alias;
  auto* value = take(capture_representation(memory, control, PartOwners::grouped_shared, bodies));
  bodies = {}; alias.reset(); owner.reset();
  check(!lifetime.expired() && !destroyed, "capture did not retain null-pointee shared owner");
  const RepresentationSelection<Control> selection{value};
  check(take(selection.part(std::size_t{0})).address()[0] == std::byte{0x25} &&
        take(selection.part(std::size_t{1})).address()[0] == std::byte{0x68},
        "shared capture changed body addresses or lifetime");
  orbital::release(value);
  check(lifetime.expired() && destroyed == 1 && !memory.live && !geometry.uses,
        "shared Representation did not dispose backing and metadata exactly once");
}
} // namespace

int main() {
  existing_hold_and_foreign_return(false);
  existing_hold_and_foreign_return(true);
  readiness_refusals_and_transfer();
  grouped_aliases_and_empty_parts();
  late_refusal_and_metadata_failure();
  closing_pool_capture();
  final_preflight_after_callbacks();
  scratch_branch_final_alias_check();
  retained_parts_fit_descriptor_budget();
  callback_expands_fresh_suppliers();
  shared_body_ownership();
  shared_backing_prefix_growth();
  std::puts("Representation capture: lifetime, refusal and callback checks passed");
}
