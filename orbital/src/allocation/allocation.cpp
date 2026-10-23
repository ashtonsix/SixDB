#include <orbital/detail/allocation/capture.h>
// Allocation/Hold lifetimes, growth and Pool-wide trim/close coordination.

#include "internal.h"
#include <algorithm>
#include <cstring>
#include <utility>

namespace orbital::allocation::detail {

ReleaseHint hint(Storage& use) noexcept {
  if (route(use)->finite) return static_cast<Group*>(route(use)->context)->last_use;
  return static_cast<Window*>(route(use)->context)->hint;
}

ReleaseHint foreign_hint(Storage& use) noexcept {
  if (route(use)->finite) return static_cast<Group*>(route(use)->context)->foreign_use;
  return static_cast<Window*>(route(use)->context)->foreign_hint;
}

bool supplied(const ReleaseHint& value) noexcept {
  return value.temperature.has_value() ||
      value.last_access != std::chrono::steady_clock::time_point{};
}

void merge_hint(ReleaseHint& target, const ReleaseHint& incoming) noexcept {
  if (!supplied(incoming)) return;
  if (incoming.last_access != std::chrono::steady_clock::time_point{} &&
      incoming.last_access < target.last_access) return;
  if (incoming.temperature) target.temperature = incoming.temperature;
  if (incoming.last_access != std::chrono::steady_clock::time_point{})
    target.last_access = incoming.last_access;
}

void merge_hint(Storage& use, const ReleaseHint& incoming, bool foreign = false) noexcept {
  if (route(use)->finite) {
    auto& group = *static_cast<Group*>(route(use)->context);
    merge_hint(foreign ? group.foreign_use : group.last_use, incoming);
  } else {
    auto& window = *static_cast<Window*>(route(use)->context);
    merge_hint(foreign ? window.foreign_hint : window.hint, incoming);
  }
}

void clear_foreign_hint(Storage& use) noexcept {
  if (route(use)->finite) static_cast<Group*>(route(use)->context)->foreign_use = {};
  else static_cast<Window*>(route(use)->context)->foreign_hint = {};
}

// An open Pool keeps its Arena pin across idle moments, so allocate/release
// churn takes no atomic. The last use still drops it when the Pool is closing
// or the pin is the Arena's only reference; close drops an idle pin.
[[gnu::always_inline]] inline bool unpin(const Pool& pool) noexcept {
  return pool.state != Pool::State::open || pool.internal->close_requested ||
      pool.arena->references.load(std::memory_order_acquire) == 1;
}

void activate(Storage& use) noexcept {
  auto& domain = *route(use);
  assert(worker::on_lane(*domain.pool->lane) && !use.users.load(std::memory_order_relaxed));
  // One Arena pin per Pool while any finite or virtual use is live or returning.
  auto& state = *domain.pool->internal;
  if (!state.active_uses++ && !state.active_lifetime)
    state.active_lifetime = Ref<Arena>::retain(*domain.pool->arena);
  use.users.store(1, std::memory_order_relaxed);
}

[[gnu::always_inline]] inline void recycle(Storage& use, bool notify = true) noexcept {
  auto& domain = *route(use);
  auto& pool = *domain.pool;
  assert(worker::on_lane(*pool.lane) && !use.users.load(std::memory_order_relaxed));
  // Detach before identity reuse or notification can grant the same record.
  // The old pin outlives every touch here, including a final local Hold release.
  Ref<Arena> retained;
  auto& state = *pool.internal;
  assert(state.active_uses);
  if (!--state.active_uses && unpin(pool)) [[unlikely]] retained = std::move(state.active_lifetime);
  if (domain.finite) {
    auto& group = *static_cast<Group*>(domain.context);
    group.geometry->give(use.identity());
    add_available(group);
  } else add_idle(*static_cast<Window*>(domain.context));
  if (notify) pulse_owner(pool.capacity_changed);
}

bool returned(Domain& domain) noexcept {
  if (!domain.finite) return static_cast<Window*>(domain.context)->uses[0].users.load(std::memory_order_acquire) == returning;
  auto& group = *static_cast<Group*>(domain.context);
  return group.advice_pending || std::any_of(group.returned.begin(), group.returned.end(), [](auto word) { return word != 0; });
}

void apply_return(Storage& use) noexcept {
  if (use.users.load(std::memory_order_acquire) != returning) return;
  if (!route(use)->finite) {
    merge_hint(use, foreign_hint(use));
    clear_foreign_hint(use);
    static_cast<Window*>(route(use)->context)->foreign_hint_pending.store(false, std::memory_order_relaxed);
  }
  use.users.store(0, std::memory_order_release);
  recycle(use, false);
  ++route(use)->pool->internal->counts.foreign_returns;
}

void returns_handle(worker::Message& message, worker::Lane&) noexcept {
  auto& domain = *static_cast<Domain*>(message.context);
  {
    std::unique_lock lock(domain.mutex, std::try_to_lock);
    if (!lock) return;
    if (domain.finite) {
      auto& group = *static_cast<Group*>(domain.context);
      merge_hint(group.last_use, group.foreign_use);
      group.foreign_use = {}; group.advice_pending = false;
      for (std::size_t i = 0; i < group.returned.size(); ++i) {
        auto word = std::exchange(group.returned[i], 0);
        while (word) {
          const auto bit = std::countr_zero(word);
          apply_return(group.uses[i * 64 + bit]);
          word &= word - 1;
        }
      }
    } else apply_return(static_cast<Window*>(domain.context)->uses[0]);
  }
  pulse(domain.pool->capacity_changed);
}

void returns_release(worker::Message& message) noexcept {
  auto& domain = *static_cast<Domain*>(message.context);
  Ref<Arena> retained;
  { std::unique_lock lock(domain.mutex, std::try_to_lock);
    if (!lock) {
      // Keep the existing queue lifetime while a publisher is preempted.
      worker::post(*domain.pool->lane, domain.returns);
      return;
    }
    retained = std::move(domain.lifetime);
    domain.queued.store(false, std::memory_order_release);
    if (returned(domain)) {
      domain.queued.store(true, std::memory_order_release);
      domain.lifetime = Ref<Arena>::retain(*domain.pool->arena);
      worker::post(*domain.pool->lane, domain.returns);
    }
  }
  if (!domain.finite) add_idle(*static_cast<Window*>(domain.context));
  pulse(domain.pool->capacity_changed);
}

const worker::MessageOps return_ops{no_retain, returns_handle, returns_release};

void initialize(Domain& domain, Pool& pool, void* context, bool finite) noexcept {
  domain.pool = &pool;
  domain.context = context;
  domain.finite = finite;
  domain.returns.context = &domain;
  domain.returns.ops = &return_ops;
}

// Caller holds Domain::mutex. Advice and final returns share one route/lifetime.
void post_returns(Domain& domain) noexcept {
  if (domain.queued.load(std::memory_order_relaxed)) return;
  domain.queued.store(true, std::memory_order_release);
  domain.lifetime = Ref<Arena>::retain(*domain.pool->arena);
  worker::post(*domain.pool->lane, domain.returns);
}

void drop(Storage& use, ReleaseHint value) noexcept {
  auto state = use.users.state.load(std::memory_order_relaxed);
  auto& domain = *route(use, state);
  const bool local = worker::on_lane(*domain.pool->lane);
  if (supplied(value)) {
    if (local) merge_hint(use, value);
    else {
      std::lock_guard lock(domain.mutex);
      merge_hint(use, value, true);
      if (domain.finite) {
        static_cast<Group*>(domain.context)->advice_pending = true;
        post_returns(domain);
      } else static_cast<Window*>(route(use)->context)->foreign_hint_pending.store(true, std::memory_order_release);
    }
  }
  if (local && (state & UseState::user_mask) == 1) {
    // Sole user on the owner Lane: no other holder can race this final store.
    use.users.state.store(state & ~UseState::user_mask, std::memory_order_relaxed);
  } else for (;;) {
    const auto users = state & UseState::user_mask;
    assert(users && users < returning);
    if (users == 1 && !local) {
      std::lock_guard lock(domain.mutex);
      state = (state & ~UseState::user_mask) | 1;
      const auto next = (state & ~UseState::user_mask) | returning;
      if (!use.users.state.compare_exchange_strong(state, next, std::memory_order_acq_rel))
        continue;
      if (domain.finite) {
        auto& group = *static_cast<Group*>(domain.context);
        const auto index = state >> 24;
        group.returned[index / 64] |= std::uint64_t{1} << (index % 64);
      }
      post_returns(domain);
      return;
    }
    const auto next = (state & ~UseState::user_mask) | (users == 1 ? 0 : users - 1);
    if (use.users.state.compare_exchange_weak(state, next, std::memory_order_acq_rel,
        std::memory_order_relaxed)) break;
  }
  if ((state & UseState::user_mask) != 1) return;
  if (local) {
    // Foreign releases with explicit hints can precede this local final release.
    if (!domain.finite && static_cast<Window*>(route(use)->context)->foreign_hint_pending.exchange(false, std::memory_order_acq_rel)) {
      // Final user retirement observes every preceding foreign hint write.
      // Other identities may publish concurrently, but cannot touch this hint.
      merge_hint(use, foreign_hint(use)); clear_foreign_hint(use);
    }
    recycle(use);
  }
}

} // namespace orbital::allocation::detail

