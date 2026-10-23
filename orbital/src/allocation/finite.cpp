#include "finite.h"

#include <cassert>
#include <cstring>
#include <limits>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

namespace orbital::allocation::detail {
namespace {
constexpr std::size_t round_up(std::size_t value, std::size_t unit) noexcept {
  return (value + unit - 1) & ~(unit - 1);
}
} // namespace

Result<std::unique_ptr<FiniteGroup>> FiniteGroup::create(unsigned cls, std::size_t page_granule) noexcept {
  if (cls >= finite_classes) return std::unexpected(Error::invalid_argument);
  const auto native = ::sysconf(_SC_PAGESIZE);
  if (native <= 0 || !std::has_single_bit(static_cast<std::size_t>(native)))
    return std::unexpected(Error::unavailable);
  const auto page = page_granule ? page_granule : static_cast<std::size_t>(native);
  if (!std::has_single_bit(page) || page > 65536 || page < static_cast<std::size_t>(native))
    return std::unexpected(Error::invalid_argument);
  auto group = std::unique_ptr<FiniteGroup>(new (std::nothrow) FiniteGroup);
  if (!group) return std::unexpected(Error::capacity);
  group->cls_ = cls;
  group->capacity_ = finite_capacities[cls];
  group->host_page_ = static_cast<std::size_t>(native);
  group->native_page_ = page;
  group->page_shift_ = static_cast<unsigned>(std::countr_zero(page));
  const auto payload_bytes = round_up(finite_homes * group->capacity_, page);
  group->payload_pages_ = payload_bytes / page;
  // A larger native granule than the host page needs alignment slack.
  group->reserved_bytes_ = payload_bytes + (page > group->host_page_ ? page : 0);
  const auto scratch_offset = round_up(group->payload_pages_ * sizeof(Page), alignof(std::uint64_t));
  group->control_bytes_ = scratch_offset + 3 * group->live_words() * sizeof(std::uint64_t);
  return group;
}

Result<void> FiniteGroup::prepare() noexcept {
  if (reservation_ || metadata_) return std::unexpected(Error::conflict);
  auto* reservation = ::mmap(nullptr, reserved_bytes_, PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (reservation == MAP_FAILED) return std::unexpected(Error::io);
  reservation_ = static_cast<std::byte*>(reservation);
  payload_ = reinterpret_cast<std::byte*>(round_up(reinterpret_cast<std::uintptr_t>(reservation_), native_page_));
  // Page states and trim scratch share one stable heap block.
  auto* metadata = ::operator new(control_bytes_, std::nothrow);
  if (!metadata) return std::unexpected(Error::capacity);
  metadata_ = metadata;
  std::memset(metadata, 0, control_bytes_);
  pages_ = ::new (metadata) Page[payload_pages_]{};
  trim_scratch_ = ::new (static_cast<std::byte*>(metadata) +
      round_up(payload_pages_ * sizeof(Page), alignof(std::uint64_t))) std::uint64_t[3 * live_words()]{};
  free_.fill(~std::uint64_t{0});
  ready_.fill(0);
  pending_.fill(0);
  return {};
}

FiniteGroup::~FiniteGroup() { (void)retire(); }

Result<void> FiniteGroup::retire() noexcept {
  bool failed = false;
  if (reservation_) {
    if (::munmap(reservation_, reserved_bytes_) != 0) failed = true;
    else { reservation_ = nullptr; payload_ = nullptr; }
  }
  if (metadata_) {
    ::operator delete(metadata_);
    metadata_ = nullptr; pages_ = nullptr; trim_scratch_ = nullptr;
  }
  if (failed) return std::unexpected(Error::io);
  mapped_pages_ = preparing_pages_ = 0;
  free_.fill(0); ready_.fill(0);
  return {};
}

bool FiniteGroup::home_mapped(std::size_t home) const noexcept {
  const auto span = pages(home);
  for (auto p = span.first; p < span.first + span.count; ++p)
    if (pages_[p] != Page::mapped) return false;
  return true;
}

std::pair<std::size_t, std::size_t> FiniteGroup::homes(PageSpan span) const noexcept {
  const auto begin = span.first * native_page_;
  const auto end = (span.first + span.count) * native_page_;
  const auto last = std::min(finite_homes, (end + capacity_ - 1) / capacity_) - 1;
  return {begin / capacity_, last};
}

void FiniteGroup::refresh_ready(PageSpan span) noexcept {
  const auto [first, last] = homes(span);
  for (auto home = first; home <= last; ++home) {
    const auto bit = std::uint64_t{1} << (home % 64);
    if ((free_[home / 64] & bit) && home_mapped(home)) ready_[home / 64] |= bit;
    else ready_[home / 64] &= ~bit;
  }
}

std::size_t FiniteGroup::first_unready() const noexcept {
  for (std::size_t w = 0; w < finite_words; ++w)
    if (const auto word = free_[w] & ~ready_[w]) {
      // A free Home on a trimming/preparing page cannot be claimed yet.
      for (auto bits = word; bits; bits &= bits - 1) {
        const auto home = w * 64 + static_cast<std::size_t>(std::countr_zero(bits));
        const auto span = pages(home);
        bool usable = true;
        for (auto p = span.first; usable && p < span.first + span.count; ++p)
          usable = pages_[p] == Page::absent || pages_[p] == Page::mapped;
        if (usable) return home;
      }
    }
  return finite_homes;
}

std::size_t FiniteGroup::cheapest_unready() const noexcept {
  auto best = finite_homes;
  std::size_t fewest = std::numeric_limits<std::size_t>::max();
  for (std::size_t w = 0; w < finite_words; ++w)
    for (auto bits = free_[w] & ~ready_[w]; bits; bits &= bits - 1) {
      const auto home = w * 64 + static_cast<std::size_t>(std::countr_zero(bits));
      const auto span = pages(home);
      std::size_t missing = 0;
      bool usable = true;
      for (auto p = span.first; usable && p < span.first + span.count; ++p) {
        usable = pages_[p] == Page::absent || pages_[p] == Page::mapped;
        missing += pages_[p] == Page::absent;
      }
      if (!usable || missing >= fewest) continue;
      best = home; fewest = missing;
      if (fewest == 1) return best;
    }
  return best;
}

void FiniteGroup::claim(std::size_t home) noexcept {
  assert(is_free(home) && home_mapped(home));
  free_[home / 64] &= ~(std::uint64_t{1} << (home % 64));
  ready_[home / 64] &= ~(std::uint64_t{1} << (home % 64));
}

std::size_t FiniteGroup::absent_run(std::size_t first, std::size_t limit) const noexcept {
  std::size_t count = 0;
  while (count < limit && first + count < payload_pages_ && pages_[first + count] == Page::absent) ++count;
  return count;
}

Result<std::size_t> FiniteGroup::begin_prepare(std::size_t home, std::span<PageSpan> output) noexcept {
  if (home >= finite_homes || !is_free(home)) return std::unexpected(Error::conflict);
  const auto span = pages(home);
  const auto end = span.first + span.count;
  std::size_t runs = 0;
  bool absent = false;
  for (auto p = span.first; p < end; ++p) {
    if (pages_[p] != Page::absent && pages_[p] != Page::mapped) return std::unexpected(Error::conflict);
    const bool missing = pages_[p] == Page::absent;
    runs += missing && !absent;
    absent = missing;
  }
  if (output.size() < runs) return std::unexpected(Error::invalid_argument);
  std::size_t used = 0;
  for (auto p = span.first; p < end;) {
    if (pages_[p] == Page::mapped) { ++p; continue; }
    const auto first = p;
    while (p < end && pages_[p] == Page::absent) { pages_[p++] = Page::preparing; ++preparing_pages_; }
    output[used++] = {first, p - first};
  }
  free_[home / 64] &= ~(std::uint64_t{1} << (home % 64));
  ready_[home / 64] &= ~(std::uint64_t{1} << (home % 64));
  pending_[home / 64] |= std::uint64_t{1} << (home % 64);
  return used;
}

Result<void> FiniteGroup::finish_prepare(std::size_t home, std::span<const PageSpan> spans,
    bool prepared) noexcept {
  if (!pending(home)) return std::unexpected(Error::conflict);
  for (auto span : spans)
    for (auto p = span.first; p < span.first + span.count; ++p)
      if (pages_[p] != Page::preparing) return std::unexpected(Error::conflict);
  if (!prepared) return {};
  for (auto span : spans) {
    for (auto p = span.first; p < span.first + span.count; ++p) pages_[p] = Page::mapped;
    mapped_pages_ += span.count; preparing_pages_ -= span.count;
    refresh_ready(span);
  }
  return {};
}

Result<void> FiniteGroup::discard_preparation(std::size_t home, std::span<const PageSpan> spans) noexcept {
  if (!pending(home)) return std::unexpected(Error::conflict);
  for (auto span : spans)
    for (auto p = span.first; p < span.first + span.count; ++p) {
      if (pages_[p] != Page::preparing) return std::unexpected(Error::conflict);
      pages_[p] = Page::absent; --preparing_pages_;
    }
  return {};
}

void FiniteGroup::end_prepare(std::size_t home) noexcept {
  assert(pending(home));
  pending_[home / 64] &= ~(std::uint64_t{1} << (home % 64));
  free_[home / 64] |= std::uint64_t{1} << (home % 64);
  if (home_mapped(home)) ready_[home / 64] |= std::uint64_t{1} << (home % 64);
}

void FiniteGroup::bind_prepared(std::size_t home) noexcept {
  assert(pending(home) && home_mapped(home));
  pending_[home / 64] &= ~(std::uint64_t{1} << (home % 64));
}

Result<void> FiniteGroup::mark_mapped(PageSpan span) noexcept {
  if (!span.count || span.first + span.count > payload_pages_) return std::unexpected(Error::invalid_argument);
  for (auto p = span.first; p < span.first + span.count; ++p)
    if (pages_[p] == Page::trimming || pages_[p] == Page::preparing) return std::unexpected(Error::conflict);
  for (auto p = span.first; p < span.first + span.count; ++p) {
    if (pages_[p] == Page::absent) ++mapped_pages_;
    pages_[p] = Page::mapped;
  }
  refresh_ready(span);
  return {};
}

Result<void> FiniteGroup::live_pages(std::span<std::uint64_t> live) const noexcept {
  if (live.size() < live_words()) return std::unexpected(Error::invalid_argument);
  std::fill(live.begin(), live.end(), 0);
  for (std::size_t w = 0; w < finite_words; ++w)
    for (auto used = ~free_[w]; used; used &= used - 1) {
      const auto span = pages(w * 64 + static_cast<std::size_t>(std::countr_zero(used)));
      for (auto p = span.first; p < span.first + span.count; ++p) live[p / 64] |= std::uint64_t{1} << (p % 64);
    }
  return {};
}

Result<void> FiniteGroup::begin_trim() noexcept {
  const auto membership = std::span<const std::uint64_t>(trim_membership());
  const auto tail = payload_pages_ % 64;
  if (tail && (membership.back() >> tail)) return std::unexpected(Error::invalid_argument);
  const auto live = std::span(trim_scratch_, live_words());
  if (const auto counted = live_pages(live); !counted) return std::unexpected(counted.error());
  std::size_t cursor = 0;
  PageSpan span;
  while (next_trim_span(membership, cursor, span))
    for (auto p = span.first; p < span.first + span.count; ++p)
      if (pages_[p] != Page::mapped || (live[p / 64] & (std::uint64_t{1} << (p % 64))))
        return std::unexpected(Error::conflict);
  // Admit the complete immutable selection before changing any page state.
  cursor = 0;
  while (next_trim_span(membership, cursor, span)) {
    for (auto p = span.first; p < span.first + span.count; ++p) pages_[p] = Page::trimming;
    refresh_ready(span); // Free Homes on trimming pages leave the ready set.
  }
  return {};
}

Result<void> FiniteGroup::finish_trim(PageSpan span, bool discarded) noexcept {
  if (!span.count || span.first + span.count > payload_pages_) return std::unexpected(Error::invalid_argument);
  for (auto p = span.first; p < span.first + span.count; ++p)
    if (pages_[p] != Page::trimming) return std::unexpected(Error::conflict);
  for (auto p = span.first; p < span.first + span.count; ++p)
    pages_[p] = discarded ? Page::absent : Page::mapped;
  if (discarded) mapped_pages_ -= span.count;
  refresh_ready(span);
  return {};
}

} // namespace orbital::allocation::detail
