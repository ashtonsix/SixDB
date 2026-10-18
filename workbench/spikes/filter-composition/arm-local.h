#pragma once
#include <arm_neon.h>
#include <cstdint>

namespace arm_local_probe {
[[gnu::always_inline]] inline std::uint64_t prepare(unsigned query) {
    const auto weights=vreinterpret_u8_u64(vcreate_u64(0x8040201008040201ULL));
    auto flags=vtst_u8(vdup_n_u8(query),weights);
    asm("" : "+w"(flags));
    return vget_lane_u64(vreinterpret_u64_u8(flags),0);
}
template<unsigned R> [[gnu::always_inline]] unsigned reduce(std::uint64_t x,std::uint64_t q) {
    static_assert(R>=4 && R<=7);
    x=~(x^q);
    asm("" : "+r"(x));
    if constexpr(R>4) {
        x &= x >> (8*(R-4));
        asm("" : "+r"(x));
    }
    auto y=std::uint32_t(x); y &= y>>16;
    return y & (y>>8);
}
template<unsigned R,bool Owner> [[gnu::always_inline]] unsigned match(const std::uint8_t* p,std::uint64_t q) {
    std::uint64_t raw=0;
    if constexpr(Owner) __builtin_memcpy(&raw,p,8);
    else if constexpr(R==4) { std::uint32_t x; __builtin_memcpy(&x,p,4); raw=x; }
    else {
        std::uint32_t lo,hi;
        __builtin_memcpy(&lo,p,4); __builtin_memcpy(&hi,p+R-4,4);
        raw=lo|(std::uint64_t(hi)<<(8*(R-4)));
    }
    return reduce<R>(raw,q);
}
}
