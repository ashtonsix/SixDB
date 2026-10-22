#pragma once
#include "kernels.h"
#include <ikea/seriespack.h>
#include <array>
#include <cassert>
#include <utility>

namespace frame_filter {
namespace detail {
namespace sp = ikea::seriespack;
namespace cp = sp::composition;
using C = ikea::comparison;

template <unsigned K, unsigned H> using format = sp::format<K, sp::geometry::local, H>;
template <unsigned K, unsigned H, class Byte>
auto view(std::size_t n, Byte *payload, Byte *head0, Byte *head1) {
    using F = format<K, H>;
    const auto tiles = (n + 7) / 8;
    return sp::view<F, Byte>::assume_valid(n, {{{{payload, tiles * F::tile_bytes}, F::tile_bytes},
                                             {{head0, H >= 8 ? tiles * 8 : 0}, 8},
                                             {{head1, H == 16 ? tiles * 8 : 0}, 8}}});
}
template <unsigned K, unsigned H>
void pack(std::span<const u64> values, std::uint8_t *payload,
          std::uint8_t *head0, std::uint8_t *head1) {
    auto source = view<K, H>(values.size(), payload, head0, head1);
    for (std::size_t i = 0; i < values.size(); ++i)
        sp::set_unchecked(source, i, values[i]);
}

// A two-byte head is two successive lexicographic decisions. Compose the
// existing eight-bit guards; do not add another packed decoder or ISA body.
template <unsigned K, unsigned H, class E> auto guarded(E e) {
    if constexpr (H == 0 || K == 8)
        return e;
    else if constexpr (H == 8)
        return cp::guarded(e);
    else if constexpr (K == 16) {
        using Outer = cp::value_expression<K, 8, decltype(e.head1),
                                           decltype(e.head0), decltype(e.head1)>;
        return cp::guarded(Outer{e.head1, e.head0, e.head1});
    } else {
        using Inner = cp::value_expression<K - 8, 8, decltype(e.payload),
                                           decltype(e.head1), decltype(e.head1)>;
        auto inner = cp::guarded(Inner{e.payload, e.head1, e.head1});
        using Outer = cp::value_expression<K, 8, decltype(inner),
                                           decltype(e.head0), decltype(e.head1)>;
        return cp::guarded(Outer{inner, e.head0, e.head1});
    }
}
template <C Op, unsigned K, unsigned H>
u64 filter(planes p, std::size_t first, unsigned count, u64 rhs, bool guard) {
    if (!count) return 0;
    assert(count <= 64 && first <= p.count && count <= p.count - first);
    auto source = [&] {
        if constexpr (H == 0)
            return sp::dense_source<format<K, H>>{p.payload, p.count};
        else
            return view<K, H>(p.count, p.payload, p.head0, p.head1);
    }();
    const auto e = cp::describe(source);
    if (guard)
        return cp::filter_unchecked<Op>(guarded<K, H>(e), first, count, rhs);
    return cp::filter_unchecked<Op>(e, first, count, rhs);
}
template <unsigned K, unsigned H>
u64 dispatch(planes p, std::size_t first, unsigned count, u64 rhs, C op, bool guard) {
    switch (op) {
    case C::equal: return filter<C::equal, K, H>(p, first, count, rhs, guard);
    case C::less: return filter<C::less, K, H>(p, first, count, rhs, guard);
    case C::less_equal: return filter<C::less_equal, K, H>(p, first, count, rhs, guard);
    case C::greater: return filter<C::greater, K, H>(p, first, count, rhs, guard);
    case C::greater_equal: return filter<C::greater_equal, K, H>(p, first, count, rhs, guard);
    }
    std::unreachable();
}

// Full native windows only: these 16 endpoint bytes are inside the exact grant.
// End-minus-one subtraction wraps at the artificial initial predecessor 255;
// a 256-row single run takes the scalar tail (or the constant codec), not this
// byte-length path. No packed decoding algorithm is duplicated here.
[[gnu::always_inline]] inline u64 weighted16(const std::uint8_t *ends, unsigned first,
                                            std::uint16_t selected) {
    if (!selected) return 0;
    const auto previous = first ? ends[first - 1] : 255;
    if (selected == 0xffff)
        return unsigned(ends[first + 15]) + 1 - (first ? unsigned(previous) + 1 : 0);
#if defined(__aarch64__)
    const auto current = vld1q_u8(ends + first);
    const auto before = vextq_u8(vdupq_n_u8(previous), current, 15);
    const auto lengths = vsubq_u8(current, before);
    return sp::native::sum<8>(sp::native::values<8>{{lengths}}, sp::native::mask16<8>(selected)).finish();
#elif defined(__AVX2__)
    const auto current = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ends + first));
    const auto before = _mm_alignr_epi8(current, _mm_set1_epi8(previous), 15);
    const auto lengths = _mm_sub_epi8(current, before);
    return sp::native::sum<8>(sp::native::values<8>{{lengths}}, sp::native::mask16<8>(selected)).finish();
#else
    u64 result = 0;
    while (selected) {
        const auto rank = first + std::countr_zero(selected);
        result += unsigned(ends[rank]) + 1 - (rank ? unsigned(ends[rank - 1]) + 1 : 0);
        selected &= selected - 1;
    }
    return result;
#endif
}
template <class E>
[[gnu::always_inline]] inline u64 count_expression(const E &e, std::size_t count, u64 rhs,
                                                  const std::uint8_t *ends) {
    u64 result = 0;
    unsigned first = 0;
    if (!ends) {
        for (; first + 64 <= count; first += 64)
            result += std::popcount(cp::filter_unchecked<C::greater_equal>(e, first, 64, rhs));
    }
    for (; first + 16 <= count; first += 16) {
        const auto selected = cp::filter16_unchecked<C::greater_equal>(e, first, rhs);
        result += ends ? weighted16(ends, first, selected) : std::popcount(selected);
    }
    if (first < count) {
        auto selected = cp::filter_unchecked<C::greater_equal>(e, first, count - first, rhs);
        if (!ends) result += std::popcount(selected);
        else while (selected) {
            const auto rank = first + std::countr_zero(selected);
            result += unsigned(ends[rank]) + 1 - (rank ? unsigned(ends[rank - 1]) + 1 : 0);
            selected &= selected - 1;
        }
    }
    return result;
}
template <unsigned K, unsigned H>
u64 count_ge(planes p, u64 rhs, bool guard, const std::uint8_t *ends) {
    if (!p.count) return 0;
    auto source = [&] {
        if constexpr (H == 0) return sp::dense_source<format<K, H>>{p.payload, p.count};
        else return view<K, H>(p.count, p.payload, p.head0, p.head1);
    }();
    const auto e = cp::describe(source);
    if (guard) return count_expression(guarded<K, H>(e), p.count, rhs, ends);
    return count_expression(e, p.count, rhs, ends);
}
template <unsigned K, unsigned H> constexpr operations entry() {
    if constexpr (K >= H) return {pack<K, H>, dispatch<K, H>, count_ge<K, H>};
    else return {};
}
template <unsigned H, unsigned First, std::size_t... I> constexpr auto table(std::index_sequence<I...>) {
    return std::array<operations, 8>{{entry<First + I, H>()...}};
}
} // namespace detail
} // namespace frame_filter
