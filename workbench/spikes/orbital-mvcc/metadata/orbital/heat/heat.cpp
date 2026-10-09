#include "heat.h"
#include <cstdlib>
#include <malloc.h>
#include <thread>

namespace orbital_heat {
namespace {
bool supplied(const oa::ReleaseHint& h) {
  return h.temperature || h.last_access != Clock::time_point{};
}
void merge(oa::ReleaseHint& target, const oa::ReleaseHint& source) {
  if (source.last_access != Clock::time_point{} && source.last_access < target.last_access) return;
  if (source.temperature) target.temperature = source.temperature;
  if (source.last_access != Clock::time_point{}) target.last_access = source.last_access;
}
void none(orbital::worker::Message&) noexcept {}
}

GroupAdvice::GroupAdvice(std::mutex& mutex, orbital::worker::Lane& lane)
    : mutex_(&mutex), lane_(&lane) {
  static const orbital::worker::MessageOps operations{none, handle, finished};
  message_.context = this; message_.ops = &operations;
}
GroupAdvice::~GroupAdvice() { if (pending()) std::abort(); }
void GroupAdvice::local(oa::ReleaseHint hint) noexcept { merge(owner_, hint); }
void GroupAdvice::foreign(oa::ReleaseHint hint) noexcept {
  if (!supplied(hint)) return;
  std::lock_guard lock(*mutex_);
  merge(foreign_, hint); dirty_ = true;
  if (!queued_.exchange(true, std::memory_order_acq_rel)) {
    ++publications;
    orbital::worker::post(*lane_, message_);
  }
}
void GroupAdvice::handle(orbital::worker::Message& message, orbital::worker::Lane&) noexcept {
  auto& self = *static_cast<GroupAdvice*>(message.context);
  ++self.callbacks;
  std::unique_lock lock(*self.mutex_, std::try_to_lock);
  if (!lock) { ++self.deferred; return; }
  merge(self.owner_, self.foreign_); self.foreign_ = {}; self.dirty_ = false;
}
void GroupAdvice::finished(orbital::worker::Message& message) noexcept {
  auto& self = *static_cast<GroupAdvice*>(message.context);
  std::unique_lock lock(*self.mutex_, std::try_to_lock);
  if (!lock) { orbital::worker::post(*self.lane_, message); return; }
  self.queued_.store(false, std::memory_order_release);
  if (self.dirty_) {
    self.queued_.store(true, std::memory_order_release);
    orbital::worker::post(*self.lane_, message);
  }
}

EpochBanks::EpochBanks(const d::FiniteGroup& geometry, d::Palette palette, std::mutex& mutex,
    Clock::time_point boundary, Clock::duration period, bool aggregate)
    : geometry_(&geometry), mutex_(&mutex), palette_(palette),
      words_(aggregate ? 1 : static_cast<std::uint32_t>((geometry.identity_count() * (palette == d::Palette::a ? 12 : 15) + 63) / 64)),
      bits_(std::make_unique<std::atomic<std::uint64_t>[]>(words_ * 2)), boundary_(boundary), period_(period), aggregate_(aggregate) {
  if (period_ <= Clock::duration{} || geometry.native_page() != 4096) std::abort(); // The actual geometry qualified by this probe.
}

std::size_t EpochBanks::heap_usable() const noexcept { return ::malloc_usable_size(bits_.get()); }

unsigned EpochBanks::select(oa::ReleaseHint hint) const noexcept {
  if (!supplied(hint) || hint.temperature == oa::Temperature::cold) return absent;
  if (hint.last_access == Clock::time_point{}) return current_;
  if (hint.last_access >= boundary_) return current_;
  if (hint.last_access >= boundary_ - period_) return 1 - current_;
  return absent;
}

std::pair<std::size_t, std::size_t> EpochBanks::cells(const d::Home& home) const noexcept {
  if (aggregate_) return {0, 0};
  if (palette_ == d::Palette::a && home.cls < 6) {
    const auto offset = static_cast<std::size_t>(home.address - geometry_->page_address(0));
    return {offset / 128, (offset + home.capacity - 1) / 128};
  }
  const auto base = palette_ == d::Palette::a ?
      geometry_->identity_count() * (3 + home.cls - 6) : geometry_->identity_count() * home.cls;
  const auto index = base + home.grid * 16 + home.slot;
  return {index, index};
}

void EpochBanks::mark(const d::Home& home, unsigned bank) noexcept {
  if (bank == absent) return;
  const auto [first, last] = cells(home);
  for (auto word = first / 64; word <= last / 64; ++word) {
    const auto low = word == first / 64 ? first % 64 : 0;
    const auto high = word == last / 64 ? last % 64 : 63;
    const auto mask = (~std::uint64_t{0} << low) & (~std::uint64_t{0} >> (63 - high));
    bits_[bank * words_ + word].fetch_or(mask, std::memory_order_relaxed);
  }
}

void EpochBanks::local(const d::Home& home, oa::ReleaseHint hint) noexcept { mark(home, select(hint)); }
void EpochBanks::foreign(const d::Home& home, oa::ReleaseHint hint) noexcept {
  std::lock_guard lock(*mutex_);
  mark(home, select(hint));
}

bool EpochBanks::rollover(Clock::time_point boundary) noexcept {
  if (boundary <= boundary_) return true;
  std::unique_lock lock(*mutex_, std::try_to_lock);
  if (!lock) { ++deferred; return false; }
  const auto elapsed = (boundary - boundary_) / period_;
  if (!elapsed) return true;
  if (elapsed >= 2) {
    for (unsigned i = 0; i < words_ * 2; ++i) bits_[i].store(0, std::memory_order_relaxed);
    bytes_cleared += bank_bytes();
  } else {
    current_ = 1 - current_;
    for (unsigned i = 0; i < words_; ++i) bits_[current_ * words_ + i].store(0, std::memory_order_relaxed);
    bytes_cleared += bank_bytes() / 2;
  }
  boundary_ += elapsed * period_;
  return true;
}

unsigned EpochBanks::heat(const d::Home& home) const noexcept {
  const auto [first, last] = cells(home);
  unsigned result{};
  for (auto word = first / 64; word <= last / 64; ++word) {
    const auto low = word == first / 64 ? first % 64 : 0;
    const auto high = word == last / 64 ? last % 64 : 63;
    const auto mask = (~std::uint64_t{0} << low) & (~std::uint64_t{0} >> (63 - high));
    if (bits_[current_ * words_ + word].load(std::memory_order_relaxed) & mask) return 2;
    if (bits_[(1 - current_) * words_ + word].load(std::memory_order_relaxed) & mask) result = 1;
  }
  return result;
}

} // namespace orbital_heat
