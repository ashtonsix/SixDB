#pragma once

// Opt-in relocatable handles over Orbital allocations: the owner may move a
// payload while readers on other workers resolve its handle. Readers resolve
// inside an epoch Section; the owner frees a replaced Allocation once every
// reader has left the epoch in which it could have resolved it.
// Spike prototype: one owner Lane per table, fixed handle and reader capacity.

#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace relocatable {
namespace a = orbital::allocation;

inline constexpr std::uint64_t idle = std::numeric_limits<std::uint64_t>::max();

class Table;
/// One per (Table, reading thread), on its own line. Written only by that
/// thread. A Reader belongs to the Table it was registered with: epochs of
/// different Tables are independent, so sharing a Reader would let one
/// Table's Section stand in for another's. Sections on one Reader may nest or
/// interleave (for example Loom Tasks on one worker): the outermost announces,
/// and protection lasts until the last Section on the Reader ends.
struct alignas(64) Reader {
  std::atomic<std::uint64_t> epoch{idle};
  std::uint32_t depth{}; // Reader-thread only.
  const Table* table{}; // Set once by the owning Table.
};

/// What a reader sees: an address and a readable extent, valid until its
/// Section ends. The owner never writes bytes a published extent covers.
struct View {
  const std::byte* address{};
  std::size_t size{};
};

class Table {
  // A reader pairs an extent with its payload by re-reading the address: an
  // extent only grows while its address is current.
  struct alignas(16) Slot {
    std::atomic<const std::byte*> address{};
    std::atomic<std::size_t> size{};
  };
  struct Retired { a::Allocation allocation; std::uint64_t epoch; };

  a::Pool& pool_;
  std::unique_ptr<Slot[]> slots_;
  std::vector<a::Allocation> owned_; // Owner-only: the current Allocation per handle.
  std::vector<std::uint32_t> free_;
  std::vector<Retired> retired_;
  std::span<Reader> readers_;
  alignas(64) std::atomic<std::uint64_t> epoch_{1};

public:
  Table(a::Pool& pool, std::size_t handles, std::span<Reader> readers)
      : pool_(pool), slots_(new Slot[handles]), owned_(handles), readers_(readers) {
    for (auto& reader : readers_) {
      assert(!reader.table && "a Reader belongs to exactly one Table");
      reader.table = this;
    }
    free_.reserve(handles);
    for (auto h = handles; h--;) free_.push_back(static_cast<std::uint32_t>(h));
    retired_.reserve(handles);
  }
  ~Table() { for (auto& reader : readers_) reader.table = nullptr; }
  Table(const Table&) = delete;
  Table& operator=(const Table&) = delete;

