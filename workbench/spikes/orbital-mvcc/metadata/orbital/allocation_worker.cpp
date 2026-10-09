// Size, ownership-trait, geometry and actual-call ABI census against frozen 7f73c81.
// Build/run commands and interpretation live in allocation-worker.md.
#include <orbital/preparation.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

// Probe-only access to FiniteGroup's private geometry, never a runtime interface.
#define private public
#include "finite.h"
#undef private
#include "internal.h"
// These cold TU-local records are measured from their definitions, not mirrors.
// The executable links every other frozen runtime TU, but not group.cpp again.
#include "group.cpp"

using namespace orbital;
using namespace orbital::allocation;
using namespace orbital::allocation::detail;

template<class T> void type(const char* name) {
  std::printf("{\"type\":\"%s\",\"size\":%zu,\"align\":%zu,"
      "\"trivial_copy\":%s,\"trivial_destructor\":%s,"
      "\"trivial_move\":%s,\"copy_constructible\":%s,"
      "\"move_constructible\":%s,\"standard_layout\":%s}\n", name, sizeof(T), alignof(T),
      std::is_trivially_copyable_v<T> ? "true" : "false",
      std::is_trivially_destructible_v<T> ? "true" : "false",
      std::is_trivially_move_constructible_v<T> ? "true" : "false",
      std::is_copy_constructible_v<T> ? "true" : "false",
      std::is_move_constructible_v<T> ? "true" : "false",
      std::is_standard_layout_v<T> ? "true" : "false");
}

// Keep these real return types visible in -emit-llvm/-S output. Ref/Allocation
// ownership remains unchanged; a small handle with a destructor may use sret.
[[gnu::noinline]] Ref<Arena> aw_return_ref(const Ref<Arena>& value) { return value; }
[[gnu::noinline]] Result<Ref<Arena>> aw_return_arena(std::uint64_t bytes) { return open_arena(bytes); }
[[gnu::noinline]] Result<Pool*> aw_return_pool(Arena& arena, worker::Lane& lane) { return add_pool(arena, lane); }
[[gnu::noinline]] Allocation aw_return_allocation(Allocation&& value) { return std::move(value); }
[[gnu::noinline]] Result<Allocation> aw_return_allocate(Pool& pool, const AllocationRequest& request) { return allocate(pool, request); }
[[gnu::noinline]] Hold aw_return_hold(const Hold& value) { return retain(value); }
[[gnu::noinline]] Result<Hold> aw_return_held(const Allocation& value, ByteRange range) { return hold(value, range); }
[[gnu::noinline]] Progress aw_return_progress(Signal& signal) { return Progress::wait(signal); }
[[gnu::noinline]] Result<void> aw_return_status(worker::Lane& lane) { return worker::adopt(lane); }
[[gnu::noinline]] Statistics aw_return_statistics(const Pool& pool) { return statistics(pool); }
[[gnu::noinline]] ReleaseHint aw_return_hint(const ReleaseHint& hint) { return hint; }
[[gnu::noinline]] PreparedBytes aw_return_prepared(const PreparedBytes& value) { return value; }
[[gnu::noinline]] AllocationRequest aw_return_request(const AllocationRequest& request) { return request; }
[[gnu::noinline]] Result<Identity> aw_return_identity(FiniteGroup& group) { return group.claim_identity(); }
[[gnu::noinline]] Result<Home> aw_return_home(FiniteGroup& group, Identity identity) { return group.bound_home(identity); }
[[gnu::noinline]] Result<std::unique_ptr<FiniteGroup>> aw_return_geometry() { return FiniteGroup::create(); }
[[gnu::noinline]] Result<std::pair<Identity, Home>> aw_return_candidate(Group& group, std::uint64_t& excluded) {
  return claim_finite_candidate(group, 512, 16, excluded);
}
[[gnu::noinline]] Result<std::optional<FiniteCandidate>> aw_return_ready(Pool& pool) { return claim_ready_candidate(pool, 512, 16); }
[[gnu::noinline]] MutableBytes aw_bytes(const Allocation& value) { return bytes(value); }
[[gnu::noinline]] void aw_release(Allocation& value, ReleaseHint hint) { release(value, hint); }
[[gnu::noinline]] Result<void> aw_construct(Preparation& request, PreparedBytes value) {
  return request.construct(value, request.context);
}

