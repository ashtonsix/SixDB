#pragma once

// Fixed-size-class Group storage, with one-byte slot bindings.
// finite.cpp owns geometry, slot/page state and VM reservations;
// group.cpp adds allocation, preparation and reclamation transactions.

#include <orbital/types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace orbital::allocation::detail {

enum class Palette : std::uint8_t { a, b };

[[nodiscard]] constexpr Palette preferred_palette(std::size_t page, std::size_t bytes,
                                                 std::size_t alignment) noexcept {
  // 256 identities × 256 B fill one 64 KiB page, including mixed tiny sizes.
  return page >= 65536 || alignment >= 64 || bytes > 192 ? Palette::b : Palette::a;
}

struct FiniteConfig {
  Palette palette{Palette::a};
  std::uint16_t identities{256};
};

struct FiniteQualification;

struct Identity {
  std::uint16_t index{};
  auto operator<=>(const Identity&) const = default;
};

struct PageSpan {
  std::size_t first{}, count{};
};

// Enumerate maximal runs, including those crossing uint64 word boundaries.
inline bool next_trim_span(std::span<const std::uint64_t> bits,
    std::size_t& cursor, PageSpan& span) noexcept {
  const auto end = bits.size() * 64;
  while (cursor < end) {
    const auto word = bits[cursor / 64] & (~std::uint64_t{0} << (cursor % 64));
    if (word) { cursor = cursor / 64 * 64 + std::countr_zero(word); break; }
    cursor = (cursor / 64 + 1) * 64;
  }
  if (cursor == end) return false;
  const auto first = cursor;
  while (cursor < end) {
    const auto offset = cursor % 64;
    const auto count = static_cast<std::size_t>(std::countr_one(bits[cursor / 64] >> offset));
    cursor += count;
    if (count < 64 - offset) break;
  }
  span = {first, cursor - first};
  return true;
}

inline void set_trim_span(std::span<std::uint64_t> bits, PageSpan span, bool set) noexcept {
  const auto end = span.first + span.count;
  for (auto page = span.first; page < end;) {
    const auto offset = page % 64;
    const auto count = std::min(end - page, 64 - offset);
    const auto mask = count == 64 ? ~std::uint64_t{0}
        : ((std::uint64_t{1} << count) - 1) << offset;
    if (set) bits[page / 64] |= mask;
    else bits[page / 64] &= ~mask;
    page += count;
  }
}

struct Home {
  std::uint16_t grid{};
  std::uint8_t cls{}, slot{};
  std::size_t capacity{};
  std::byte* address{};
  PageSpan pages;
};

/// Fixed palette, binding plane and page state of one Group.
/// Bindings locate storage; the shared allocation code manages physical uses.
class FiniteGroup {
public:
  // Computes geometry without VM ownership. The allocator retains and charges its Group
  // before helper preparation; geometry stays private until completion.
  [[nodiscard]] static Result<std::unique_ptr<FiniteGroup>> create(FiniteConfig = {}) noexcept;
  [[nodiscard]] Result<void> prepare() noexcept;
  ~FiniteGroup();
  // The allocator ends helper work and quarantines the group before retirement. Partial
  // failure retains remaining mappings for retry; no geometry operation may follow.
  [[nodiscard]] Result<void> retire() noexcept;
  FiniteGroup(const FiniteGroup&) = delete;
  FiniteGroup& operator=(const FiniteGroup&) = delete;

  [[nodiscard]] Result<Identity> claim_identity(std::uint64_t excluded_grids = 0) noexcept;
  [[nodiscard]] bool has_identity() const noexcept { return available_identity_grids_ != 0; }

  [[nodiscard]] Result<void> release_identity(Identity) noexcept;
  [[nodiscard]] Result<Home> claim_home(Identity, std::size_t bytes,
                                      std::size_t alignment) noexcept;
  // Select once while trying several identity grids in the same Group.
  [[nodiscard]] Result<std::uint8_t> class_for(std::size_t bytes) const noexcept;
  [[nodiscard]] Result<Home> claim_home_class(Identity, std::uint8_t cls,
                                            std::size_t alignment) noexcept;
  [[nodiscard]] Result<void> release_home(const Home&) noexcept;
  [[nodiscard]] Result<void> bind(Identity, const Home&) noexcept;
  // Protected identity only. Read path touches its binding and immutable
  // geometry, never allocator occupancy; stale aliases are not reopened here.
  [[nodiscard]] std::byte* resolve(Identity) const noexcept;
  [[nodiscard]] Result<Home> bound_home(Identity) const noexcept;
  [[nodiscard]] std::size_t bound_capacity(Identity identity) const noexcept {
    if (identity.index >= identities_) return 0;
    const auto coordinate = std::atomic_ref<std::uint8_t>(bindings_[identity.index])
        .load(std::memory_order_acquire);
    return coordinate == unbound ? 0 : capacities_[coordinate & 15];
  }

  [[nodiscard]] const std::uint8_t* descriptor(Identity) const noexcept;

  [[nodiscard]] std::size_t class_minimum(std::uint8_t cls) const noexcept;

  [[nodiscard]] std::size_t native_page() const noexcept { return native_page_; }

  [[nodiscard]] std::size_t host_page() const noexcept { return host_page_; }
  [[nodiscard]] constexpr std::size_t slots_per_grid() const noexcept { return 16; }
  [[nodiscard]] constexpr std::size_t class_count() const noexcept { return 15; }
  [[nodiscard]] constexpr std::size_t grid_of(Identity id) const noexcept { return id.index >> 4; }
  [[nodiscard]] std::size_t capacity(std::size_t cls) const noexcept {
    return cls < capacities_.size() ? capacities_[cls] : 0;
  }

