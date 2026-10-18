#pragma once

// Shared implementation records and small leaves of the allocation submodule.
// Backend selection, callbacks and preparation records stay in their owning TU.

#include <orbital/preparation.h>
#include "finite.h"
#include <array>
#include <bit>
#include <cassert>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <vector>
#include <unistd.h>

namespace orbital::allocation {
namespace detail { struct Domain; struct Group; struct Window; }
// Physical retention is independent of the dense one-byte binding plane.
// One immutable identity byte and 24 use-state bits, including the return tag.
struct UseState {
  static constexpr std::uint32_t user_mask = 0x00ffffffu;
  std::atomic<std::uint32_t> state{};
  std::uint32_t load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return state.load(order) & user_mask;
  }
  void store(std::uint32_t users, std::memory_order order = std::memory_order_seq_cst) noexcept {
    state.store((state.load(std::memory_order_relaxed) & ~user_mask) | users, order);
  }

};
struct Storage {
  UseState users;
  std::uint8_t identity() const noexcept { return users.state.load(std::memory_order_relaxed) >> 24; }
  void initialize_identity(std::uint8_t index) noexcept {
    users.state.store(std::uint32_t{index} << 24, std::memory_order_relaxed);
  }
};
static_assert(sizeof(Storage) == 4);

namespace detail {
constexpr std::uint32_t returning = std::uint32_t{1} << 23;
constexpr std::uint16_t group_identities = 256;


struct Job {
  Job* next{};
  void* context{};
  void (*run)(Job&) noexcept{};
};

struct Domain {
  Pool* pool{};
  void* context{};
  bool finite{};
  worker::Message returns;
  std::mutex mutex; // Foreign return publication only; local native use bypasses it.
  std::atomic<bool> queued{};
  Ref<Arena> lifetime;
};

// Actual routing state precedes the slots; there is no loaded header pointer.
// Recovery remains the pinned Clang/Linux container-of experiment.
template<std::size_t Count>
struct StorageOwner {
  Domain domain;
  std::array<Storage, Count> slots{};
  Storage& operator[](std::size_t index) noexcept { return slots[index]; }
  constexpr std::size_t size() const noexcept { return Count; }
};
static_assert(std::is_standard_layout_v<Domain>);
static_assert(std::is_standard_layout_v<StorageOwner<256>>);
static_assert(std::is_standard_layout_v<StorageOwner<1>>);
static_assert(offsetof(StorageOwner<256>, slots) == offsetof(StorageOwner<1>, slots));
static_assert(offsetof(StorageOwner<1>, slots) == sizeof(Domain));

inline Domain* route(Storage& use, std::uint32_t state) noexcept {
  auto* first = &use - (state >> 24);
  return reinterpret_cast<Domain*>(reinterpret_cast<std::byte*>(first) - offsetof(StorageOwner<1>, slots));
}
inline Domain* route(Storage& use) noexcept { return route(use, use.users.state.load(std::memory_order_relaxed)); }
inline const Domain* route(const Storage& use) noexcept { return route(const_cast<Storage&>(use)); }

struct Group {
  StorageOwner<group_identities> uses;
  std::size_t owner_index{}; // Pool owning-vector position, not an allocation identity.
  Group* next_available{};
  Group** previous_available{};
  std::unique_ptr<FiniteGroup> geometry;
  Ref<Arena> active_lifetime; // Owner-managed pin while any finite identity is live/returning.
  std::uint16_t active_identities{};
  ReleaseHint foreign_use;
  bool advice_pending{};
  std::vector<std::uint64_t> live;
  std::array<std::uint64_t, (group_identities + 63) / 64> returned{};
  std::uint64_t metadata_charge{};
  Job job;
  worker::Message trimmed;
  Ref<Arena> trim_lifetime;
  bool trim_pending{}, retiring{};
  bool retired{}, control_preparing{};
  Palette palette{};
  std::uint64_t preparations{};
  ReleaseHint last_use;
  std::uint64_t retire_charge{}, retire_mapped{}, retire_reserved{};
};

struct IdleWindows {
  PoolState* owner{};
  Window* head{};
  std::size_t records{}; // All registered Windows, including active/preparing ones.
};
using WindowSizes = std::map<std::size_t, IdleWindows>;

struct Window {
  StorageOwner<1> uses;
  std::size_t owner_index{}; // Pool owning-vector position, not an allocation identity.
  Ref<Arena> active_lifetime;
  Window* next_idle{};
  Window** previous_idle{};
  WindowSizes::value_type* size_entry{};
  Window* next_size{};
  Window** previous_size{};
  std::uint64_t idle_order{};
  std::byte* mapping{};
  std::size_t mapping_bytes{};
  std::byte* address{};
  // prepared is confirmed accessible; charged also covers uncertain pages
  // after a failed preparation, until retry or actual unmapping.
  std::size_t reserved{}, prepared{}, charged{};
  // Created on the first real Hold. Counts protect native-page endpoint bins;
  // no per-Hold list or change to the four-byte Storage identity.
  std::unique_ptr<std::atomic<std::uint32_t>[]> readers;
  std::size_t tail_extent{}, tail_limit{};
  ReleaseHint hint, foreign_hint;
  std::atomic<bool> foreign_hint_pending{};
  Job job;
  worker::Message trimmed;
  Ref<Arena> trim_lifetime;
  bool trim_pending{}, discarded{}, tail_released{};
  std::uint64_t metadata_charge{};
  Preparation* preparation{}; // Cold pending record, null in ordinary ready inventory.

