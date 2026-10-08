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
#include <mutex>
#include <vector>
#include <unistd.h>

namespace orbital::allocation {
namespace detail { struct Domain; struct Group; struct Window; }
// Physical retention is independent of the dense one-byte binding plane.
struct Storage {
  detail::Domain* domain{};
  Ref<Arena> lifetime; // One pin per live reservation, shared by its physical uses.
  std::size_t held_extent{};
  std::atomic<std::uint32_t> users{};
  std::uint16_t identity{};
  std::atomic<bool> foreign_hint_pending{};
};

namespace detail {
constexpr std::uint32_t returning = std::uint32_t{1} << 31;
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

struct Group {
  Domain domain;
  std::size_t owner_index{}; // Pool owning-vector position, not an allocation identity.
  Group* next_available{};
  Group** previous_available{};
  std::unique_ptr<FiniteGroup> geometry;
  std::array<Storage, group_identities> uses{};
  std::array<ReleaseHint, group_identities> hints{}, foreign_hints{};
  std::vector<std::uint64_t> live;
  std::array<std::uint64_t, (group_identities + 63) / 64> returned{};
  std::vector<PageSpan> trimming;
  std::vector<bool> discarded;
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

struct Window {
  Domain domain;
  std::size_t owner_index{}; // Pool owning-vector position, not an allocation identity.
  Storage use;
  Window* next_idle{};
  Window** previous_idle{};
  std::byte* mapping{};
  std::size_t mapping_bytes{};
  std::byte* address{};
  // prepared is confirmed accessible; charged also covers uncertain pages
  // after a failed preparation, until retry or actual unmapping.
  std::size_t reserved{}, prepared{}, charged{};
  ReleaseHint hint, foreign_hint;
  Job job;
  worker::Message trimmed;
  Ref<Arena> trim_lifetime;
  bool trim_pending{}, discarded{};
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
  std::vector<std::unique_ptr<Window>> windows;
  Window* idle_windows{};
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

// Fill a fresh output only on success; common allocation code establishes
// its physical use before exposing the handle to callers.
Result<void> finite_allocate(Pool&, const AllocationRequest&, std::size_t,
    Allocation&);
Result<void> window_allocate(Pool&, const AllocationRequest&, std::size_t,
    Allocation&);
// False means local capacity was unavailable; common growth may try elsewhere.
Result<bool> relocate_finite(Allocation&, const AllocationRequest&, std::size_t);
Result<void> extend_window(Window&, std::size_t);
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

inline Result<std::size_t> extent(const AllocationRequest& request) noexcept {
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

inline void add_idle(Window& window) noexcept {
  if (window.previous_idle || window.trim_pending || window.use.users.load(std::memory_order_acquire) ||
      window.domain.queued.load(std::memory_order_acquire)) return;
  auto& head = window.domain.pool->internal->idle_windows;
  window.next_idle = head;
  window.previous_idle = &head;
  if (head) head->previous_idle = &window.next_idle;
  head = &window;
}

inline void remove_idle(Window& window) noexcept {
  if (!window.previous_idle) return;
  *window.previous_idle = window.next_idle;
  if (window.next_idle) window.next_idle->previous_idle = window.previous_idle;
  window.next_idle = nullptr;
  window.previous_idle = nullptr;
}

inline void add_available(Group& group) noexcept {
  if (group.previous_available || group.retiring || group.control_preparing || !group.geometry->has_identity()) return;
  auto& head = group.domain.pool->internal->available_groups;
  group.next_available = head;
  group.previous_available = &head;
  if (head) head->previous_available = &group.next_available;
  head = &group;
}

inline void remove_available(Group& group) noexcept {
  for (auto& candidate : group.domain.pool->internal->ready_candidates)
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