  // ---- reader side (any worker) ----
  class Section {
    Reader& reader_;
  public:
    Section(const Table& table, Reader& reader) noexcept : reader_(reader) {
      // Enforced: optimized builds keep assertions.
      assert(reader_.table == &table && "Section on a Reader registered with another Table");
      if (reader_.depth++) return; // An enclosing Section already protects this epoch.
      // Announce before resolving: the owner's collect() must observe this
      // epoch or a later one before it can free anything resolved below.
      reader_.epoch.store(table.epoch_.load(std::memory_order_acquire), std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    ~Section() {
      assert(reader_.depth);
      if (!--reader_.depth) reader_.epoch.store(idle, std::memory_order_release);
    }
    Section(const Section&) = delete;
    Section& operator=(const Section&) = delete;
  };
  [[gnu::always_inline]] View resolve(std::uint32_t handle) const noexcept {
    const auto& slot = slots_[handle];
    for (;;) {
      const auto* address = slot.address.load(std::memory_order_acquire);
      const auto size = slot.size.load(std::memory_order_acquire);
      if (slot.address.load(std::memory_order_relaxed) == address) [[likely]] return {address, size};
    }
  }

  // ---- owner side (the Pool's Lane) ----
  orbital::Result<std::uint32_t> create(std::size_t bytes) {
    if (free_.empty()) return std::unexpected(orbital::Error::capacity);
    auto value = a::allocate(pool_, {.bytes = bytes});
    if (!value) return std::unexpected(value.error());
    const auto handle = free_.back(); free_.pop_back();
    publish(handle, *value);
    owned_[handle] = std::move(*value);
    return handle;
  }
  /// Owner-writable bytes of the current payload. Bytes a reader may see must
  /// not change until a move publishes a replacement.
  std::byte* writable(std::uint32_t handle) noexcept { return owned_[handle].address; }
  /// Grow within capacity in place; otherwise copy to a new Allocation,
  /// publish it and retire the old one under the current epoch.
  orbital::Result<bool> grow(std::uint32_t handle, std::size_t bytes) {
    auto& current = owned_[handle];
    if (bytes <= a::capacity(current)) {
      // Growth in place exposes new bytes; the writer initialized them first.
      current.size = bytes;
      slots_[handle].size.store(bytes, std::memory_order_release);
      return false;
    }
    if (!reserve_retirement()) return std::unexpected(orbital::Error::capacity);
    auto replacement = a::allocate(pool_, {.bytes = bytes});
    if (!replacement) return std::unexpected(replacement.error());
    std::memcpy(replacement->address, current.address, current.size);
    replacement->size = current.size; // New bytes stay unpublished until written.
    publish(handle, *replacement);
    retire(std::move(current));
    current = std::move(*replacement);
    return true;
  }
  /// Copy-on-write replacement: fill a fresh Allocation, publish it, retire the old.
  template<class Fill> orbital::Result<void> replace(std::uint32_t handle, std::size_t bytes, Fill fill) {
    if (!reserve_retirement()) return std::unexpected(orbital::Error::capacity);
    auto replacement = a::allocate(pool_, {.bytes = bytes});
    if (!replacement) return std::unexpected(replacement.error());
    fill(replacement->address, replacement->size);
    publish(handle, *replacement);
    retire(std::move(owned_[handle]));
    owned_[handle] = std::move(*replacement);
    return {};
  }
  void set_size(std::uint32_t handle, std::size_t bytes) noexcept {
    assert(bytes <= a::capacity(owned_[handle]));
    owned_[handle].size = bytes;
    slots_[handle].size.store(bytes, std::memory_order_release);
  }
  orbital::Result<void> destroy(std::uint32_t handle) {
    if (!reserve_retirement()) return std::unexpected(orbital::Error::capacity);
    slots_[handle].address.store(nullptr, std::memory_order_release);
    slots_[handle].size.store(0, std::memory_order_release);
    retire(std::move(owned_[handle]));
    free_.push_back(handle); // Capacity reserved at construction.
    return {};
  }
  /// Release retired Allocations no reader can still hold. Returns the count.
  std::size_t collect() {
    // Pairs with the Section's fence: either a reader's announcement is visible
    // here, or that reader's resolve sees every replacement published before.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto oldest = idle;
    for (const auto& reader : readers_)
      oldest = std::min(oldest, reader.epoch.load(std::memory_order_acquire));
    std::size_t freed = 0;
    auto keep = retired_.begin();
    for (auto& item : retired_) {
      if (item.epoch < oldest) { a::release(item.allocation); ++freed; }
      else *keep++ = std::move(item);
    }
    retired_.erase(keep, retired_.end());
    return freed;
  }
  std::size_t retired() const noexcept { return retired_.size(); }

private:
  void publish(std::uint32_t handle, const a::Allocation& value) noexcept {
    // Publish the smaller extent first, so any (address, extent) pair a reader
    // can observe lies within that address's payload.
    auto& slot = slots_[handle];
    if (value.size < slot.size.load(std::memory_order_relaxed)) {
      slot.size.store(value.size, std::memory_order_release);
      slot.address.store(value.address, std::memory_order_release);
    } else {
      slot.address.store(value.address, std::memory_order_release);
      slot.size.store(value.size, std::memory_order_release);
    }
  }
  // Before anything is published: once readers can see a replacement, retiring
  // the old payload must not fail, or unwinding would free protected bytes.
  // Capacity grows geometrically, so a stalled reader's growing backlog costs
  // amortized constant bookkeeping per move.
  bool reserve_retirement() noexcept {
    if (retired_.size() < retired_.capacity()) return true;
    try { retired_.reserve(std::max<std::size_t>(16, 2 * retired_.capacity())); return true; }
    catch (const std::bad_alloc&) { return false; }
  }
  void retire(a::Allocation old) noexcept {
    assert(retired_.size() < retired_.capacity());
    // Readers that entered at or before this epoch may hold the old address.
    const auto epoch = epoch_.fetch_add(1, std::memory_order_acq_rel);
    retired_.push_back({std::move(old), epoch});
  }
};

} // namespace relocatable