  ~Window();
};

struct ReadyCandidate {
  Group* group{};
  std::uint16_t minimum{}, maximum{}, alignment{};
  std::uint8_t grid{};
};

struct PoolState {
  std::array<ReadyCandidate, 4> ready_candidates;
  std::uint8_t next_candidate{};
  Pool* next_interested{};
  Pool* previous_interested{};
  std::vector<std::unique_ptr<Group>> groups;
  Group* available_groups{};
  // Register sizes before VM publication; returns/reuse never allocate index nodes.
  // Index and intrusive heads outlive Window destruction, including final-pin teardown.
  WindowSizes window_sizes;
  Window* idle_windows{};
  std::uint64_t idle_order{};
  std::vector<std::unique_ptr<Window>> windows;
  worker::Message notice;
  std::atomic<bool> notice_pending{};
  Ref<Arena> notice_lifetime;
  Job broadcast_job; // Reuses the notice pin through cold registry work and its reply.
  bool broadcast_requested{}, close_requested{}; // Assigned Pool lane only.
  Statistics counts;
  bool trim_failed{};
  std::uint64_t preparations{}, preparation_debt{}, preparation_metadata{}, preparation_uncertain{};
};

struct ArenaState {
  std::mutex registry;
  std::vector<std::unique_ptr<Pool>> pools;
  std::atomic<Pool*> interested_pools{};
  std::mutex jobs_mutex;
  std::condition_variable jobs_ready;
  Job* first{};
  Job* last{};
  bool stopping{};
  worker::Thread reclaimer;

  static void reclaim(void* context) noexcept;

  ~ArenaState();
};
} // namespace detail
} // namespace orbital::allocation

