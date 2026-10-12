// Frozen configurable backend for the palette experiment. See README.md.
#include "finite.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstring>
#include <limits>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

namespace orbital::allocation::palette_experiment {
namespace {
constexpr std::array<std::size_t, 15> palette_a{
    16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072};
constexpr std::array<std::size_t, 15> palette_b{
    256, 320, 384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2560, 3072};
constexpr std::array<std::size_t, 15> palette_b_large{
    256, 320, 384, 448, 512, 640, 768, 896, 1088, 1280, 1536, 1792, 2176, 2560, 3072};

template<std::size_t Entries, std::size_t Classes>
constexpr auto lookup_table(const std::array<std::size_t, Classes>& capacities,
                            std::size_t quantum) noexcept {
  std::array<std::uint8_t, Entries> result{};
  unsigned cls = 0;
  for (std::size_t index = 0; index < Entries; ++index) {
    while ((index + 1) * quantum > capacities[cls]) ++cls;
    result[index] = static_cast<std::uint8_t>(cls);
  }
  return result;
}

constexpr auto lookup_a = lookup_table<192>(palette_a, 16);
constexpr auto lookup_b = lookup_table<48>(palette_b, 64);
constexpr auto lookup_b_large = lookup_table<48>(palette_b_large, 64);

constexpr std::size_t round_up(std::size_t value, std::size_t unit) noexcept {
  return (value + unit - 1) & ~(unit - 1);
}

// Class starts and grid strides preserve a whole slot-mask period. If slot
// zero is unaligned, no slot in this grid can meet the requested alignment.
std::uint16_t aligned_slots(std::size_t capacity, std::size_t offset,
                            std::size_t alignment) noexcept {
  constexpr std::array<std::uint16_t, 5> repeating{0xffff, 0x5555, 0x1111, 0x0101, 1};
  const auto mask = alignment - 1;
  if ((capacity & mask) == 0) return 0xffff;
  if (offset & mask) return 0;
  const auto step = std::countr_zero(alignment) - std::countr_zero(capacity);
  return repeating[std::min(step, 4)];
}

std::uint8_t load(std::uint8_t& value) noexcept {
  return std::atomic_ref<std::uint8_t>(value).load(std::memory_order_acquire);
}

void store(std::uint8_t& value, std::uint8_t next) noexcept {
  std::atomic_ref<std::uint8_t>(value).store(next, std::memory_order_release);
}
} // namespace

Result<std::unique_ptr<FiniteGroup>> FiniteGroup::create(FiniteConfig config) noexcept {
  if ((config.palette != Palette::a && config.palette != Palette::b) ||
      (config.identities != 64 && config.identities != 256 && config.identities != 1024) ||
      (config.class_bits != 4 && config.class_bits != 5))
    return std::unexpected(Error::invalid_argument);
  const auto native = ::sysconf(_SC_PAGESIZE);
  if (native <= 0 || !std::has_single_bit(static_cast<std::size_t>(native)))
    return std::unexpected(Error::unavailable);
  const auto page = config.page_granule ? config.page_granule : static_cast<std::size_t>(native);
  const auto slots = std::size_t{1} << (8 - config.class_bits);
  const auto capacities = config.capacities.empty()
      ? std::span(config.palette == Palette::a ? palette_a : (page >= 16384 ? palette_b_large : palette_b))
      : config.capacities;
  if (!std::has_single_bit(page) || page > 65536 || page < static_cast<std::size_t>(native) ||
      page < config.identities || config.identities / slots > 64 || capacities.empty() ||
      capacities.size() > (1u << config.class_bits))
    return std::unexpected(Error::invalid_argument);
  std::size_t previous = 0;
  for (const auto capacity : capacities) {
    if (capacity <= previous || capacity % 16 || capacity > page || capacity > 65535)
      return std::unexpected(Error::invalid_argument);
    previous = capacity;
  }
  const bool packed = config.capacities.empty() ? config.palette == Palette::a : config.pack_small;
  if (packed && (capacities.size() < 5 || !std::equal(capacities.begin(), capacities.begin() + 5,
                                                   palette_a.begin())))
    return std::unexpected(Error::invalid_argument);
  auto group = std::unique_ptr<FiniteGroup>(new (std::nothrow) FiniteGroup);
  if (!group) return std::unexpected(Error::capacity);
  group->host_page_ = static_cast<std::size_t>(native);
  group->native_page_ = page;
  group->identities_ = config.identities;
  group->class_bits_ = config.class_bits;
  group->slot_bits_ = 8 - config.class_bits;
  group->class_mask_ = static_cast<std::uint8_t>((1u << config.class_bits) - 1);
  group->slots_ = static_cast<std::uint8_t>(slots);
  group->slot_mask_ = static_cast<std::uint16_t>((1u << slots) - 1);
  group->class_count_ = static_cast<std::uint8_t>(capacities.size());
  group->grid_count_ = config.identities / slots;
  std::copy(capacities.begin(), capacities.end(), group->capacities_.begin());
  if (config.class_lookup && capacities.back() <= 3072) {
    // The common power-of-two divisor makes every lookup bucket exact.
    std::size_t common = 0;
    for (auto capacity : capacities) common |= capacity;
    group->class_lookup_shift_ = static_cast<std::uint8_t>(std::countr_zero(common));
    if (std::ranges::equal(capacities, palette_a)) group->class_lookup_ = lookup_a.data();
    else if (std::ranges::equal(capacities, palette_b)) group->class_lookup_ = lookup_b.data();
    else if (std::ranges::equal(capacities, palette_b_large)) group->class_lookup_ = lookup_b_large.data();
    else group->class_lookup_bytes_ = static_cast<std::uint8_t>(
        capacities.back() >> group->class_lookup_shift_);
  }
  std::size_t offset = 0;
  if (packed) {
    for (const auto cls : {3u, 0u, 2u, 1u, 4u}) {
      group->offsets_[cls] = static_cast<std::uint32_t>(offset);
      offset += group->identities_ * group->capacities_[cls];
    }
  }
  for (unsigned cls = packed ? 5 : 0; cls < group->class_count_; ++cls) {
    offset = round_up(offset, page);
    group->offsets_[cls] = static_cast<std::uint32_t>(offset);
    offset += group->identities_ * group->capacities_[cls];
  }
  const auto payload_bytes = round_up(offset, page);
  group->payload_pages_ = payload_bytes / page;
  if (group->payload_pages_ > std::numeric_limits<std::uint16_t>::max())
    return std::unexpected(Error::unavailable);
  group->reserved_bytes_ = payload_bytes + (page > group->host_page_ ? page : 0);
  const auto grids_offset = round_up(group->identities_, alignof(std::uint16_t));
  const auto grids_bytes = group->grid_count_ * group->grid_words() * group->mask_bytes();
  const auto scratch_offset = round_up(grids_offset + grids_bytes + group->payload_pages_ * sizeof(Page),
                                      alignof(std::uint64_t));
  group->metadata_bytes_ = scratch_offset + 3 * group->live_words() * sizeof(std::uint64_t) +
      group->class_lookup_bytes_;
  group->control_bytes_ = group->metadata_bytes_;
  return group;
}

Result<void> FiniteGroup::prepare() noexcept {
  if (reservation_ || metadata_) return std::unexpected(Error::conflict);
  const auto grids_offset = round_up(identities_, alignof(std::uint16_t));
  const auto grids_bytes = grid_count_ * grid_words() * mask_bytes();
  const auto scratch_offset = round_up(grids_offset + grids_bytes + payload_pages_ * sizeof(Page),
                                      alignof(std::uint64_t));
  auto* reservation = ::mmap(nullptr, reserved_bytes_, PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (reservation == MAP_FAILED)
    return std::unexpected(Error::io);
  reservation_ = static_cast<std::byte*>(reservation);
  payload_ = reinterpret_cast<std::byte*>(round_up(reinterpret_cast<std::uintptr_t>(reservation_), native_page_));
  // Bindings and owner control share one stable heap block. Their accessors stay
  // distinct; no binding address or payload offset is inferred from the other.
  // Ordinary new covers the maximum required alignment: the uint64_t scratch.
  auto* metadata = ::operator new(metadata_bytes_, std::nothrow);
  if (!metadata)
    return std::unexpected(Error::capacity);
  metadata_ = metadata;
  std::memset(metadata, 0, metadata_bytes_);
  bindings_ = ::new (metadata) std::uint8_t[identities_];
  std::memset(bindings_, unbound, identities_);
  if (slots_ == 8)
    grids8_ = ::new (static_cast<std::byte*>(metadata) + grids_offset) std::uint8_t[grid_count_ * grid_words()]{};
  else
    grids_ = ::new (static_cast<std::byte*>(metadata) + grids_offset) std::uint16_t[grid_count_ * grid_words()]{};
  pages_ = ::new (static_cast<std::byte*>(metadata) + grids_offset + grids_bytes) Page[payload_pages_]{};
  trim_scratch_ = ::new (static_cast<std::byte*>(metadata) + scratch_offset)
      std::uint64_t[3 * live_words()]{};
  if (class_lookup_bytes_) {
    // Shared A/B tables need no owner bytes; custom short tables use this tail.
    auto* lookup = ::new (static_cast<std::byte*>(metadata) + metadata_bytes_ - class_lookup_bytes_)
        std::uint8_t[class_lookup_bytes_];
    unsigned cls = 0;
    for (std::size_t index = 0; index < class_lookup_bytes_; ++index) {
      while ((index + 1) * (std::size_t{1} << class_lookup_shift_) > capacities_[cls]) ++cls;
      lookup[index] = static_cast<std::uint8_t>(cls);
    }
    class_lookup_ = lookup;
  }
  available_identity_grids_ = grid_count_ == 64
      ? ~std::uint64_t{0} : (std::uint64_t{1} << grid_count_) - 1;
  return {};
}

FiniteGroup::~FiniteGroup() {
  (void)retire();
}

Result<void> FiniteGroup::retire() noexcept {
  bool failed = false;
  if (reservation_) {
    if (::munmap(reservation_, reserved_bytes_) != 0) {
      failed = true;
    } else {
      reservation_ = nullptr;
      payload_ = nullptr;
    }
  }
  if (metadata_) {
    ::operator delete(metadata_);
    metadata_ = nullptr;
    bindings_ = nullptr;
    grids_ = nullptr;
    pages_ = nullptr;
    trim_scratch_ = nullptr;
    class_lookup_ = nullptr;
  }
  if (failed)
    return std::unexpected(Error::io);
  mapped_pages_ = preparing_pages_ = 0;
  return {};
}

bool FiniteGroup::valid(Identity identity) const noexcept {
  return identity.index < identities_ &&
      (identity_mask(grid_of(identity)) & (1u << (identity.index & (slots_ - 1))));
}

Result<Identity> FiniteGroup::claim_identity(std::uint64_t excluded_grids) noexcept {
  const auto eligible = available_identity_grids_ & ~excluded_grids;
  if (!eligible)
    return std::unexpected(Error::capacity);
  const auto grid = std::countr_zero(eligible);
  auto occupied = identity_mask(grid);
  const auto slot = std::countr_zero(static_cast<std::uint16_t>(~occupied & slot_mask_));
  occupied |= static_cast<std::uint16_t>(1u << slot);
  set_identity_mask(grid, occupied);
  if (occupied == slot_mask_)
    available_identity_grids_ &= ~(std::uint64_t{1} << grid);
  return Identity{static_cast<std::uint16_t>((grid << slot_bits_) + slot)};
}

Result<void> FiniteGroup::release_identity(Identity identity) noexcept {
  if (!valid(identity))
    return std::unexpected(Error::invalid_argument);
  store(bindings_[identity.index], unbound);
  const auto grid = grid_of(identity);
  set_identity_mask(grid, identity_mask(grid) & ~(1u << (identity.index & (slots_ - 1))));
  available_identity_grids_ |= std::uint64_t{1} << grid;
  return {};
}

Home FiniteGroup::home(std::size_t grid, unsigned cls, unsigned slot) const noexcept {
  const auto capacity = capacities_[cls];
  const auto offset = offsets_[cls] + ((grid << slot_bits_) + slot) * capacity;
  const auto page_shift = std::countr_zero(native_page_);
  const auto first = offset >> page_shift;
  const auto last = (offset + capacity - 1) >> page_shift;
  return {static_cast<std::uint16_t>(grid), static_cast<std::uint8_t>(cls),
          static_cast<std::uint8_t>(slot), capacity, payload_ + offset,
          {first, last - first + 1}};
}

std::size_t FiniteGroup::class_minimum(std::uint8_t cls) const noexcept {
  return cls < class_count_ ? (cls ? capacities_[cls - 1] + 1 : 1) : 0;
}

unsigned FiniteGroup::select_class(std::size_t bytes) const noexcept {
  if (class_lookup_) return class_lookup_[(bytes - 1) >> class_lookup_shift_];
  return static_cast<unsigned>(std::lower_bound(capacities_.begin(),
      capacities_.begin() + class_count_, bytes) - capacities_.begin());
}

Result<Home> FiniteGroup::claim_home(Identity identity, std::size_t bytes,
                                    std::size_t alignment) noexcept {
  const auto cls = class_for(bytes);
  if (!cls) return std::unexpected(cls.error());
  return claim_home_class(identity, *cls, alignment);
}

Result<std::uint8_t> FiniteGroup::class_for(std::size_t bytes) const noexcept {
  if (!bytes || bytes > capacities_[class_count_ - 1])
    return std::unexpected(Error::invalid_argument);
  return static_cast<std::uint8_t>(select_class(bytes));
}

Result<Home> FiniteGroup::claim_home_class(Identity identity, std::uint8_t cls,
                                          std::size_t alignment) noexcept {
  if (!valid(identity) || cls >= class_count_ ||
      !std::has_single_bit(alignment) || alignment > native_page_)
    return std::unexpected(Error::invalid_argument);
  const auto grid = grid_of(identity);
  auto occupied = occupied_mask(grid, cls);
  auto available = static_cast<std::uint16_t>(~occupied & slot_mask_);
  if (class_count_ == (1u << class_bits_) && cls == class_count_ - 1)
    available &= ~(1u << (slots_ - 1));
  if (available) available &= aligned_slots(
      capacities_[cls], offsets_[cls] + (grid << slot_bits_) * capacities_[cls], alignment);
  Home selected;
  std::size_t fewest = std::numeric_limits<std::size_t>::max();
  while (available) {
    const auto slot = static_cast<unsigned>(std::countr_zero(available));
    const auto candidate = home(grid, cls, slot);
    bool usable = true;
    std::size_t missing = 0;
    for (std::size_t page = candidate.pages.first;
         usable && page < candidate.pages.first + candidate.pages.count; ++page) {
      usable = pages_[page] != Page::trimming && pages_[page] != Page::preparing;
      missing += pages_[page] == Page::absent;
    }
    if (usable && missing < fewest) {
      selected = candidate; fewest = missing;
      if (!missing) break;
    }
    available &= static_cast<std::uint16_t>(available - 1);
  }
  if (fewest == std::numeric_limits<std::size_t>::max())
    return std::unexpected(Error::capacity);
  set_occupied_mask(grid, cls, occupied | (1u << selected.slot));
  return selected;
}

bool FiniteGroup::occupied(const Home& candidate) const noexcept {
  if (candidate.grid >= grid_count_ || candidate.cls >= class_count_ || candidate.slot >= slots_ ||
      ((candidate.slot << class_bits_) | candidate.cls) == unbound ||
      !(occupied_mask(candidate.grid, candidate.cls) & (1u << candidate.slot)))
    return false;
  const auto expected = home(candidate.grid, candidate.cls, candidate.slot);
  return candidate.capacity == expected.capacity && candidate.address == expected.address &&
      candidate.pages.first == expected.pages.first && candidate.pages.count == expected.pages.count;
}

bool FiniteGroup::bound(const Home& candidate) const noexcept {
  const auto coordinate = static_cast<std::uint8_t>((candidate.slot << class_bits_) | candidate.cls);
  auto identities = identity_mask(candidate.grid);
  while (identities) {
    const auto identity = std::countr_zero(identities);
    if (load(bindings_[(candidate.grid << slot_bits_) + identity]) == coordinate) return true;
    identities &= static_cast<std::uint16_t>(identities - 1);
  }
  return false;
}

Result<void> FiniteGroup::release_home(const Home& candidate) noexcept {
  if (!occupied(candidate)) return std::unexpected(Error::invalid_argument);
  // Binding and pending preparation each retain the physical coordinate.
  if ((pending_mask(candidate.grid, candidate.cls) & (1u << candidate.slot)) || bound(candidate))
    return std::unexpected(Error::conflict);
  set_occupied_mask(candidate.grid, candidate.cls,
      occupied_mask(candidate.grid, candidate.cls) & ~(1u << candidate.slot));
  return {};
}

Result<void> FiniteGroup::bind(Identity identity, const Home& candidate) noexcept {
  if (!valid(identity) || !occupied(candidate) || grid_of(identity) != candidate.grid)
    return std::unexpected(Error::invalid_argument);
  if (pending_mask(candidate.grid, candidate.cls) & (1u << candidate.slot))
    return std::unexpected(Error::unavailable);
  for (auto page = candidate.pages.first; page < candidate.pages.first + candidate.pages.count; ++page)
    if (pages_[page] != Page::mapped)
      return std::unexpected(Error::unavailable);
  const auto coordinate = static_cast<std::uint8_t>((candidate.slot << class_bits_) | candidate.cls);
  auto identities = identity_mask(candidate.grid);
  while (identities) {
    const auto other = std::countr_zero(identities);
    if ((candidate.grid << slot_bits_) + other != identity.index &&
        load(bindings_[(candidate.grid << slot_bits_) + other]) == coordinate)
      return std::unexpected(Error::conflict);
    identities &= static_cast<std::uint16_t>(identities - 1);
  }
  store(bindings_[identity.index], coordinate);
  return {};
}

Result<Home> FiniteGroup::bound_home(Identity identity) const noexcept {
  if (identity.index >= identities_) return std::unexpected(Error::invalid_argument);
  const auto coordinate = load(bindings_[identity.index]);
  if (coordinate == unbound) return std::unexpected(Error::unavailable);
  return home(grid_of(identity), coordinate & class_mask_, coordinate >> class_bits_);
}

std::byte* FiniteGroup::resolve(Identity identity) const noexcept {
  if (identity.index >= identities_)
    return nullptr;
  const auto coordinate = load(bindings_[identity.index]);
  if (coordinate == unbound)
    return nullptr;
  const auto cls = coordinate & class_mask_;
  const auto slot = coordinate >> class_bits_;
  return payload_ + offsets_[cls] + ((identity.index & ~(slots_ - 1)) + slot) * capacities_[cls];
}

const std::uint8_t* FiniteGroup::descriptor(Identity identity) const noexcept {
  return identity.index < identities_ ? bindings_ + identity.index : nullptr;
}

bool FiniteGroup::valid(PageSpan span) const noexcept {
  return span.count && span.first < payload_pages_ && span.count <= payload_pages_ - span.first;
}

std::byte* FiniteGroup::page_address(std::size_t page) const noexcept {
  return page < payload_pages_ ? payload_ + page * native_page_ : nullptr;
}

bool FiniteGroup::mapped(std::size_t page) const noexcept {
  return page < payload_pages_ && pages_[page] == Page::mapped;
}

bool FiniteGroup::trimming(std::size_t page) const noexcept {
  return page < payload_pages_ && pages_[page] == Page::trimming;
}

bool FiniteGroup::preparing(std::size_t page) const noexcept {
  return page < payload_pages_ && pages_[page] == Page::preparing;
}

bool FiniteGroup::pending(const Home& candidate) const noexcept {
  return occupied(candidate) &&
      (pending_mask(candidate.grid, candidate.cls) & (1u << candidate.slot));
}

Result<std::size_t> FiniteGroup::begin_prepare(const Home& candidate,
                                             std::span<PageSpan> output) noexcept {
  if (!occupied(candidate)) return std::unexpected(Error::invalid_argument);
  if ((pending_mask(candidate.grid, candidate.cls) & (1u << candidate.slot)) || bound(candidate))
    return std::unexpected(Error::conflict);
  std::size_t runs = 0;
  bool absent = false;
  const auto end = candidate.pages.first + candidate.pages.count;
  for (auto page = candidate.pages.first; page < end; ++page) {
    const auto state = pages_[page];
    if (state != Page::absent && state != Page::mapped)
      return std::unexpected(Error::conflict);
    const bool missing = state == Page::absent;
    runs += missing && !absent;
    absent = missing;
  }
  if (output.size() < runs) return std::unexpected(Error::invalid_argument);
  std::size_t used = 0;
  for (auto page = candidate.pages.first; page < end;) {
    if (pages_[page] == Page::mapped) { ++page; continue; }
    const auto first = page;
    while (page < end && pages_[page] == Page::absent) {
      pages_[page++] = Page::preparing;
      ++preparing_pages_;
    }
    output[used++] = {first, page - first};
  }
  set_pending_mask(candidate.grid, candidate.cls, pending_mask(candidate.grid, candidate.cls) | (1u << candidate.slot));
  return used;
}

Result<std::size_t> FiniteGroup::preparation_spans(const Home& candidate,
                                                  std::span<const PageSpan> spans) const noexcept {
  if (!occupied(candidate)) return std::unexpected(Error::invalid_argument);
  if (!(pending_mask(candidate.grid, candidate.cls) & (1u << candidate.slot)))
    return std::unexpected(Error::conflict);
  const auto end = candidate.pages.first + candidate.pages.count;
  auto previous_end = candidate.pages.first;
  std::size_t count = 0;
  for (auto span : spans) {
    if (!valid(span) || span.first < previous_end || span.first < candidate.pages.first ||
        span.first >= end || span.count > end - span.first)
      return std::unexpected(Error::invalid_argument);
    previous_end = span.first + span.count;
    for (auto page = span.first; page < previous_end; ++page)
      if (pages_[page] != Page::preparing) return std::unexpected(Error::conflict);
    count += span.count;
  }
  return count;
}

Result<void> FiniteGroup::finish_prepare(const Home& candidate,
                                        std::span<const PageSpan> spans,
                                        bool prepared) noexcept {
  const auto checked = preparation_spans(candidate, spans);
  if (!checked) return std::unexpected(checked.error());
  const auto count = *checked;
  const auto end = candidate.pages.first + candidate.pages.count;
  std::size_t expected = 0;
  for (auto page = candidate.pages.first; page < end; ++page)
    expected += pages_[page] == Page::preparing;
  if (count != expected) return std::unexpected(Error::invalid_argument);
  if (!prepared) return {};
  for (auto span : spans)
    for (auto page = span.first; page < span.first + span.count; ++page)
      pages_[page] = Page::mapped;
  mapped_pages_ += count;
  preparing_pages_ -= count;
  set_pending_mask(candidate.grid, candidate.cls, pending_mask(candidate.grid, candidate.cls) & ~(1u << candidate.slot));
  return {};
}

Result<void> FiniteGroup::discard_preparation(const Home& candidate,
                                             std::span<const PageSpan> spans) noexcept {
  const auto checked = preparation_spans(candidate, spans);
  if (!checked) return std::unexpected(checked.error());
  for (auto span : spans)
    for (auto page = span.first; page < span.first + span.count; ++page)
      pages_[page] = Page::absent;
  preparing_pages_ -= *checked;
  return {};
}

Result<void> FiniteGroup::end_prepare(const Home& candidate) noexcept {
  if (!occupied(candidate)) return std::unexpected(Error::invalid_argument);
  if (!(pending_mask(candidate.grid, candidate.cls) & (1u << candidate.slot)))
    return std::unexpected(Error::conflict);
  set_pending_mask(candidate.grid, candidate.cls, pending_mask(candidate.grid, candidate.cls) & ~(1u << candidate.slot));
  return {};
}

Result<void> FiniteGroup::mark_mapped(PageSpan span) noexcept {
  if (!valid(span))
    return std::unexpected(Error::invalid_argument);
  for (auto page = span.first; page < span.first + span.count; ++page)
    if (pages_[page] == Page::trimming || pages_[page] == Page::preparing)
      return std::unexpected(Error::conflict);
  for (auto page = span.first; page < span.first + span.count; ++page) {
    if (pages_[page] == Page::absent)
      ++mapped_pages_;
    pages_[page] = Page::mapped;
  }
  return {};
}

Result<void> FiniteGroup::live_pages(std::span<std::uint64_t> live) const noexcept {
  if (live.size() < live_words())
    return std::unexpected(Error::invalid_argument);
  std::fill(live.begin(), live.end(), 0);
  for (std::size_t grid = 0; grid < grid_count_; ++grid) {
    // Homes may outlive every identity in this grid.
    for (unsigned cls = 0; cls < class_count_; ++cls) {
      auto occupied = occupied_mask(grid, cls);
      while (occupied) {
        const auto slot = static_cast<unsigned>(std::countr_zero(occupied));
        const auto coverage = home(grid, cls, slot).pages;
        for (auto page = coverage.first; page < coverage.first + coverage.count; ++page)
          live[page / 64] |= std::uint64_t{1} << (page % 64);
        occupied &= static_cast<std::uint16_t>(occupied - 1);
      }
    }
  }
  return {};
}

Result<void> FiniteGroup::begin_trim() noexcept {
  const auto membership = std::span<const std::uint64_t>(trim_membership());
  const auto tail = payload_pages_ % 64;
  if (tail && (membership.back() >> tail)) return std::unexpected(Error::invalid_argument);
  const auto live = std::span(trim_scratch_, live_words());
  const auto counted = live_pages(live);
  if (!counted) return std::unexpected(counted.error());
  std::size_t cursor = 0;
  PageSpan span;
  while (next_trim_span(membership, cursor, span))
    for (auto page = span.first; page < span.first + span.count; ++page)
      if (pages_[page] != Page::mapped || (live[page / 64] & (std::uint64_t{1} << (page % 64))))
        return std::unexpected(Error::conflict);
  // Admit the complete immutable selection before changing any page state.
  cursor = 0;
  while (next_trim_span(membership, cursor, span))
    for (auto page = span.first; page < span.first + span.count; ++page)
      pages_[page] = Page::trimming;
  return {};
}

Result<void> FiniteGroup::begin_trim(PageSpan span) noexcept {
  return begin_trim(std::span(&span, 1));
}

Result<void> FiniteGroup::begin_trim(std::span<const PageSpan> spans) noexcept {
  const auto live = std::span(trim_scratch_, live_words());
  const auto counted = live_pages(live);
  assert(counted);
  for (auto span : spans) {
    if (!valid(PageSpan{span.first, span.count})) return std::unexpected(Error::invalid_argument);
    for (auto page = span.first; page < span.first + span.count; ++page)
      if (pages_[page] != Page::mapped || (live[page / 64] & (std::uint64_t{1} << (page % 64))))
        return std::unexpected(Error::conflict);
  }
  for (auto span : spans)
    for (auto page = span.first; page < span.first + span.count; ++page)
      pages_[page] = Page::trimming;
  return {};
}

Result<void> FiniteGroup::finish_trim(PageSpan span, bool discarded) noexcept {
  if (!valid(span))
    return std::unexpected(Error::invalid_argument);
  for (auto page = span.first; page < span.first + span.count; ++page)
    if (pages_[page] != Page::trimming)
      return std::unexpected(Error::conflict);
  for (auto page = span.first; page < span.first + span.count; ++page)
    pages_[page] = discarded ? Page::absent : Page::mapped;
  if (discarded)
    mapped_pages_ -= span.count;
  return {};
}

} // namespace orbital::allocation::palette_experiment