template<class T> void field(const char* name, std::size_t offset) {
  std::printf("{\"field\":\"%s\",\"offset\":%zu,\"size\":%zu}\n", name, offset, sizeof(T));
}

void geometry(Palette palette) {
  auto result = FiniteGroup::create({palette, 256});
  assert(result);
  auto& g = **result;
  const auto bitmap_words = g.live_words();
  const auto trim_runs = (g.payload_pages() + 1) / 2;
  std::vector<std::uint64_t> live(bitmap_words);
  std::vector<PageSpan> trimming;
  std::vector<bool> discarded;
  trimming.reserve(trim_runs);
  discarded.reserve(trim_runs);
  const auto vectors = live.capacity() * sizeof(std::uint64_t) +
      trimming.capacity() * sizeof(PageSpan) + (discarded.capacity() + 7) / 8;
  const auto exact = g.control_bytes() + sizeof(FiniteGroup) + sizeof(Group) + vectors;
  std::printf("{\"geometry\":\"%s\",\"identities\":%zu,\"native_page\":%zu,"
      "\"grids\":%zu,\"grid_bytes\":%zu,\"payload_pages\":%zu,"
      "\"binding_useful_bytes\":%zu,\"binding_mapping_bytes\":%zu,"
      "\"metadata_mapping_bytes\":%zu,\"control_mapping_bytes\":%zu,"
      "\"payload_va_bytes\":%zu,\"reservation_bytes\":%zu,"
      "\"live_vector_bytes\":%zu,\"trim_vector_bytes\":%zu,"
      "\"discard_vector_bytes\":%zu,\"charged_metadata_bytes\":%zu}\n",
      palette == Palette::a ? "a" : "b", g.identity_count(), g.native_page(),
      g.grid_count_, g.grid_count_ * sizeof(FiniteGroup::Grid), g.payload_pages(),
      g.identity_count(), g.native_page(), g.metadata_bytes_, g.control_bytes(),
      g.reserved_bytes_ - g.native_page(), g.reserved_bytes_,
      live.capacity() * sizeof(std::uint64_t), trimming.capacity() * sizeof(PageSpan),
      (discarded.capacity() + 7) / 8, exact);
}

void inventory(const char* name, Arena& arena, Pool& pool) {
  const auto counts = statistics(pool);
  const auto& p = *pool.internal;
  const auto& a = *arena.state;
  const auto setup = sizeof(Arena) + sizeof(ArenaState) + sizeof(Pool) + sizeof(PoolState) +
      a.pools.capacity() * sizeof(std::unique_ptr<Pool>) +
      p.groups.capacity() * sizeof(std::unique_ptr<Group>) +
      p.windows.capacity() * sizeof(std::unique_ptr<Window>);
  std::printf("{\"inventory\":\"%s\",\"charged_bytes\":%llu,\"mapped_bytes\":%llu,"
      "\"metadata_bytes\":%llu,\"reserved_bytes\":%llu,"
      "\"groups\":%zu,\"windows\":%zu,\"arena_pool_capacity\":%zu,"
      "\"group_capacity\":%zu,\"window_capacity\":%zu,"
      "\"uncharged_setup_and_inventory_requested_bytes\":%zu}\n", name,
      static_cast<unsigned long long>(arena.charged_bytes.load()),
      static_cast<unsigned long long>(counts.mapped_bytes),
      static_cast<unsigned long long>(counts.metadata_bytes),
      static_cast<unsigned long long>(counts.reserved_bytes), p.groups.size(), p.windows.size(),
      a.pools.capacity(), p.groups.capacity(), p.windows.capacity(), setup);
}

