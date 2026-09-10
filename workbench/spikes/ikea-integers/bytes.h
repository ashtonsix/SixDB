#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ikea::integers {
#define IP_INLINE inline __attribute__((always_inline))
static_assert(std::endian::native == std::endian::little);
template<unsigned N> IP_INLINE uint64_t read_bytes(const uint8_t* p) {
    static_assert(N >= 1 && N <= 8);
    if constexpr(std::has_single_bit(N)) {
        using T = std::conditional_t<N==1, uint8_t, std::conditional_t<N==2,
            uint16_t, std::conditional_t<N==4, uint32_t, uint64_t>>>;
        T v; std::memcpy(&v,p,N); return v;
    } else {
        constexpr unsigned H = std::bit_floor(N);
        return read_bytes<H>(p) | (read_bytes<H>(p+N-H) << (8*(N-H)));
    }
}
template<unsigned N> IP_INLINE void write_bytes(uint8_t* p, uint64_t v) {
    static_assert(N >= 1 && N <= 8);
    if constexpr(std::has_single_bit(N)) std::memcpy(p,&v,N);
    else {
        constexpr unsigned H = std::bit_floor(N);
        write_bytes<H>(p,v); write_bytes<H>(p+N-H,v >> (8*(N-H)));
    }
}
IP_INLINE uint64_t transpose8(uint64_t x) {
    auto exchange = [&]<unsigned S, uint64_t Mask>() {
        const uint64_t t = (x ^ (x >> S)) & Mask;
        x ^= t ^ (t << S);
    };
    exchange.template operator()<7,0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14,0x0000cccc0000ccccULL>();
    exchange.template operator()<28,0x00000000f0f0f0f0ULL>();
    return x;
}
} // namespace ikea::integers
