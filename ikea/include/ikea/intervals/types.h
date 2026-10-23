#pragma once
#include <ikea/bitset256.h>
#include <ikea/intervals/detail/bytes.h>
#include <algorithm>
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <type_traits>

namespace ikea::intervals {
/// Each source has at most UINT_MAX assigned ranks, with count also the absent
/// sentinel. Larger directories compose local sources with wider outer indices.
inline constexpr unsigned maximum_count = std::numeric_limits<unsigned>::max();
enum class edge { at_or_after, after };
struct rank_window {
    unsigned first, limit;
    bool empty() const noexcept {
        return first == limit;
    }
    auto operator<=>(const rank_window&) const = default;
};
/// Coordinates of one contiguous half-open interval.
template <class Point> struct interval {
    Point first, limit;
};
enum class error { description, capacity, alignment, overlap, contents, value, effects, range };
/// Cold diagnostic text for this module's typed errors.
std::string_view describe(error) noexcept;
namespace detail {
template <std::integral A, std::integral B> constexpr bool integer_less(A a, B b) {
    if constexpr (std::is_signed_v<A> == std::is_signed_v<B>)
        return a < b;
    else if constexpr (std::is_signed_v<A>)
        return a < 0 || std::make_unsigned_t<A>(a) < b;
    else
        return b >= 0 && a < std::make_unsigned_t<B>(b);
}
template <edge E, class T, class Q> constexpr bool meets(const T& value, const Q& point) {
    if constexpr (std::integral<T> && std::integral<Q>) {
        if constexpr (E == edge::after)
            return integer_less(point, value);
        else
            return !integer_less(value, point);
    } else {
        if constexpr (E == edge::after)
            return point < value;
        else
            return !(value < point);
    }
}
inline std::uint64_t low_bits(unsigned n) {
    return n == 64 ? ~std::uint64_t{} : (std::uint64_t{1} << n) - 1;
}
/// Classification precedes conversion: queries outside a stored lane's domain
/// must keep their order. The returned value is used only when relation == 0.
template <std::unsigned_integral T, std::integral Q> struct prepared_query {
    int relation;
    T value;
    explicit prepared_query(Q point)
        : relation(integer_less(point, 0)                               ? -1
                   : integer_less(std::numeric_limits<T>::max(), point) ? 1
                                                                        : 0),
          value(relation ? T{} : T(point)) {}
};
} // namespace detail

/// Borrowed original-rank selection. Byte backing is little-endian within each
/// byte and needs exactly ceil(covered_positions/8) readable bytes, without word
/// padding. A bitset256 borrows numeric words under its own endian-independent
/// position law. Keep backing stable; this is eligibility, never ownership.
/// Default construction selects none. Named all/none need no bitmap storage.
class selection {
    enum class kind { all, none, bits, complement, block, complement_block };
    kind kind_ = kind::none;
    std::size_t first_ = 0;
    const void* data_ = nullptr;
    std::size_t bytes_ = 0;
    selection(kind type, std::size_t first, const void* data, std::size_t bytes) noexcept
        : kind_(type), first_(first), data_(data), bytes_(bytes) {}
    bool inverted() const noexcept {
        return kind_ == kind::complement || kind_ == kind::complement_block;
    }
    bool block_backing() const noexcept {
        if constexpr (std::endian::native == std::endian::little)
            return false;
        else
            return kind_ == kind::block || kind_ == kind::complement_block;
    }
    static std::uint64_t load_bytes(const std::byte* p, unsigned take) noexcept {
        if (take == 8)
            return detail::load<std::uint64_t>(p);
        std::uint64_t word = 0;
        unsigned i = 0;
        if (take >= 4) {
            word = detail::load<std::uint32_t>(p);
            i = 4;
        }
        if (take - i >= 2) {
            word |= std::uint64_t(detail::load<std::uint16_t>(p + i)) << (i * 8);
            i += 2;
        }
        if (take - i)
            word |= std::uint64_t(std::to_integer<unsigned>(p[i])) << (i * 8);
        return word;
    }
    unsigned end(unsigned n) const noexcept {
        if (first_ >= n)
            return n;
        const auto distance = std::size_t(n) - first_;
        return bytes_ >= (distance / 8 + (distance % 8 != 0)) ? n : unsigned(first_ + bytes_ * 8);
    }

