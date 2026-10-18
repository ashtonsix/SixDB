#include <arm_neon.h>
#ifdef __ARM_FEATURE_SVE2_BITPERM
#include <arm_sve.h>
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>

// Prepared query: byte b is 0xff iff query bit b is set. Requires eight
// readable bytes for R>4, four for R=4. Bytes beyond R can be arbitrary.
template<unsigned R>
static inline std::uint8_t match(const std::uint8_t* p, std::uint64_t q) {
    static_assert(R>=4 && R<=8);
    std::uint64_t x;
    if constexpr(R==4) {
        std::uint32_t raw; __builtin_memcpy(&raw,p,4); x=raw;
    } else {
        __builtin_memcpy(&x,p,8);
    }
    x=~(x^q);
    // Keep Clang from splitting EON and from narrowing the first shifted
    // 64-bit AND to a 32-bit AND plus a separate 64-bit shift.
    asm("" : "+r"(x));
    if constexpr(R>4) {
        x &= x >> (8*(R-4));
        asm("" : "+r"(x));
    }
    auto y=static_cast<std::uint32_t>(x);
    y &= y >> 16;
    // y has only sixteen possibly set bits. The last AND therefore has
    // only eight, and may directly set flags without a separate byte mask.
    return y & (y >> 8);
}

extern "C" [[gnu::noinline]] bool contains6_arm_prepared(const std::uint8_t* p, std::uint64_t q) {
    return match<6>(p,q)!=0;
}
extern "C" [[gnu::noinline]] bool contains4_arm_prepared(const std::uint8_t* p, std::uint64_t q) {
    return match<4>(p,q)!=0;
}
extern "C" [[gnu::noinline]] unsigned match6_arm_prepared(const std::uint8_t* p, std::uint64_t q) {
    return match<6>(p,q);
}
static inline std::uint64_t prepare(unsigned q) {
    const auto weights=vreinterpret_u8_u64(vcreate_u64(0x8040201008040201ULL));
    auto flags=vtst_u8(vdup_n_u8(q),weights);
    asm("" : "+w"(flags));
    return vget_lane_u64(vreinterpret_u64_u8(flags),0);
}
extern "C" [[gnu::noinline]] bool contains6_arm_runtime(const std::uint8_t* p, unsigned q) {
    if(q>=64) return false;
    return match<6>(p,prepare(q))!=0;
}

#ifdef __ARM_FEATURE_SVE2_BITPERM
// One 8x8 transpose is three stable bit partitions. This is a separate
// instruction-count control, not a selected replacement for scalar reduction.
extern "C" [[gnu::noinline]] bool contains6_sve_prepared(
    const std::uint8_t* p, svuint64_t alternating, svuint8_t query, svbool_t active) {
    auto x=svld1rq_u64(svwhilelt_b64(std::uint64_t(0),std::uint64_t(1)),reinterpret_cast<const std::uint64_t*>(p));
    x=svbgrp_u64(x,alternating);
    x=svbgrp_u64(x,alternating);
    x=svbgrp_u64(x,alternating);
    const auto rows=svand_n_u8_x(active,svreinterpret_u8_u64(x),63);
    return svptest_any(active,svcmpeq_u8(active,rows,query));
}
#endif

// Independent scalar decoding, including random unrelated bytes after R.
template<unsigned R> static void check(std::mt19937_64& rng) {
    for(unsigned trial=0;trial<2048;++trial) {
        alignas(8) std::uint8_t storage[16];
        for(auto& v:storage) v=rng();
        const auto* p=storage+1; // Deliberately unaligned.
        for(unsigned q=0;q<(1u<<R);++q) {
            unsigned expected=0;
            for(unsigned row=0;row<8;++row) {
                unsigned v=0;
                for(unsigned bit=0;bit<R;++bit) v|=((p[bit]>>row)&1u)<<bit;
                expected|=unsigned(v==q)<<row;
            }
            std::uint64_t prepared=0;
            for(unsigned b=0;b<8;++b) if((q>>b)&1) prepared|=0xffULL<<(8*b);
            assert(prepare(q)==prepared);
            assert(match<R>(p,prepared)==expected);
            if constexpr(R==4) assert(contains4_arm_prepared(p,prepared)==bool(expected));
            if constexpr(R==6) {
                assert(contains6_arm_prepared(p,prepared)==bool(expected));
                assert(match6_arm_prepared(p,prepared)==expected);
                assert(contains6_arm_runtime(p,q)==bool(expected));
#ifdef __ARM_FEATURE_SVE2_BITPERM
                assert(contains6_sve_prepared(p,svdup_u64(0x5555555555555555ULL),
                       svdup_u8(q),svwhilelt_b8(std::uint64_t(0),std::uint64_t(8)))==bool(expected));
#endif
            }
        }
    }
}

int main() {
    std::mt19937_64 rng(0xa190fe);
    check<4>(rng); check<5>(rng); check<6>(rng); check<7>(rng); check<8>(rng);
    std::uint8_t zero[8]{};
    assert(!contains6_arm_runtime(zero,64));
    std::puts("ARM Local contains: R4..8, every in-domain query, 2048 randomized unaligned tiles per width, arbitrary extra bytes: passed");
#ifdef __ARM_FEATURE_SVE2_BITPERM
    std::puts("SVE2 BitPerm three-BGRP transpose + contains R6: passed");
#endif
}