namespace orbital::allocation::detail {

inline void no_retain(worker::Message&) noexcept {}

void queue_notice(Pool&) noexcept;
void remove_interest(Pool&) noexcept;
void refund(Pool&, std::uint64_t) noexcept;
void schedule(Arena&, Job&) noexcept;
void initialize(Domain&, Pool&, void*, bool) noexcept;

// Establish the active backend pin before exposing the first physical user.
void activate(Storage&) noexcept;

// Fill a fresh output only on success; common allocation code establishes
// its physical use before exposing the handle to callers.
Result<void> finite_allocate(Pool&, const AllocationRequest&, std::size_t,
    Allocation&);
Result<void> window_allocate(Pool&, const AllocationRequest&, std::size_t,
    Allocation&);
// False means local capacity was unavailable; common growth may try elsewhere.
Result<bool> relocate_finite(Allocation&, const GrowthRequest&, std::size_t);
Result<void> extend_window(Window&, std::size_t);
Result<void> retain_window_range(Window&, ByteRange);
void release_window_range(Window&, ByteRange) noexcept;
std::size_t window_tail_floor(const Window&) noexcept;
void trim_window_tail(Window&, std::size_t);
Result<void> prepare_finite(Pool&, Preparation&, const AllocationRequest&, worker::Lane&);
Result<void> prepare_window(Pool&, Preparation&, const AllocationRequest&, worker::Lane&);

inline void verify(Result<void> result) noexcept { assert(result.has_value()); (void)result; }

constexpr auto maximum = std::numeric_limits<std::size_t>::max();

inline std::size_t page_size() noexcept {
  static const auto value = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  return value;
}

inline bool round_up(std::size_t value, std::size_t unit, std::size_t& result) noexcept {
  if (value > maximum - (unit - 1)) return false;
  result = (value + unit - 1) & ~(unit - 1);
  return true;
}

template<class Request>
inline Result<std::size_t> extent(const Request& request) noexcept {
  if (!request.bytes || !std::has_single_bit(request.alignment) ||
      request.bytes > maximum - request.readable_padding)
    return std::unexpected(Error::invalid_argument);
  if (request.reserved_bytes && request.reserved_bytes < request.bytes + request.readable_padding)
    return std::unexpected(Error::invalid_argument);
  return request.bytes + request.readable_padding;
}

template<bool Ordered>
bool claim_bytes(Arena& arena, std::uint64_t amount) noexcept {
  constexpr auto load_order = Ordered ? std::memory_order_seq_cst : std::memory_order_relaxed;
  constexpr auto claim_order = Ordered ? std::memory_order_seq_cst : std::memory_order_acq_rel;
  auto charged = arena.charged_bytes.load(load_order);
  do {
    if (amount > arena.byte_limit || charged > arena.byte_limit - amount) return false;
  } while (!arena.charged_bytes.compare_exchange_weak(charged, charged + amount,
      claim_order, load_order));
  return true;
}

[[gnu::noinline]] bool refused_claim(Pool&, std::uint64_t) noexcept;

[[gnu::always_inline]] inline bool claim(Pool& pool, std::uint64_t amount) noexcept {
  if (claim_bytes<false>(*pool.arena, amount)) return true;
  return refused_claim(pool, amount);
}

void renumber_idle(PoolState&) noexcept;

inline void add_idle(Window& window) noexcept {
  if (window.previous_idle || window.trim_pending || window.uses[0].users.load(std::memory_order_acquire) ||
      window.uses.domain.queued.load(std::memory_order_acquire)) return;
  auto& state = *window.uses.domain.pool->internal;
  if (state.idle_order == std::numeric_limits<std::uint64_t>::max()) renumber_idle(state);
  window.idle_order = ++state.idle_order;
  auto& head = state.idle_windows;
  window.next_idle = head;
  window.previous_idle = &head;
  if (head) head->previous_idle = &window.next_idle;
  head = &window;
  auto& size_head = window.size_entry->second.head;
  window.next_size = size_head;
  window.previous_size = &size_head;
  if (size_head) size_head->previous_size = &window.next_size;
  size_head = &window;
}

inline void remove_idle(Window& window) noexcept {
  if (!window.previous_idle) return;
  *window.previous_idle = window.next_idle;
  if (window.next_idle) window.next_idle->previous_idle = window.previous_idle;
  window.next_idle = nullptr;
  window.previous_idle = nullptr;
  *window.previous_size = window.next_size;
  if (window.next_size) window.next_size->previous_size = window.previous_size;
  window.next_size = nullptr;
  window.previous_size = nullptr;
}

inline void add_available(Group& group) noexcept {
  if (group.previous_available || group.retiring || group.control_preparing || !group.geometry->has_identity()) return;
  auto& head = group.uses.domain.pool->internal->available_groups;
  group.next_available = head;
  group.previous_available = &head;
  if (head) head->previous_available = &group.next_available;
  head = &group;
}

inline void remove_available(Group& group) noexcept {
  for (auto& candidate : group.uses.domain.pool->internal->ready_candidates)
    if (candidate.group == &group) candidate.group = nullptr;
  if (!group.previous_available) return;
  *group.previous_available = group.next_available;
  if (group.next_available) group.next_available->previous_available = group.previous_available;
  group.next_available = nullptr;
  group.previous_available = nullptr;
}

template<class T> void erase_owner(std::vector<std::unique_ptr<T>>& owners, T& retired) noexcept {
  const auto index = retired.owner_index;
  assert(index < owners.size() && owners[index].get() == &retired);
  if (index != owners.size() - 1) {
    owners[index].swap(owners.back());
    owners[index]->owner_index = index;
  }
  owners.pop_back();
}

} // namespace orbital::allocation::detail