  public:
    selection() = default;
    static selection all() noexcept { return {kind::all, 0, nullptr, 0}; }
    static selection none() noexcept { return {}; }
    static selection bits(std::size_t first, std::span<const std::byte> bytes) noexcept {
        return {kind::bits, first, bytes.data(), bytes.size()};
    }
    /// Set bits in an owner's tombstone plane exclude those ranks.
    static selection complement(std::size_t first, std::span<const std::byte> bytes) noexcept {
        return {kind::complement, first, bytes.data(), bytes.size()};
    }
    static selection bits(std::size_t first, const bitset256& block) noexcept {
        if constexpr (std::endian::native == std::endian::little)
            return bits(first, std::as_bytes(std::span(block.words)));
        else
            return {kind::block, first, &block, 32};
    }
    static selection complement(std::size_t first, const bitset256& block) noexcept {
        if constexpr (std::endian::native == std::endian::little)
            return complement(first, std::as_bytes(std::span(block.words)));
        else
            return {kind::complement_block, first, &block, 32};
    }
    static selection bits(std::size_t, const bitset256&&) = delete;
    static selection complement(std::size_t, const bitset256&&) = delete;
    bool is_all() const noexcept { return kind_ == kind::all; }
    bool is_none() const noexcept { return kind_ == kind::none; }
    bool covers(std::size_t first, std::size_t count) const noexcept {
        if (!count)
            return true;
        if (count - 1 > std::numeric_limits<std::size_t>::max() - first)
            return false;
        if (is_all() || is_none())
            return true;
        return first >= first_ && (first + (count - 1) - first_) / 8 < bytes_;
    }
    /// Trusted covered original rank, after covers(rank,1).
    [[gnu::always_inline]] bool contains(std::size_t rank) const noexcept {
        if (is_all() || is_none())
            return is_all();
        const auto offset = rank - first_;
        const bool bit = block_backing()
            ? static_cast<const bitset256*>(data_)->contains(unsigned(offset))
            : bool(std::to_integer<unsigned>(static_cast<const std::byte*>(data_)[offset / 8]) &
                   (1u << (offset % 8)));
        return bit != inverted();
    }
    /// Trusted covered window of 0..64 ranks. Bit r names first+r; arbitrary
    /// origins and windows crossing byte/word boundaries retain exact reads.
    [[gnu::always_inline]] std::uint64_t mask(std::size_t first, unsigned count) const noexcept {
        assert(count <= 64);
        if (!count || is_none())
            return 0;
        const auto limit = detail::low_bits(count);
        if (is_all())
            return limit;
        const auto offset = first - first_;
        std::uint64_t word;
        if (block_backing())
            word = static_cast<const bitset256*>(data_)->mask(unsigned(offset), count);
        else {
            const unsigned shift = offset % 8, take = (shift + count + 7) / 8;
            const auto* p = static_cast<const std::byte*>(data_) + offset / 8;
            word = load_bytes(p, std::min(take, 8u)) >> shift;
            if (take == 9)
                word |= std::uint64_t(std::to_integer<unsigned>(p[8])) << (64 - shift);
        }
        return (inverted() ? ~word : word) & limit;
    }
    /// First selected rank at or after first and below n, or n when absent.
    /// Positions outside explicit bitmap coverage are skipped without access.
    [[gnu::always_inline]] unsigned next(unsigned first, unsigned n) const noexcept {
        if (is_all() || is_none())
            return is_all() ? std::min(first, n) : n;
        if (first_ >= n)
            return n;
        first = std::max(first, unsigned(first_));
        const auto limit = end(n);
        while (first < limit) {
            const auto aligned = first / 64 * 64;
            const auto base = std::max(aligned, unsigned(first_));
            const auto take = std::min(64u - (base - aligned), limit - base);
            const auto bits = mask(base, take) & ~detail::low_bits(first - base);
            if (bits)
                return base + std::countr_zero(bits);
            first = base + take;
        }
        return n;
    }
    /// Last selected rank below limit and n, or n when absent.
    [[gnu::always_inline]] unsigned previous(unsigned limit, unsigned n) const noexcept {
        limit = std::min(limit, n);
        if (is_all() || is_none())
            return is_all() && limit ? limit - 1 : n;
        if (first_ >= limit)
            return n;
        limit = end(limit);
        while (limit > first_) {
            const auto base = std::max((limit - 1) / 64 * 64, unsigned(first_));
            const auto bits = mask(base, limit - base);
            if (bits)
                return base + 63 - std::countl_zero(bits);
            limit = base;
        }
        return n;
    }
};
} // namespace ikea::intervals