int main() {
#define TYPE(T) type<T>(#T)
  TYPE(Ref<Arena>); TYPE(Result<Ref<Arena>>); TYPE(Result<Pool*>); TYPE(Result<void>);
  TYPE(AllocationRequest); TYPE(Allocation); TYPE(Result<Allocation>);
  TYPE(Hold); TYPE(Result<Hold>); TYPE(ReleaseHint); TYPE(Statistics);
  TYPE(PreparedBytes); TYPE(Preparation); TYPE(Preparation::Outcome);
  TYPE(Arena); TYPE(Pool); TYPE(Storage); TYPE(Domain); TYPE(Job);
  TYPE(Group); TYPE(Window); TYPE(ReadyCandidate); TYPE(PoolState); TYPE(ArenaState);
  TYPE(FiniteConfig); TYPE(Identity); TYPE(Result<Identity>); TYPE(PageSpan); TYPE(Home); TYPE(Result<Home>);
  TYPE(FiniteGroup); TYPE(FiniteGroup::Grid); TYPE(FiniteGroup::Page);
  TYPE(FinitePreparation); TYPE(FiniteCandidate); TYPE(Result<std::unique_ptr<FiniteGroup>>);
  using Candidate = Result<std::pair<Identity, Home>>;
  using Ready = Result<std::optional<FiniteCandidate>>;
  TYPE(Candidate); TYPE(Ready);
  TYPE(worker::Message); TYPE(worker::MessageOps); TYPE(worker::Lane); TYPE(worker::Thread);
  TYPE(Signal); TYPE(WaitLink); TYPE(Progress); TYPE(ByteRange); TYPE(Bytes); TYPE(MutableBytes); TYPE(Scope);
  TYPE(ObjectId); TYPE(PartId); TYPE(Epoch); TYPE(SerialPosition);
  TYPE(std::mutex); TYPE(std::condition_variable); TYPE(std::thread);
#undef TYPE
#define FIELD(T, M) field<decltype(T::M)>(#T "." #M, offsetof(T, M))
  FIELD(Storage, domain); FIELD(Storage, lifetime); FIELD(Storage, held_extent);
  FIELD(Storage, users); FIELD(Storage, identity); FIELD(Storage, foreign_hint_pending);
  FIELD(Domain, pool); FIELD(Domain, context); FIELD(Domain, finite);
  FIELD(Domain, returns); FIELD(Domain, mutex); FIELD(Domain, queued); FIELD(Domain, lifetime);
  FIELD(Group, domain); FIELD(Group, owner_index); FIELD(Group, next_available);
  FIELD(Group, previous_available); FIELD(Group, geometry); FIELD(Group, uses);
  FIELD(Group, hints); FIELD(Group, foreign_hints); FIELD(Group, live);
  FIELD(Group, returned); FIELD(Group, trimming); FIELD(Group, discarded);
  FIELD(Group, metadata_charge); FIELD(Group, job); FIELD(Group, trimmed);
  FIELD(Group, trim_lifetime); FIELD(Group, trim_pending); FIELD(Group, retiring);
  FIELD(Group, retired); FIELD(Group, control_preparing); FIELD(Group, palette);
  FIELD(Group, preparations); FIELD(Group, last_use); FIELD(Group, retire_charge);
  FIELD(Group, retire_mapped); FIELD(Group, retire_reserved);
  FIELD(worker::Lane, cpu); FIELD(worker::Lane, notify_posted); FIELD(worker::Lane, notify_context);
  FIELD(worker::Lane, sentinel); FIELD(worker::Lane, producer); FIELD(worker::Lane, consumer);
  FIELD(worker::Lane, owner); FIELD(worker::Lane, owner_next);
#undef FIELD
  geometry(Palette::a); geometry(Palette::b);

  worker::Lane lane;
  assert(worker::adopt(lane));
  auto arena = open_arena(1ull << 30);
  assert(arena);
  auto pool = add_pool(**arena, lane);
  assert(pool);
  inventory("empty", **arena, **pool);
  auto tiny = allocate(**pool, {.bytes = 64});
  assert(tiny);
  inventory("one_64B", **arena, **pool);
  auto medium = allocate(**pool, {.bytes = 512});
  assert(medium);
  inventory("64B_plus_512B", **arena, **pool);
  auto large = allocate(**pool, {.bytes = 65536});
  assert(large);
  inventory("plus_64KiB_window", **arena, **pool);
  release(*tiny); release(*medium); release(*large);
  while (close(**pool).kind() != Progress::Kind::done) {
    worker::poll(lane, 64);
    std::this_thread::yield();
  }
  inventory("closed", **arena, **pool);
  assert(remove_pool(**pool));
  arena->reset();
  worker::relinquish(lane);
}
