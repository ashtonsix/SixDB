#pragma once

// Single-class Group geometry: 256 equal Homes, one per identity, with free,
// ready and pending bitmaps and per-page backing state.
// finite.cpp owns geometry and VM reservations; group.cpp adds allocation,
// preparation and reclamation transactions.

#include <orbital/types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace orbital::allocation::detail {

inline constexpr std::size_t finite_homes = 256;
inline constexpr std::size_t finite_words = finite_homes / 64;
inline constexpr std::size_t finite_cutoff = 3072;
inline constexpr std::array<std::uint16_t, 26> finite_capacities{
    16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512,
    640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2560, 3072};
inline constexpr unsigned finite_classes = finite_capacities.size();

inline constexpr auto finite_lookup = [] {
  std::array<std::uint8_t, finite_cutoff / 16> result{};
  unsigned cls = 0;
  for (unsigned i = 0; i < result.size(); ++i) {
    while (16 * (i + 1) > finite_capacities[cls]) ++cls;
    result[i] = static_cast<std::uint8_t>(cls);
  }
  return result;
}();

/// Smallest class whose 256 Homes fill at least one native page, so a Group
/// never maps a page it cannot use: 16 B at 4 KiB, 64 B at 16 KiB, 256 B at 64 KiB.
[[nodiscard]] constexpr unsigned first_class(std::size_t page) noexcept {
  unsigned cls = 0;
  while (cls + 1 < finite_classes && finite_capacities[cls] * finite_homes < page) ++cls;
  return cls;
}

/// Class for a request, or finite_classes when none fits. Homes sit at
/// page-aligned base + index × capacity, so alignment a ≤ page holds exactly
/// when capacity is a multiple of a.
[[nodiscard, gnu::always_inline]] inline unsigned select_class(std::size_t page, std::size_t bytes,
    std::size_t alignment, unsigned floor) noexcept {
  if (!bytes || bytes > finite_cutoff || alignment > page) [[unlikely]] return finite_classes;
  unsigned cls = std::max<unsigned>(finite_lookup[(bytes - 1) >> 4], floor);
  // Every capacity is a multiple of 16; only stronger alignment can skip classes.
  if (alignment > 16) [[unlikely]]
    while (cls < finite_classes && (finite_capacities[cls] & (alignment - 1))) ++cls;
  return cls;
}
[[nodiscard]] inline unsigned select_class(std::size_t page, std::size_t bytes, std::size_t alignment) noexcept {
  return select_class(page, bytes, alignment, first_class(page));
}

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

/// One size class, 256 Homes. Identity i always means Home i. A Home is free,
/// pending (reserved by a preparation) or bound to a live identity. Ready
/// Homes are free with every covering page mapped.
class FiniteGroup {
public:
  // Computes geometry without VM ownership. The allocator retains and charges its Group
  // before helper preparation; geometry stays private until completion.
  [[nodiscard]] static Result<std::unique_ptr<FiniteGroup>> create(unsigned cls,
      std::size_t page_granule = 0) noexcept;
  [[nodiscard]] Result<void> prepare() noexcept;
  ~FiniteGroup();
  // The allocator ends helper work and quarantines the group before retirement. Partial
  // failure retains remaining mappings for retry; no geometry operation may follow.
  [[nodiscard]] Result<void> retire() noexcept;
  FiniteGroup(const FiniteGroup&) = delete;
  FiniteGroup& operator=(const FiniteGroup&) = delete;

  [[nodiscard]] unsigned cls() const noexcept { return cls_; }
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::byte* address(std::size_t home) const noexcept { return payload_ + home * capacity_; }
  [[nodiscard]] PageSpan pages(std::size_t home) const noexcept {
    const auto first = home * capacity_ >> page_shift_;
    const auto last = ((home + 1) * capacity_ - 1) >> page_shift_;
    return {first, last - first + 1};
  }

  [[nodiscard]] bool has_free() const noexcept { return any(free_); }
  [[nodiscard]] bool has_ready() const noexcept { return any(ready_); }
  [[nodiscard]] bool is_free(std::size_t home) const noexcept { return test(free_, home); }
  [[nodiscard]] std::size_t free_count() const noexcept { return count(free_); }

  /// Lowest ready Home, now bound to its identity; finite_homes if none.
  [[gnu::always_inline]] std::size_t take_ready() noexcept {
    for (std::size_t w = 0; w < finite_words; ++w)
      if (const auto word = ready_[w]) {
        const auto bit = static_cast<std::size_t>(std::countr_zero(word));
        ready_[w] = word & (word - 1);
        free_[w] &= ~(std::uint64_t{1} << bit);
        return w * 64 + bit;
      }
    return finite_homes;
  }
  /// Return a bound Home. It is ready again: a bound Home's pages stay mapped.
  [[gnu::always_inline]] void give(std::size_t home) noexcept {
    free_[home / 64] |= std::uint64_t{1} << (home % 64);
    ready_[home / 64] |= std::uint64_t{1} << (home % 64);
  }
  /// Lowest free Home whose pages are not all mapped; finite_homes if none.
  [[nodiscard]] std::size_t first_unready() const noexcept;
  /// Bind a specific free Home whose pages are all mapped.
  void claim(std::size_t home) noexcept;

