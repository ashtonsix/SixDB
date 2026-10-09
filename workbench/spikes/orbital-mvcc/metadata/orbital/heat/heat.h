#pragma once

#include "internal.h"
#include <atomic>
#include <memory>

namespace orbital_heat {
namespace oa = orbital::allocation;
namespace d = oa::detail;
using Clock = std::chrono::steady_clock;

// Standalone probe routing is charged here. A real integration could reuse the
// existing Domain return Message, but this prototype does not claim that saving.
class GroupAdvice {
  oa::ReleaseHint owner_, foreign_;
  std::mutex* mutex_;
  orbital::worker::Lane* lane_;
  orbital::worker::Message message_;
  std::atomic<bool> queued_{};
  bool dirty_{};
  static void handle(orbital::worker::Message&, orbital::worker::Lane&) noexcept;
  static void finished(orbital::worker::Message&) noexcept;
public:
  std::uint64_t publications{}, callbacks{}, deferred{};
  GroupAdvice(std::mutex&, orbital::worker::Lane&);
  ~GroupAdvice();
  void local(oa::ReleaseHint) noexcept;
  void foreign(oa::ReleaseHint) noexcept;
  oa::ReleaseHint advice() const noexcept { return owner_; }
  bool pending() const noexcept { return queued_.load(std::memory_order_acquire); }
};

// Shared immutable geometry supplies class addresses/capacities. No copied
// directory or per-identity owner exists in this physical-recency experiment.
class EpochBanks {
  const d::FiniteGroup* geometry_;
  std::mutex* mutex_;
  d::Palette palette_;
  std::uint32_t words_;
  std::unique_ptr<std::atomic<std::uint64_t>[]> bits_;
  Clock::time_point boundary_;
  Clock::duration period_;
  unsigned current_{};
  bool aggregate_{};
  static constexpr unsigned absent = 2;
  unsigned select(oa::ReleaseHint) const noexcept;
  std::pair<std::size_t, std::size_t> cells(const d::Home&) const noexcept;
  void mark(const d::Home&, unsigned) noexcept;
public:
  std::uint64_t bytes_cleared{}, deferred{};
  EpochBanks(const d::FiniteGroup&, d::Palette, std::mutex&,
      Clock::time_point first_boundary, Clock::duration period, bool aggregate = false);
  void local(const d::Home&, oa::ReleaseHint) noexcept;
  void foreign(const d::Home&, oa::ReleaseHint) noexcept;
  // Probe seam for the publisher-stall falsifier. Caller holds the actual
  // Domain mutex through selection and OR; owner rollover only try-locks.
  unsigned select_foreign_locked(oa::ReleaseHint value) const noexcept { return select(value); }
  void mark_foreign_locked(const d::Home& home, unsigned bank) noexcept { mark(home, bank); }
  bool rollover(Clock::time_point boundary) noexcept;
  unsigned heat(const d::Home&) const noexcept;
  std::size_t heap_usable() const noexcept;
  std::size_t bank_bytes() const noexcept { return std::size_t(words_) * 2 * sizeof(std::atomic<std::uint64_t>); }
};

} // namespace orbital_heat
