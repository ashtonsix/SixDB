#include "support.h"

// Exercise lane order independently of the encoders. Exhaust every predicate
// pattern for <=16 lanes; wider packets also cross every SIMD sublane boundary.
template <unsigned Bytes, unsigned VectorBytes, C Op> void mask_patterns() {
    constexpr unsigned lanes = VectorBytes / Bytes;
    constexpr auto limit = ~std::uint64_t{0} >> (64 - Bytes * 8);
    constexpr auto live = ~std::uint64_t{0} >> (64 - lanes);
    std::mt19937_64 random(Bytes * 100 + VectorBytes);
    alignas(64) std::array<std::uint8_t, VectorBytes> data{};
    const unsigned patterns = lanes <= 16 ? (1u << lanes) : 4096;
    for (unsigned p = 0; p < patterns + lanes; ++p) {
        const auto pattern = p >= patterns ? std::uint64_t{1} << (p - patterns)
                            : lanes <= 16 ? p : random() & live;
        for (auto q : {std::uint64_t{0}, std::uint64_t{1}, limit / 2 + 1, limit}) {
            std::uint64_t wanted = 0;
            for (unsigned lane = 0; lane < lanes; ++lane) {
                const auto x = (pattern >> lane & 1) ? limit : 0;
                for (unsigned b = 0; b < Bytes; ++b)
                    data[lane * Bytes + b] = x >> (b * 8);
                wanted |= std::uint64_t(oracle(Op, x, q)) << lane;
            }
#if defined(__aarch64__)
            const auto got = ikea::detail::filter::bits<Bytes, Op>(vld1q_u8(data.data()), q);
#elif defined(__AVX2__)
            const auto got = [&] {
                if constexpr (VectorBytes == 16)
                    return ikea::detail::filter::bits<Bytes, Op>(
                        _mm_loadu_si128(reinterpret_cast<const __m128i*>(data.data())), q);
                else if constexpr (VectorBytes == 32)
                    return ikea::detail::filter::bits<Bytes, Op>(
                        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data.data())), q);
#if defined(__AVX512BW__) && defined(__AVX512VL__)
                else
                    return ikea::detail::filter::bits<Bytes, Op>(_mm512_loadu_si512(data.data()), q);
#endif
            }();
#else
            const auto got = wanted;
#endif
            assert(got == wanted);
        }
    }
}

void check_filter_masks() {
#if defined(__aarch64__) || defined(__AVX2__)
    [&]<C... Ops>(operations<Ops...>) {
        [&]<unsigned... B>(std::integer_sequence<unsigned, B...>) {
            auto widths = [&]<C Op>() {
                (mask_patterns<B, 16, Op>(), ...);
#if defined(__AVX2__)
                (mask_patterns<B, 32, Op>(), ...);
#if defined(__AVX512BW__) && defined(__AVX512VL__)
                (mask_patterns<B, 64, Op>(), ...);
#endif
#endif
            };
            (widths.template operator()<Ops>(), ...);
        }(std::integer_sequence<unsigned, 1, 2, 4, 8>{});
    }(operations<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
#endif
}
