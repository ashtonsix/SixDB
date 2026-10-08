// Reserved Window selection, preparation and reclamation transactions.

#include "internal.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <sys/mman.h>
#include <utility>

namespace orbital::allocation::detail {

Window::~Window() { if (mapping) (void)::munmap(mapping, mapping_bytes); }

void window_trim_run(Job& job) noexcept {
  auto& window = *static_cast<Window*>(job.context);
  window.discarded = ::munmap(window.mapping, window.mapping_bytes) == 0;
  worker::post(*window.domain.pool->lane, window.trimmed);
}

void window_trim_handle(worker::Message& message, worker::Lane&) noexcept {
  auto& window = *static_cast<Window*>(message.context);
  if (!window.discarded) { window.domain.pool->internal->trim_failed = true; return; }
  refund(*window.domain.pool, window.charged);
  window.domain.pool->internal->counts.trim_pages += window.charged / page_size();
  window.mapping = window.address = nullptr;
  window.mapping_bytes = window.prepared = window.reserved = window.charged = 0;
}

void window_trim_release(worker::Message& message) noexcept {
  auto& window = *static_cast<Window*>(message.context);
  auto retained = std::move(window.trim_lifetime);
  if (window.discarded) {
    auto& pool = *window.domain.pool;
    const auto metadata = window.metadata_charge;
    auto& windows = pool.internal->windows;
    erase_owner(windows, window);
    if (windows.empty()) std::vector<std::unique_ptr<Window>>{}.swap(windows);
    refund(pool, metadata);
    pulse(pool.capacity_changed);
    return;
  }
  window.trim_pending = false;
  window.use.users.store(0, std::memory_order_release);
  add_idle(window);
  pulse(window.domain.pool->capacity_changed);
}

const worker::MessageOps window_trim_ops{no_retain, window_trim_handle, window_trim_release};

// Owner published/charged this private Window before the helper receives it.
void window_prepare_body(Window& window) noexcept {
  const auto& request = window.preparation->request;
  auto& outcome = window.preparation->outcome;
  std::size_t required{};
  const auto extent_ok = round_up(request.bytes + request.readable_padding, page_size(), required);
  assert(extent_ok); (void)extent_ok;
  if (!outcome.mapping) {
    auto* mapping = ::mmap(nullptr, window.mapping_bytes, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return;
    outcome.mapping = static_cast<std::byte*>(mapping);
    const auto alignment = std::max(page_size(), request.alignment);
    const auto raw = reinterpret_cast<std::uintptr_t>(mapping);
    outcome.address = reinterpret_cast<std::byte*>((raw + alignment - 1) & ~(alignment - 1));
  }
  if (required > outcome.prepared) {
    if (::mprotect(outcome.address + outcome.prepared, required - outcome.prepared,
        PROT_READ | PROT_WRITE) != 0) return;
    outcome.prepared = required;
  }
  if (request.initialization == Initialization::zero)
    std::memset(outcome.address, 0, request.bytes + request.readable_padding);
  else {
    // This entire Window is exclusive idle capacity. No live bytes are changed;
    // uninitialized body has no contents promise. A write ensures physical backing.
    for (std::size_t offset = 0; offset < required; offset += page_size())
      *static_cast<volatile std::byte*>(outcome.address + offset) = std::byte{0};
    if (request.readable_padding)
      std::memset(outcome.address + request.bytes, 0, request.readable_padding);
  }
  outcome.success = true;
  if (auto initialize = window.preparation->construct) {
    auto result = initialize({{outcome.address, request.bytes}, request.readable_padding}, window.preparation->context);
    outcome.success = result.has_value();
    if (!result) outcome.error = result.error();
  }
}

void window_prepare_handle(worker::Message&, worker::Lane&) noexcept {}

void window_prepare_release(worker::Message& message) noexcept {
  auto& window = *static_cast<Window*>(message.context);
  auto& pool = *window.domain.pool;
  auto& preparation = *window.preparation;
  auto retained = std::move(window.trim_lifetime);
  const auto outcome = preparation.outcome;
  window.mapping = outcome.mapping;
  window.address = outcome.address;
  window.prepared = outcome.prepared;
  window.preparation = nullptr;
  preparation.backend = nullptr;
  preparation.preparer = nullptr;
  window.job.run = window_trim_run;
  window.trimmed.ops = &window_trim_ops;
  const bool accepted = outcome.success && !preparation.discard &&
      pool.state == Pool::State::open && !pool.internal->close_requested;
  if (accepted) {
    auto& result = preparation.value;
    window.use.lifetime = Ref<Arena>::retain(*pool.arena);
    window.use.users.store(1, std::memory_order_relaxed);
    result.storage = &window.use;
    result.address = window.address;
    result.size = preparation.request.bytes;
    result.readable_padding = preparation.request.readable_padding;
    ++pool.internal->counts.allocations;
    if (preparation.reused) ++pool.internal->counts.reuses;
    preparation.state = Preparation::State::ready;
  } else {
    preparation.error = outcome.success ? Error::rejected : outcome.error;
    preparation.state = preparation.discard || pool.internal->close_requested
        ? Preparation::State::cancelled : Preparation::State::failed;
    preparation.owner = nullptr;
    if (window.mapping) add_idle(window);
    else {
      const auto charged = window.charged + window.metadata_charge;
      erase_owner(pool.internal->windows, window);
      refund(pool, charged);
    }
  }
  pulse(pool.capacity_changed);
  // This is the final access to the caller frame; backend storage is detached.
  complete(preparation.changed, accepted ? Result<void>{} : std::unexpected(preparation.error));
}

const worker::MessageOps window_prepare_ops{no_retain, window_prepare_handle, window_prepare_release};

void window_prepare_lane_handle(worker::Message& message, worker::Lane&) noexcept {
  auto& window = *static_cast<Window*>(message.context);
  window_prepare_body(window);
}

void window_prepare_lane_release(worker::Message& message) noexcept {
  auto& window = *static_cast<Window*>(message.context);
  // The preparer queue has unlinked its final use. This same stable Message
  // becomes the owner reply; worker::poll does not touch it after release.
  message.ops = &window_prepare_ops;
  worker::post(*window.domain.pool->lane, message);
}

const worker::MessageOps window_prepare_lane_ops{
    no_retain, window_prepare_lane_handle, window_prepare_lane_release};

Window* select_idle_window(Pool& pool, std::size_t reserved, std::size_t prepared,
    std::size_t alignment) noexcept {
  Window* selected = nullptr;
  std::size_t best_extra = maximum;
  bool best_prepared = false;
  for (auto* window = pool.internal->idle_windows; window; window = window->next_idle) {
    if (window->reserved < reserved ||
        reinterpret_cast<std::uintptr_t>(window->address) % alignment) continue;
    const auto extra = prepared > window->charged ? prepared - window->charged : 0;
    const bool ready = prepared <= window->prepared;
    if (!extra && ready) return window;
    if (extra < best_extra || (extra == best_extra && ready && !best_prepared)) {
      selected = window; best_extra = extra; best_prepared = ready;
    }
  }
  return selected;
}

Result<void> window_allocate(Pool& pool, const AllocationRequest& request, std::size_t size,
    Allocation& result) {
  std::size_t reserved, prepared;
  if (!round_up(std::max(size, request.reserved_bytes), page_size(), reserved) ||
      !round_up(size, page_size(), prepared)) return std::unexpected(Error::invalid_argument);
  Window* selected = select_idle_window(pool, reserved, prepared, request.alignment);
  if (selected) {
    const auto extra = prepared > selected->charged ? prepared - selected->charged : 0;
    if (extra && !claim(pool, extra)) return std::unexpected(Error::capacity);
    selected->charged += extra;
    if (prepared > selected->prepared) {
      const auto extent = prepared - selected->prepared;
      if (::mprotect(selected->address + selected->prepared, extent, PROT_READ | PROT_WRITE) != 0)
        return std::unexpected(Error::io);
      selected->prepared = prepared;
    }
    ++pool.internal->counts.reuses;
  } else {
    const auto align = std::max(page_size(), request.alignment);
    const auto slack = align - page_size();
    if (reserved > maximum - slack) return std::unexpected(Error::invalid_argument);
    const auto mapping_bytes = reserved + slack;
    const auto metadata = sizeof(Window);
    std::unique_ptr<Window> fresh;
    try {
      fresh = std::make_unique<Window>();
      // Reserve the publication slot before mapping. Once VM ownership exists,
      // neither successful nor failed preparation needs fallible bookkeeping.
      auto& inventory = pool.internal->windows;
      if (inventory.size() == inventory.capacity()) {
        const auto limit = inventory.max_size();
        const auto next = inventory.capacity() > limit / 2 ? limit :
            std::max<std::size_t>(8, inventory.capacity() * 2);
        inventory.reserve(next);
      }
    } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
    if (!claim(pool, prepared + metadata)) return std::unexpected(Error::capacity);
    auto* mapping = ::mmap(nullptr, mapping_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
      refund(pool, prepared + metadata);
      return std::unexpected(Error::io);
    }
    const auto raw = reinterpret_cast<std::uintptr_t>(mapping);
    const auto aligned = (raw + align - 1) & ~(align - 1);
    selected = fresh.get();
    selected->mapping = static_cast<std::byte*>(mapping);
    selected->mapping_bytes = mapping_bytes;
    selected->address = reinterpret_cast<std::byte*>(aligned);
    selected->reserved = reserved;
    selected->charged = prepared;
    selected->metadata_charge = metadata;
    initialize(selected->domain, pool, selected, false);
    selected->use.domain = &selected->domain;
    selected->job = {nullptr, selected, window_trim_run};
    selected->trimmed.context = selected;
    selected->trimmed.ops = &window_trim_ops;
    selected->owner_index = pool.internal->windows.size();
    pool.internal->windows.push_back(std::move(fresh));
    // A failed syscall can change part of a range. Keep the full charged bound
    // as idle, retryable inventory; preparation is confirmed only on success.
    if (::mprotect(selected->address, prepared, PROT_READ | PROT_WRITE) != 0) {
      add_idle(*selected);
      return std::unexpected(Error::io);
    }
    selected->prepared = prepared;
  }
  remove_idle(*selected);
  result.storage = &selected->use;
  result.address = selected->address; result.size = request.bytes;
  result.readable_padding = request.readable_padding;
  return {};
}

Result<void> extend_window(Window& window, std::size_t size) {
  auto& pool = *window.domain.pool;
  std::size_t prepared;
  if (!round_up(size, page_size(), prepared)) return std::unexpected(Error::invalid_argument);
  if (prepared > window.prepared) {
    const auto extra = prepared > window.charged ? prepared - window.charged : 0;
    if (!claim(pool, extra)) return std::unexpected(Error::capacity);
    window.charged += extra;
    if (::mprotect(window.address + window.prepared, prepared - window.prepared,
        PROT_READ | PROT_WRITE) != 0) return std::unexpected(Error::io);
    window.prepared = prepared;
  }
  return {};
}

Result<void> prepare_window(Pool& pool, Preparation& preparation,
    const AllocationRequest& request, worker::Lane& preparer) {
  if (!pool.lane || !worker::on_lane(*pool.lane)) return std::unexpected(Error::conflict);
  if (pool.state != Pool::State::open || pool.internal->close_requested)
    return std::unexpected(Error::unavailable);
  if (preparation.state != Preparation::State::idle || preparation.backend)
    return std::unexpected(Error::invalid_argument);
  {
    const auto target = preparer.owner.load(std::memory_order_acquire);
    if (!target) return std::unexpected(Error::unavailable);
    if (&preparer == pool.lane || target == pool.lane->owner.load(std::memory_order_acquire))
      return std::unexpected(Error::invalid_argument);
  }
  const auto size = extent(request);
  if (!size) return std::unexpected(size.error());
  std::size_t reserved{}, prepared{};
  if (!round_up(std::max(*size, request.reserved_bytes), page_size(), reserved) ||
      !round_up(*size, page_size(), prepared)) return std::unexpected(Error::invalid_argument);
  const auto alignment = std::max(page_size(), request.alignment);
  const auto slack = alignment - page_size();
  if (reserved > maximum - slack) return std::unexpected(Error::invalid_argument);
  auto* selected = select_idle_window(pool, reserved, prepared, request.alignment);
  std::unique_ptr<Window> fresh;
  if (!selected) {
    try {
      fresh = std::make_unique<Window>();
      auto& inventory = pool.internal->windows;
      if (inventory.size() == inventory.capacity())
        inventory.reserve(std::max<std::size_t>(8, inventory.capacity() * 2));
    } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
    if (!claim(pool, prepared + sizeof(Window))) return std::unexpected(Error::capacity);
    selected = fresh.get();
    selected->reserved = reserved;
    selected->mapping_bytes = reserved + slack;
    selected->charged = prepared;
    selected->metadata_charge = sizeof(Window);
    initialize(selected->domain, pool, selected, false);
    selected->use.domain = &selected->domain;
  } else {
    const auto extra = prepared > selected->charged ? prepared - selected->charged : 0;
    if (extra && !claim(pool, extra)) return std::unexpected(Error::capacity);
    selected->charged += extra;
    remove_idle(*selected);
    preparation.reused = true;
  }
  auto& window = *selected;
  preparation.request = request;
  preparation.outcome = {window.mapping, window.address, window.prepared, false, Error::io};
  window.preparation = &preparation;
  window.job = {nullptr, &window, window_trim_run};
  window.trimmed.context = &window;
  window.trimmed.ops = &window_prepare_lane_ops;
  window.trim_lifetime = Ref<Arena>::retain(*pool.arena);
  preparation.owner = &pool;
  preparation.preparer = &preparer;
  preparation.backend = &window;
  preparation.changed.lane = pool.lane;
  preparation.changed.state = Readiness::pending;
  preparation.state = Preparation::State::pending;
  auto* published = selected;
  if (fresh) {
    selected->owner_index = pool.internal->windows.size();
    pool.internal->windows.push_back(std::move(fresh));
  }
  worker::post(preparer, published->trimmed);
  return {};
}

} // namespace orbital::allocation::detail