namespace orbital::allocation {
using namespace detail;

void retain_ref(Arena& arena) noexcept { arena.references.fetch_add(1, std::memory_order_relaxed); }

void release_ref(Arena& arena) noexcept {
  if (arena.references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete &arena;
}

Result<Allocation> allocate(Pool& pool, const AllocationRequest& request) {
  if (!pool.lane || !worker::on_lane(*pool.lane)) [[unlikely]] return std::unexpected(Error::conflict);
  auto& state = *pool.internal;
  if (pool.state != Pool::State::open || state.close_requested) [[unlikely]]
    return std::unexpected(Error::unavailable);
  auto size = extent(request);
  if (!size) [[unlikely]] return std::unexpected(size.error());
  const auto reserve = std::max(*size, request.reserved_bytes);
  Result<Allocation> result(std::in_place);
  auto& value = *result;
  const auto cls = select_class(state.page, reserve, request.alignment, state.floor_class);
  if (cls < finite_classes && take_head(state, cls, request, value)) [[likely]] {
    // Owner fast path: the Pool pin and first use, without re-routing the Storage.
    if (!state.active_uses++ && !state.active_lifetime) [[unlikely]]
      state.active_lifetime = Ref<Arena>::retain(*pool.arena);
    value.storage->users.store(1, std::memory_order_relaxed);
  } else {
    auto acquired = cls < finite_classes ? finite_allocate(pool, request, cls, value)
        : window_allocate(pool, request, *size, value);
    if (!acquired) {
      // Returning the same result permits direct payload construction in the caller's slot.
      result = std::unexpected(acquired.error());
      return result;
    }
    activate(*value.storage);
  }
  if (request.initialization == Initialization::zero) std::memset(value.address, 0, *size);
  else if (request.readable_padding) std::memset(value.address + request.bytes, 0, request.readable_padding);
  ++state.counts.allocations;
  return result;
}

Result<void> prepare(Pool& pool, Preparation& request,
    const AllocationRequest& allocation, worker::Lane& preparer) {
  if (!pool.lane || !worker::on_lane(*pool.lane)) return std::unexpected(Error::conflict);
  if (pool.state != Pool::State::open || pool.internal->close_requested)
    return std::unexpected(Error::unavailable);
  if (request.state != Preparation::State::idle || request.backend)
    return std::unexpected(Error::invalid_argument);
  const auto target = preparer.owner.load(std::memory_order_acquire);
  if (!target) return std::unexpected(Error::unavailable);
  if (&preparer == pool.lane || target == pool.lane->owner.load(std::memory_order_acquire))
    return std::unexpected(Error::invalid_argument);
  const auto size = extent(allocation);
  if (!size) return std::unexpected(size.error());
  const auto reserve = std::max(*size, allocation.reserved_bytes);
  const auto cls = select_class(page_size(), reserve, allocation.alignment);
  return cls < finite_classes
      ? prepare_finite(pool, request, allocation, cls, preparer)
      : prepare_window(pool, request, allocation, preparer);
}

void cancel(Preparation& preparation) noexcept {
  // Terminal records are self-contained; their former Pool may already be gone.
  if (preparation.state != Preparation::State::pending &&
      preparation.state != Preparation::State::repair_wait && preparation.state != Preparation::State::ready) return;
  assert(preparation.owner && preparation.owner->lane && worker::on_lane(*preparation.owner->lane));
  if (preparation.state == Preparation::State::pending || preparation.state == Preparation::State::repair_wait) {
    preparation.discard = true; // Backend still owns its accepted physical work.
  } else {
    release(preparation.value);
    preparation.state = Preparation::State::cancelled;
    preparation.error = Error::rejected;
    preparation.owner = nullptr;
  }
}

Result<Allocation> take(Preparation& preparation) {
  // Inspect self-contained status before any pointer to the former Pool.
  if (preparation.state != Preparation::State::ready)
    return std::unexpected(preparation.state == Preparation::State::pending
        ? Error::unavailable : preparation.state == Preparation::State::taken
        ? Error::invalid_argument : preparation.error);
  if (!preparation.owner || !preparation.owner->lane ||
      !worker::on_lane(*preparation.owner->lane)) return std::unexpected(Error::conflict);
  preparation.state = Preparation::State::taken;
  preparation.owner = nullptr;
  return std::move(preparation.value);
}

std::size_t capacity(const Allocation& allocation) noexcept {
  if (!allocation) return 0;
  const auto& domain = *route(*allocation.storage);
  if (domain.finite)
    return static_cast<Group*>(domain.context)->geometry->capacity();
  return static_cast<Window*>(domain.context)->prepared;
}

std::size_t reserved_bytes(const Allocation& allocation) noexcept {
  if (!allocation) return 0;
  const auto& domain = *route(*allocation.storage);
  if (domain.finite) return capacity(allocation);
  return static_cast<Window*>(domain.context)->reserved;
}

Result<void> grow(Allocation& allocation, const GrowthRequest& request) {
  if (!allocation) return std::unexpected(Error::invalid_argument);
  auto& use = *allocation.storage;
  auto& pool = *route(use)->pool;
  if (!pool.lane || !worker::on_lane(*pool.lane)) return std::unexpected(Error::conflict);
  if (pool.state != Pool::State::open || pool.internal->close_requested)
    return std::unexpected(Error::unavailable);
  auto size = extent(request);
  if (!size || request.bytes < allocation.size) return std::unexpected(Error::invalid_argument);
  // While the move-only Allocation is live, it owns exactly one user. Every
  // additional user is a Hold; foreign release publishes completion here.
  const bool held = use.users.load(std::memory_order_acquire) > 1;
  const bool aligned = reinterpret_cast<std::uintptr_t>(allocation.address) % request.alignment == 0;
  const auto previous_extent = allocation.size + allocation.readable_padding;
  const auto needed = std::max({*size, request.reserved_bytes, previous_extent});
  if (!route(use)->finite) {
    const auto& window = *static_cast<Window*>(route(use)->context);
    if (window.trim_pending && *size > window.prepared)
      return std::unexpected(Error::conflict);
  }
  if (!aligned || needed > reserved_bytes(allocation)) {
    if (held) return std::unexpected(Error::conflict);
    // A finite Home holds exactly one class; larger extents move to another
    // Home or a Window. Identity is private, not a public address promise.
    // Allocate uninitialized replacement bytes. Fresh-allocation padding policy
    // does not apply to growth; interpretation and initialization belong to its writer.
    auto replacement = allocate(pool, {.bytes = std::max(*size, previous_extent),
        .reserved_bytes = needed, .alignment = request.alignment});
    if (!replacement) return std::unexpected(replacement.error());
    std::memcpy(replacement->address, allocation.address, previous_extent);
    replacement->size = request.bytes;
    replacement->readable_padding = request.readable_padding;
    allocation = std::move(*replacement);
    ++pool.internal->counts.relocations;
  } else {
    if (!route(use)->finite) {
      auto& window = *static_cast<Window*>(route(use)->context);
      if (*size > window.prepared) {
        auto extended = extend_window(window, *size);
        if (!extended) return std::unexpected(extended.error());
      }
    }
    allocation.size = request.bytes;
    allocation.readable_padding = request.readable_padding;
  }
  if (!route(*allocation.storage)->finite) {
    auto& window = *static_cast<Window*>(route(*allocation.storage)->context);
    if (window.tail_released)
      window.tail_extent = std::max(window.tail_extent, allocation.size + allocation.readable_padding);
  }
  ++pool.internal->counts.growths;
  return {};
}

Result<void> relinquish_tail(Allocation& allocation, std::size_t bytes, std::size_t padding) {
  if (!allocation) return std::unexpected(Error::invalid_argument);
  auto& domain = *route(*allocation.storage);
  auto& pool = *domain.pool;
  if (!pool.lane || !worker::on_lane(*pool.lane)) return std::unexpected(Error::conflict);
  if (pool.state != Pool::State::open || pool.internal->close_requested)
    return std::unexpected(Error::unavailable);
  if (bytes > allocation.size || padding > allocation.size + allocation.readable_padding - bytes)
    return std::unexpected(Error::invalid_argument);
  allocation.size = bytes;
  allocation.readable_padding = padding;
  if (!domain.finite) {
    auto& window = *static_cast<Window*>(domain.context);
    window.tail_extent = bytes + padding;
    window.tail_released = true;
  }
  return {};
}

namespace {
bool add_user(Storage& use, std::uint32_t state) noexcept {
  for (;;) {
    const auto users = state & UseState::user_mask;
    if (!users || users >= returning - 1) return false;
    // A checked low-field increment cannot carry into the immutable identity.
    if (use.users.state.compare_exchange_weak(state, state + 1,
        std::memory_order_relaxed, std::memory_order_relaxed)) return true;
  }
}
}

Result<Hold> hold(const Allocation& allocation, ByteRange footprint) {
  if (!allocation) return std::unexpected(Error::conflict);
  auto& use = *allocation.storage;
  auto state = use.users.state.load(std::memory_order_relaxed);
  if (!worker::on_lane(*route(use, state)->pool->lane))
    return std::unexpected(Error::conflict);
  if (!footprint.size || footprint.offset > allocation.size + allocation.readable_padding ||
      footprint.size > allocation.size + allocation.readable_padding - footprint.offset)
    return std::unexpected(Error::invalid_argument);
  auto& domain = *route(use, state);
  if (!domain.finite) {
    auto retained = retain_window_range(*static_cast<Window*>(domain.context), footprint);
    if (!retained) return std::unexpected(retained.error());
  }
  if (!add_user(use, state)) {
    if (!domain.finite) release_window_range(*static_cast<Window*>(domain.context), footprint);
    return std::unexpected(Error::capacity);
  }
  Hold result;
  result.storage = &use;
  result.address = allocation.address + footprint.offset;
  result.footprint = footprint;
  return result;
}

namespace detail {
Result<void> ready_for_capture(const Allocation& allocation) noexcept {
  if (!allocation) return std::unexpected(Error::conflict);
  auto& use = *allocation.storage;
  const auto state = use.users.state.load(std::memory_order_relaxed);
  if (!worker::on_lane(*route(use, state)->pool->lane))
    return std::unexpected(Error::conflict);
  if (!(allocation.size + allocation.readable_padding))
    return std::unexpected(Error::invalid_argument);
  const auto users = state & UseState::user_mask;
  // Preserve capture's previous full-Hold refusal at saturation. Transfer itself
  // needs no extra user; relaxing this boundary would be a separate API change.
  if (!users || users >= returning - 1) return std::unexpected(Error::capacity);
  return {};
}

} // namespace detail

Result<Hold> retain(const Hold& original) {
  if (!original) return std::unexpected(Error::invalid_argument);
  auto& use = *original.storage;
  auto state = use.users.state.load(std::memory_order_relaxed);
  if (!worker::on_lane(*route(use, state)->pool->lane))
    return std::unexpected(Error::conflict);
  auto& domain = *route(use, state);
  if (!domain.finite) {
    auto retained = retain_window_range(*static_cast<Window*>(domain.context), original.footprint);
    if (!retained) return std::unexpected(retained.error());
  }
  if (!add_user(use, state)) {
    if (!domain.finite) release_window_range(*static_cast<Window*>(domain.context), original.footprint);
    return std::unexpected(Error::capacity);
  }
  Hold result;
  result.storage = original.storage;
  result.address = original.address;
  result.footprint = original.footprint;
  return result;
}

void release(Allocation& allocation, ReleaseHint value) noexcept {
  if (!allocation.storage) return;
  auto& use = *std::exchange(allocation.storage, nullptr);
  allocation.address = nullptr; allocation.size = allocation.readable_padding = 0;
  // Owner fast path: the sole use of a finite Home, released on its own Lane
  // without advice. No other holder exists to race the final store.
  const auto state = use.users.state.load(std::memory_order_relaxed);
  if ((state & UseState::user_mask) == 1 && !supplied(value)) [[likely]] {
    auto& domain = *route(use, state);
    auto& pool = *domain.pool;
    if (domain.finite && worker::on_lane(*pool.lane)) [[likely]] {
      // The checks above are recycle()'s preconditions; repeat none of them.
      use.users.state.store(state & ~UseState::user_mask, std::memory_order_relaxed);
      auto& internal = *pool.internal;
      Ref<Arena> retained;
      if (!--internal.active_uses && unpin(pool)) [[unlikely]] retained = std::move(internal.active_lifetime);
      auto& group = *static_cast<Group*>(domain.context);
      group.geometry->give(state >> 24);
      if (!group.previous_available) [[unlikely]] add_available(group);
      ++pool.capacity_changed.generation;
      if (pool.capacity_changed.waiters) [[unlikely]] notify_waiters(pool.capacity_changed);
      return;
    }
  }
  drop(use, value);
}

void release(Hold& hold, ReleaseHint value) noexcept {
  if (!hold.storage) return;
  auto* storage = std::exchange(hold.storage, nullptr);
  auto& domain = *route(*storage);
  if (!domain.finite) release_window_range(*static_cast<Window*>(domain.context), hold.footprint);
  hold.address = nullptr; hold.footprint = {};
  drop(*storage, value);
}

Allocation& Allocation::operator=(Allocation&& other) noexcept {
  if (this == &other) return *this;
  release(*this);
  storage = std::exchange(other.storage, nullptr);
  address = std::exchange(other.address, nullptr);
  size = std::exchange(other.size, 0); readable_padding = std::exchange(other.readable_padding, 0);
  return *this;
}

Hold::Hold(Hold&& other) noexcept { *this = std::move(other); }

Hold& Hold::operator=(Hold&& other) noexcept {
  if (this == &other) return *this;
  release(*this);
  storage = std::exchange(other.storage, nullptr);
  address = std::exchange(other.address, nullptr); footprint = std::exchange(other.footprint, {});
  return *this;
}

Hold::~Hold() { release(*this); }

Progress trim_cold(Pool& pool, std::uint64_t keep_bytes) {
  if (!pool.lane || !worker::on_lane(*pool.lane)) return Progress::fail(Error::conflict);
  if (std::exchange(pool.internal->trim_failed, false)) return Progress::fail(Error::io);
  const auto observed = pool.capacity_changed.generation;
  bool pending = false;
  std::uint64_t idle = 0;
  for (auto& group : pool.internal->groups) {
    if (group->trim_pending || group->control_preparing) { pending = true; continue; }
    if (group->retiring) continue;
    if (group->geometry->has_trim()) {
      auto success = group->geometry->trim_success();
      std::fill(success.begin(), success.end(), 0);
      group->trim_pending = true;
      group->trim_lifetime = Ref<Arena>::retain(*pool.arena);
      schedule(*pool.arena, group->job); pending = true;
      continue;
    }
    verify(group->geometry->live_pages(group->live));
    for (std::size_t p = 0; p < group->geometry->payload_pages(); ++p)
      if (group->geometry->mapped(p) && !(group->live[p / 64] & (std::uint64_t{1} << (p % 64))))
        idle += group->geometry->native_page();
  }
  for (auto& window : pool.internal->windows) {
    if (window->trim_pending || window->preparation) { pending = true; continue; }
    if (window->address && !window->uses[0].users.load(std::memory_order_acquire) &&
        !window->uses.domain.queued.load(std::memory_order_acquire)) idle += window->charged;
    else if (window->tail_released) {
      const auto floor = window_tail_floor(*window);
      if (floor < window->charged) idle += window->charged - floor;
    }
  }
  // One Pool-wide floor, shared by finite pages and virtual windows. Prefer
  // cold groups, then permit warmer inventory when the requested trim needs it.
  for (unsigned pass = 0; pass < 2; ++pass) {
    for (auto& group : pool.internal->groups) {
      if (group->trim_pending || group->control_preparing) continue;
      if (group->retiring) {
        if (pass) continue;
        group->trim_pending = true;
        group->trim_lifetime = Ref<Arena>::retain(*pool.arena);
        schedule(*pool.arena, group->job); pending = true;
        continue;
      }
      const bool cold = group->last_use.temperature.value_or(Temperature::cold) == Temperature::cold;
      if ((pass == 0) != cold) continue;
      bool empty = !group->uses.domain.queued.load(std::memory_order_acquire);
      for (std::size_t i = 0; empty && i < finite_homes; ++i)
        empty = group->uses[i].users.load(std::memory_order_acquire) == 0;
      const auto mapped = group->geometry->mapped_pages() * group->geometry->native_page();
      if (empty && !group->preparations && mapped <= idle && idle - mapped >= keep_bytes) {
        remove_available(*group);
        group->retire_charge = group->metadata_charge + mapped;
        group->retire_mapped = mapped;
        group->retire_reserved = group->geometry->reserved_bytes();
        group->retiring = group->trim_pending = true;
        group->trim_lifetime = Ref<Arena>::retain(*pool.arena);
        schedule(*pool.arena, group->job); idle -= mapped; pending = true;
        continue;
      }
      const auto page = group->geometry->native_page();
      auto membership = group->geometry->trim_membership();
      for (std::size_t p = 0; p < group->geometry->payload_pages() && idle > keep_bytes; ++p) {
        if (!group->geometry->mapped(p) || (group->live[p / 64] & (std::uint64_t{1} << (p % 64)))) continue;
        if (idle - page < keep_bytes) break;
        membership[p / 64] |= std::uint64_t{1} << (p % 64);
        idle -= page;
      }
      if (group->geometry->has_trim()) {
        verify(group->geometry->begin_trim());
        auto success = group->geometry->trim_success();
        std::fill(success.begin(), success.end(), 0);
        group->trim_pending = true;
        group->trim_lifetime = Ref<Arena>::retain(*pool.arena);
        schedule(*pool.arena, group->job); pending = true;
      }
    }
    for (auto& window : pool.internal->windows) {
      if (window->trim_pending || window->preparation || !window->address) continue;
      const bool cold = window->hint.temperature.value_or(Temperature::cold) == Temperature::cold;
      if ((pass == 0) != cold) continue;
      if (window->uses[0].users.load(std::memory_order_acquire) ||
          window->uses.domain.queued.load(std::memory_order_acquire)) {
        if (!window->tail_released || idle <= keep_bytes) continue;
        const auto floor = window_tail_floor(*window);
        if (floor >= window->charged) continue;
        const auto allowance = (idle - keep_bytes) / page_size() * page_size();
        const auto reclaim = std::min(window->charged - floor, allowance);
        if (!reclaim) continue;
        trim_window_tail(*window, window->charged - reclaim);
        idle -= reclaim; pending = true;
        continue;
      }
      if (idle < window->charged || idle - window->charged < keep_bytes) continue;
      idle -= window->charged;
      remove_idle(*window);
      window->uses[0].users.store(returning, std::memory_order_release);
      window->trim_pending = true;
      window->trim_lifetime = Ref<Arena>::retain(*pool.arena);
      schedule(*pool.arena, window->job); pending = true;
    }
  }
  return pending ? Progress::wait(pool.capacity_changed, observed) : Progress::done();
}

Progress close(Pool& pool) {
  if (pool.state == Pool::State::closed) return Progress::done();
  if (!pool.lane || !worker::on_lane(*pool.lane)) return Progress::fail(Error::conflict);
  pool.internal->close_requested = true;
  // An idle Pool's cached pin ends with close. The caller keeps the Arena alive
  // through close, so this is not its last reference.
  if (!pool.internal->active_uses && pool.internal->active_lifetime &&
      pool.arena->references.load(std::memory_order_acquire) > 1)
    pool.internal->active_lifetime.reset();
  if (pool.state == Pool::State::open) {
    {
      std::unique_lock lock(pool.arena->state->registry, std::try_to_lock);
      if (!lock) {
        const auto observed = pool.capacity_changed.generation;
        queue_notice(pool);
        return Progress::wait(pool.capacity_changed, observed);
      }
      pool.state = Pool::State::closing;
      remove_interest(pool);
    }
    pulse(pool.capacity_changed);
  }
  const auto observed = pool.capacity_changed.generation;
  if (pool.internal->preparation_debt) return Progress::fail(Error::io);
  if (pool.internal->preparations) return Progress::wait(pool.capacity_changed, observed);
  for (auto& group : pool.internal->groups) {
    if (group->trim_pending || group->uses.domain.queued.load(std::memory_order_acquire)) return Progress::wait(pool.capacity_changed, observed);
    if (group->retiring) continue;
    for (std::size_t i = 0; i < finite_homes; ++i)
      if (group->uses[i].users.load(std::memory_order_acquire)) return Progress::wait(pool.capacity_changed, observed);
  }
  for (auto& window : pool.internal->windows)
    if (window->preparation || (!window->trim_pending && (window->uses[0].users.load(std::memory_order_acquire) ||
        window->uses.domain.queued.load(std::memory_order_acquire)))) return Progress::wait(pool.capacity_changed, observed);
  auto trimming = trim_cold(pool, 0);
  if (trimming.kind() != Progress::Kind::done) return trimming;
  std::uint64_t metadata = 0;
  for (auto& group : pool.internal->groups) metadata += group->metadata_charge;
  pool.internal->groups.clear(); // No active/queued homes, trims or native descriptor borrows.
  for (auto& window : pool.internal->windows) metadata += window->metadata_charge;
  pool.internal->windows.clear();
  refund(pool, metadata);
  if (pool.internal->notice_pending.load(std::memory_order_acquire) || pool.capacity_changed.waiters)
    return Progress::wait(pool.capacity_changed, observed);
  std::unique_lock lock(pool.arena->state->registry, std::try_to_lock);
  if (!lock) {
    queue_notice(pool);
    return Progress::wait(pool.capacity_changed, observed);
  }
  pool.lane = nullptr; pool.capacity_changed.lane = nullptr;
  pool.state = Pool::State::closed;
  return Progress::done();
}

Statistics statistics(const Pool& pool) noexcept {
  assert(pool.state == Pool::State::closed || worker::on_lane(*pool.lane));
  auto result = pool.internal->counts;
  result.arena_pins = bool(pool.internal->active_lifetime);
  result.metadata_bytes += pool.internal->preparation_metadata;
  for (auto& group : pool.internal->groups) {
    if (group->control_preparing) {
      result.metadata_bytes += group->metadata_charge;
      result.preparing_reserved_bytes += group->geometry->native_page() *
          group->geometry->payload_pages();
      continue;
    }
    if (group->retiring) {
      result.mapped_bytes += group->retire_mapped;
      result.reserved_bytes += group->retire_reserved;
      result.metadata_bytes += group->metadata_charge;
      result.idle_bytes += group->retire_mapped;
      continue;
    }
    result.preparing_bytes += group->geometry->preparing_pages() * group->geometry->native_page();
    result.mapped_bytes += group->geometry->mapped_pages() * group->geometry->native_page();
    result.reserved_bytes += group->geometry->reserved_bytes();
    result.metadata_bytes += group->metadata_charge;
    verify(group->geometry->live_pages(group->live));
    for (std::size_t p = 0; p < group->geometry->payload_pages(); ++p)
      if ((group->geometry->mapped(p) || group->geometry->trimming(p)) &&
          !(group->live[p / 64] & (std::uint64_t{1} << (p % 64))))
        result.idle_bytes += group->geometry->native_page();
  }
  result.uncertain_bytes += pool.internal->preparation_uncertain;
  assert(result.preparing_bytes >= pool.internal->preparation_uncertain);
  result.preparing_bytes -= pool.internal->preparation_uncertain;
  for (auto& window : pool.internal->windows) {
    result.mapped_bytes += window->prepared;
    if (window->preparation) {
      result.preparing_bytes += window->charged - window->prepared;
      if (!window->mapping) result.preparing_reserved_bytes += window->mapping_bytes;
      else result.reserved_bytes += window->mapping_bytes;
    } else {
      result.uncertain_bytes += window->charged - window->prepared;
      if (window->mapping) result.reserved_bytes += window->mapping_bytes;
    }
    result.metadata_bytes += window->metadata_charge;
    if (!window->preparation && !window->uses[0].users.load(std::memory_order_acquire)) result.idle_bytes += window->charged;
    else if (!window->preparation && window->tail_released) {
      const auto floor = window_tail_floor(*window);
      if (floor < window->charged) result.idle_bytes += window->charged - floor;
    }
  }
  return result;
}

} // namespace orbital::allocation
