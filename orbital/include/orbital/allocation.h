#pragma once

// Physical storage, reuse and reclamation under a shared byte budget.
// Loom selects the owning Pool; Ikea qualifies the format of its bytes.

#include <orbital/worker.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>

namespace orbital::allocation {
struct Arena;
struct Pool;
struct Storage;
namespace detail { struct ArenaState; struct PoolState; }
void retain_ref(Arena&) noexcept;
void release_ref(Arena&) noexcept;

enum class Initialization { uninitialized, zero };

enum class Temperature { hot, warm, cold };

/// Optional reclamation advice; omitted fields preserve earlier hints.
struct ReleaseHint {
  std::optional<Temperature> temperature;
  std::chrono::steady_clock::time_point last_access{};
};

struct AllocationRequest {
  // Nonzero reserved_bytes includes padding; zero lets the allocator choose.
  std::size_t bytes{}, reserved_bytes{};
  std::size_t alignment{alignof(std::max_align_t)}, readable_padding{};
  Initialization initialization{Initialization::uninitialized};
};

/// Private span growth without content initialization. The caller authorizes
/// and initializes newly exposed body/padding before a consumer reads them.
struct GrowthRequest {
  std::size_t bytes{}, reserved_bytes{};
  std::size_t alignment{alignof(std::max_align_t)}, readable_padding{};
};

/// Shared byte budget and background reclamation for its Pools.
struct Arena {
  std::atomic<std::uint32_t> references{};
  std::uint64_t byte_limit{};
  std::atomic<std::uint64_t> charged_bytes{};
  std::unique_ptr<detail::ArenaState> state;
  Arena();
  ~Arena();
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
};

/// Reusable storage assigned to one stable Lane; keep that Lane alive through close.
struct Pool {
  Arena* arena{};
  worker::Lane* lane{};
  enum class State { open, closing, closed };
  State state{State::open};
  /// Observe generation before a request; recheck availability after every wake.
  /// A legal shared-budget refusal enrolls this Pool for one future refund notice.
  /// Local changes also pulse this signal. A notice grants no storage.
  Signal capacity_changed;
  std::unique_ptr<detail::PoolState> internal;
  Pool();
  ~Pool();
  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;
};

/// Move-only ownership of mutable storage. Destruction releases its physical use.
struct Allocation {
  Storage* storage{};
  std::byte* address{};
  std::size_t size{}, readable_padding{};
  Allocation() = default;
  Allocation(Allocation&&) noexcept;
  Allocation& operator=(Allocation&&) noexcept;
  Allocation(const Allocation&) = delete;
  Allocation& operator=(const Allocation&) = delete;
  ~Allocation();

  explicit operator bool() const noexcept { return storage; }
};

/// Retains an address and footprint after Allocation release.
/// Physical lifetime does not freeze contents or grant access permission.
struct Hold {
  Storage* storage{};
  std::byte* address{};
  ByteRange footprint{}; ///< Offset is relative to Allocation; address already includes it.
  Hold() = default;
  Hold(Hold&&) noexcept;
  Hold& operator=(Hold&&) noexcept;
  Hold(const Hold&) = delete;
  Hold& operator=(const Hold&) = delete;
  ~Hold();

  explicit operator bool() const noexcept { return storage; }
};

/// Per-Pool accounting, not process RSS. Mapped bytes are confirmed accessible;
/// pending/uncertain bytes are charged bounds, not confirmed mappings.
struct Statistics {
  std::uint64_t mapped_bytes{}, reserved_bytes{}, metadata_bytes{}, idle_bytes{};
  std::uint64_t preparing_bytes{}, uncertain_bytes{}, preparing_reserved_bytes{};
  std::uint64_t allocations{}, reuses{}, growths{}, relocations{}, foreign_returns{}, trim_pages{};
};

/// One retirement helper per Arena. An optional CPU selects its placement;
/// -1 inherits caller affinity. Startup reports affinity failure before return.
[[nodiscard]] Result<Ref<Arena>> open_arena(std::uint64_t byte_limit, int reclaimer_cpu = -1);
/// Add on the adopted Lane; caller keeps Arena alive through Pool close/removal.
[[nodiscard]] Result<Pool*> add_pool(Arena&, worker::Lane&);
/// Invalidates a fully closed Pool handle. Copy final statistics before removal.
[[nodiscard]] Result<void> remove_pool(Pool&);
/// Owner-lane operations. Padding is initialized; uninitialized body is not readable data.
[[nodiscard]] Result<Allocation> allocate(Pool&, const AllocationRequest&);
/// Preserves existing readable bytes; never initializes new body or padding.
/// Failure preserves the original allocation. Relocation requires all Holds retired.
[[nodiscard]] Result<void> grow(Allocation&, const GrowthRequest&);
/// Capture on the storage's owner Lane; include every byte a native kernel may read.
/// Empty ranges and ranges beyond the Allocation plus readable padding fail.
[[nodiscard]] Result<Hold> hold(const Allocation&, ByteRange footprint);
/// Duplicate a live Hold on its owner Lane; saturation refuses without mutation.
[[nodiscard]] Result<Hold> retain(const Hold&);
/// Clear this use; final foreign returns are queued to the owning Lane.
/// Temperature guides reclamation; timestamps reject stale advice.
void release(Hold&, ReleaseHint = {}) noexcept;
void release(Allocation&, ReleaseHint = {}) noexcept;
/// Starts asynchronous reclamation with a Pool-wide idle-payload floor of keep_bytes.
/// Selects COLD first, then warmer inventory; poll the owner Lane through completion.
[[nodiscard]] Progress trim_cold(Pool&, std::uint64_t keep_bytes);
/// Rejects new allocation/growth, then drains actual uses, Messages and reclamation.
[[nodiscard]] Progress close(Pool&);
Statistics statistics(const Pool&) noexcept;
/// Accessible bytes, not initialized length or resident RAM. A reused Window
/// can expose more than the request; finite capacity follows its Group's palette,
/// so identical requests can round differently.
std::size_t capacity(const Allocation&) noexcept;
/// Address space retained for growth; virtual windows may reserve beyond capacity.
std::size_t reserved_bytes(const Allocation&) noexcept;

inline MutableBytes bytes(const Allocation& value) noexcept { return {value.address, value.size}; }

inline MutableBytes bytes(const Hold& value) noexcept {
  return {value.address, static_cast<std::size_t>(value.footprint.size)};
}
} // namespace orbital::allocation