  [[nodiscard]] std::size_t identity_count() const noexcept { return identities_; }

  [[nodiscard]] std::size_t payload_pages() const noexcept { return payload_pages_; }

  [[nodiscard]] std::size_t reserved_bytes() const noexcept { return reservation_ ? reserved_bytes_ : 0; }

  // Exact requested bytes of the stable writable owner metadata heap block.
  // The separately allocated sizeof(FiniteGroup) is not included.
  [[nodiscard]] std::size_t control_bytes() const noexcept { return control_bytes_; }

  [[nodiscard]] std::size_t mapped_pages() const noexcept { return mapped_pages_; }

  [[nodiscard]] std::byte* page_address(std::size_t) const noexcept;
  [[nodiscard]] bool mapped(std::size_t) const noexcept;
  [[nodiscard]] bool trimming(std::size_t) const noexcept;
  [[nodiscard]] bool preparing(std::size_t) const noexcept;
  [[nodiscard]] bool pending(const Home&) const noexcept;
  [[nodiscard]] std::size_t preparing_pages() const noexcept { return preparing_pages_; }

  // Owner reserves absent pages and the Home before dispatch. The helper uses
  // the returned immutable spans and initializes only the requested Home bytes.
  // Only the owner finishes after VM and initialization settle. Failure keeps
  // both reservations for a same-spans retry or confirmed physical discard.
  [[nodiscard]] Result<std::size_t> begin_prepare(const Home&, std::span<PageSpan>) noexcept;
  [[nodiscard]] Result<void> finish_prepare(const Home&, std::span<const PageSpan>,
                                          bool prepared) noexcept;
  // Only confirmed DONTNEED + PROT_NONE bounds become absent/refundable. The
  // Home remains pending while the caller's record retains unsettled bounds.
  [[nodiscard]] Result<void> discard_preparation(const Home&, std::span<const PageSpan>) noexcept;
  // Owner calls after helper work ends and every original span is settled;
  // already-discarded pages may now belong to a different preparation.
  [[nodiscard]] Result<void> end_prepare(const Home&) noexcept;

  // The allocator completes mappings/charges before marking them ready. Trim reserves
  // empty mapped pages first, excluding new claims until completion or failure.
  [[nodiscard]] Result<void> mark_mapped(PageSpan) noexcept;
  [[nodiscard]] Result<void> begin_trim(PageSpan) noexcept;
  [[nodiscard]] Result<void> begin_trim(std::span<const PageSpan>) noexcept;
  // Separate from owner scratch. Membership remains immutable through helper
  // completion; failed membership survives reply/retry. Success is helper-only
  // until reply and is cleared by the owner before dispatch.
  [[nodiscard]] std::span<std::uint64_t> trim_membership() noexcept {
    return {trim_scratch_ + live_words(), live_words()};
  }
  [[nodiscard]] std::span<const std::uint64_t> trim_membership() const noexcept {
    return {trim_scratch_ + live_words(), live_words()};
  }
  [[nodiscard]] std::span<std::uint64_t> trim_success() noexcept {
    return {trim_scratch_ + 2 * live_words(), live_words()};
  }
  [[nodiscard]] bool has_trim() const noexcept {
    for (const auto word : trim_membership()) if (word) return true;
    return false;
  }
  [[nodiscard]] Result<void> begin_trim() noexcept;
  [[nodiscard]] Result<void> finish_trim(PageSpan, bool discarded) noexcept;
  [[nodiscard]] std::size_t live_words() const noexcept { return (payload_pages_ + 63) / 64; }

  [[nodiscard]] Result<void> live_pages(std::span<std::uint64_t>) const noexcept;

private:
  friend struct FiniteQualification;
  [[nodiscard]] static Result<std::unique_ptr<FiniteGroup>> create_geometry(
      FiniteConfig, std::size_t page_granule) noexcept;
  static constexpr std::uint8_t unbound = 0xff;
  FiniteGroup() = default;
  struct Grid {
    std::uint16_t identities{};
    std::array<std::uint16_t, 15> occupied{};
    std::array<std::uint16_t, 15> pending{};
  };
  enum class Page : std::uint8_t { absent, mapped, trimming, preparing };
  [[nodiscard]] bool valid(Identity) const noexcept;
  [[nodiscard]] bool occupied(const Home&) const noexcept;
  [[nodiscard]] bool bound(const Home&) const noexcept;
  [[nodiscard]] bool valid(PageSpan) const noexcept;
  [[nodiscard]] Result<std::size_t> preparation_spans(
      const Home&, std::span<const PageSpan>) const noexcept;
  [[nodiscard]] Home home(std::size_t grid, unsigned cls, unsigned slot) const noexcept;

  std::byte* reservation_{};
  std::byte* payload_{};
  std::uint8_t* bindings_{};
  void* metadata_{};
  std::size_t metadata_bytes_{};
  Grid* grids_{};
  Page* pages_{};
  std::uint64_t* trim_scratch_{};
  std::size_t native_page_{}, host_page_{}, reserved_bytes_{}, control_bytes_{};
  std::size_t identities_{}, grid_count_{}, payload_pages_{};
  std::array<std::uint16_t, 15> capacities_{};
  std::array<std::uint32_t, 15> offsets_{};
  const std::uint8_t* class_lookup_{};
  std::uint8_t class_lookup_shift_{};
  alignas(64) std::uint64_t available_identity_grids_{};
  std::size_t mapped_pages_{}, preparing_pages_{};
};

} // namespace orbital::allocation::detail
