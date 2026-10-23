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
    const auto expression = author::expression(source);
#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    author::wide_ops<16> ops;
    native_group::store(out, author::read(ops, expression, i, std::uint64_t{0xffff}));
#elif defined(__aarch64__) || defined(__AVX2__)
    author::native_ops ops;
    native::store16(out, author::read(ops, expression, i, std::uint16_t{0xffff}));
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
    constexpr unsigned grain = V::layout_type::width <= 8    ? 64
                               : V::layout_type::width <= 16 ? 32
                                                             : 16;
    const auto expression = author::expression(source);
    author::wide_ops<grain> ops;
    constexpr std::uint64_t active = ~std::uint64_t{0} >> (64 - grain);
    while (count >= grain) {
        native_group::store(out, author::read(ops, expression, first, active));
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
namespace detail {
/// Borrows this named view as well as its bytes. Binding chooses dense execution
/// once where applicable; placed kernels retain the actual independent streams.
template <class U, class F, class B>
erased_reader<U> erase_placed_reader(const basic_view<F, B>& source, std::size_t count) {
    static_assert(sizeof(U) * 8 >= F::width);
    if constexpr (F::heads == 0)
        if (auto dense_source = dense(source)) {
            dense_source->count = count;
            return erase_dense_reader<U>(*dense_source);
        }
    using V = basic_view<F, B>;
    return {&source, count, detail::placed_range<V, U>, detail::placed_read16<V, U>,
            detail::placed_point<V, U>};
}
template <class U, class F, class B>
erased_reader<U> erase_placed_reader(const basic_view<F, B>& source) {
    return erase_placed_reader<U>(source, source.size());
}
} // namespace detail
} // namespace ikea::seriespack