  // Preparation reserves a free Home: it leaves the free set until finish or
  // discard. Absent pages become preparing; other homes cannot claim them.
  [[nodiscard]] Result<std::size_t> begin_prepare(std::size_t home, std::span<PageSpan>) noexcept;
  [[nodiscard]] Result<void> finish_prepare(std::size_t home, std::span<const PageSpan>,
      bool prepared) noexcept;
  // Only confirmed DONTNEED + PROT_NONE bounds become absent/refundable.
  [[nodiscard]] Result<void> discard_preparation(std::size_t home, std::span<const PageSpan>) noexcept;
  // Owner calls after helper work ends and every original span is settled;
  // the Home becomes free again.
  void end_prepare(std::size_t home) noexcept;
  /// Pending Home becomes bound to its identity after a successful preparation.
  void bind_prepared(std::size_t home) noexcept;
  [[nodiscard]] bool pending(std::size_t home) const noexcept { return test(pending_, home); }

  /// Mark absent/mapped pages mapped and refresh readiness of the free Homes they cover.
  [[nodiscard]] Result<void> mark_mapped(PageSpan) noexcept;
  /// Absent pages contiguous from first, at most limit of them.
  [[nodiscard]] std::size_t absent_run(std::size_t first, std::size_t limit) const noexcept;

  // Trim reserves empty mapped pages first, excluding new claims until completion
  // or failure. Membership stays immutable through helper completion.
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
  /// Pages covered by any Home that is bound or pending.
  [[nodiscard]] Result<void> live_pages(std::span<std::uint64_t>) const noexcept;

  [[nodiscard]] std::size_t native_page() const noexcept { return native_page_; }
  [[nodiscard]] std::size_t host_page() const noexcept { return host_page_; }
  [[nodiscard]] std::size_t payload_pages() const noexcept { return payload_pages_; }
  [[nodiscard]] std::size_t reserved_bytes() const noexcept { return reservation_ ? reserved_bytes_ : 0; }
  [[nodiscard]] std::size_t control_bytes() const noexcept { return control_bytes_; }
  [[nodiscard]] std::size_t mapped_pages() const noexcept { return mapped_pages_; }
  [[nodiscard]] std::size_t preparing_pages() const noexcept { return preparing_pages_; }
  [[nodiscard]] std::byte* page_address(std::size_t page) const noexcept {
    return page < payload_pages_ ? payload_ + page * native_page_ : nullptr;
  }
  [[nodiscard]] bool mapped(std::size_t page) const noexcept { return page < payload_pages_ && pages_[page] == Page::mapped; }
  [[nodiscard]] bool trimming(std::size_t page) const noexcept { return page < payload_pages_ && pages_[page] == Page::trimming; }
  [[nodiscard]] bool preparing(std::size_t page) const noexcept { return page < payload_pages_ && pages_[page] == Page::preparing; }

private:
  FiniteGroup() = default;
  enum class Page : std::uint8_t { absent, mapped, trimming, preparing };
  using Bits = std::array<std::uint64_t, finite_words>;
  static bool any(const Bits& bits) noexcept { return (bits[0] | bits[1] | bits[2] | bits[3]) != 0; }
  static bool test(const Bits& bits, std::size_t i) noexcept { return bits[i / 64] >> (i % 64) & 1; }
  static std::size_t count(const Bits& bits) noexcept {
    std::size_t total = 0;
    for (auto word : bits) total += static_cast<std::size_t>(std::popcount(word));
    return total;
  }
  [[nodiscard]] bool home_mapped(std::size_t home) const noexcept;
  // Homes overlapping a page span: [first, last].
  [[nodiscard]] std::pair<std::size_t, std::size_t> homes(PageSpan) const noexcept;
  void refresh_ready(PageSpan) noexcept;

  // Hot owner state first: bitmaps and address arithmetic share two lines.
  alignas(64) Bits free_{}, ready_{};
  std::byte* payload_{};
  std::size_t capacity_{};
  unsigned cls_{}, page_shift_{};
  Bits pending_{};
  std::byte* reservation_{};
  Page* pages_{};
  std::uint64_t* trim_scratch_{};
  void* metadata_{};
  std::size_t native_page_{}, host_page_{}, reserved_bytes_{}, control_bytes_{};
  std::size_t payload_pages_{}, mapped_pages_{}, preparing_pages_{};
};

} // namespace orbital::allocation::detail
