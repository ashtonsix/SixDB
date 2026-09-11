#pragma once
#include <ikea/seriespack/detail/dense_read.h>
#include <ikea/seriespack/author/expression.h>

namespace ikea::seriespack {
namespace detail {
template <class V, class U> U placed_point(const void* source, std::size_t i) {
    return get_unchecked(*static_cast<const V*>(source), i);
}
template <class V, class U>
[[gnu::always_inline]] inline void placed_region(const V& source, std::size_t i,
                                                 U* __restrict out) {
    const auto expression = composition::describe(source);
#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    composition::wide_ops<16> ops;
    native_group::store(out, composition::read(ops, expression, i, std::uint64_t{0xffff}));
#elif defined(__aarch64__) || defined(__AVX2__)
    composition::native_ops ops;
    native::store16(out, composition::read(ops, expression, i, std::uint16_t{0xffff}));
#else
    each<16>([&](auto p) { out[p] = get_unchecked(source, i + p); });
#endif
}
template <class V, class U>
void placed_read16(const void* source, std::size_t i, U* __restrict out) {
    __builtin_assume(i % 16 == 0);
    placed_region(*static_cast<const V*>(source), i, out);
}
template <class V, class U>
void placed_range(const void* resource, std::size_t first, std::size_t count, U* __restrict out) {
    const auto& source = *static_cast<const V*>(resource);
    if (first % 16 == 0 && count == 16)
        return placed_region(source, first, out);
    while (count && first % 16) {
        *out++ = get_unchecked(source, first++);
        --count;
    }
#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    constexpr unsigned grain = V::format_type::width <= 8    ? 64
                               : V::format_type::width <= 16 ? 32
                                                             : 16;
    const auto expression = composition::describe(source);
    composition::wide_ops<grain> ops;
    constexpr std::uint64_t active = ~std::uint64_t{0} >> (64 - grain);
    while (count >= grain) {
        native_group::store(out, composition::read(ops, expression, first, active));
        first += grain;
        out += grain;
        count -= grain;
    }
#endif
    while (count >= 16) {
        placed_region(source, first, out);
        first += 16;
        out += 16;
        count -= 16;
    }
    while (count--) {
        *out++ = get_unchecked(source, first++);
    }
}
} // namespace detail
/// Borrows this named view as well as its bytes. Binding chooses dense execution
/// once where applicable; placed kernels retain the actual independent streams.
template <class U, class F, class B> decoder<U> bind_decoder(const view<F, B>& source) {
    static_assert(sizeof(U) * 8 >= F::width);
    if constexpr (F::heads == 0)
        if (auto dense_source = dense(source))
            return bind_decoder<U>(*dense_source);
    using V = view<F, B>;
    return {&source, source.size(), detail::placed_range<V, U>, detail::placed_read16<V, U>,
            detail::placed_point<V, U>};
}
template <class U, class F, class B> auto bind_decoder(const view<F, B>&&) = delete;

/// Checked convenience call. The output must be disjoint from occupied input
/// bytes, as for the bound operation. Owners retain lifetime/synchronization.
template <class F, class B, class U>
std::expected<void, error> read(const view<F, B>& source, std::size_t first, std::size_t count,
                                std::span<U> out) {
    if (first > source.size() || count > source.size() - first)
        return std::unexpected(error::range);
    if (out.size() < count)
        return std::unexpected(error::capacity);
    bind_decoder<U>(source).read_unchecked(first, count, out.data());
    return {};
}
} // namespace ikea::seriespack
