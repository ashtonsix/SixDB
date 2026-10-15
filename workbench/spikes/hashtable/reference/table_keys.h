#pragma once
#include "table_value.h"

namespace hashtable_spike::tiered {
// A cold-admitted identity projection, shared by all suffix byte planes.
// The native load is the actual TuplePack author endpoint for this physical
// unit. The complete consumer combines equality masks without decoding rows.
template<unsigned Bytes> struct identity_plane {
    static_assert(Bytes==8||Bytes==16);
    tp::reader<64> read;
    identity_plane():read([] {
        std::array<tp::code,Bytes> codes;
        std::array<byte,Bytes> map;
        for(unsigned i=0;i<Bytes;++i){codes[i]={byte(i),0,8};map[i]=byte(i);}
        auto description=tp::layout::make(Bytes,codes);require(bool(description));
        auto prepared=tp::reader<64>::make(*description,map);require(bool(prepared));return *prepared;
    }()) {
        require(read.unit_bytes()==Bytes&&read.read_bytes()==mask(Bytes));
        require(read.ordering().map_size()==Bytes);
        for(unsigned i=0;i<Bytes;++i)require(read.ordering().template offset<1>(0,i)==i);
    }
    template<bool Direct>[[gnu::always_inline]] auto get(const byte* p)const {
#if defined(__aarch64__)
        if constexpr(Direct) {
            if constexpr(Bytes==8)return vcombine_u8(vld1_u8(p),vdup_n_u8(0));
            else return vld1q_u8(p);
        } else return tp::native::load_unit(p,Bytes).a;
#else
        if constexpr(Direct) {
            if constexpr(Bytes==8)return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
            else return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        } else {
            const auto packet=tp::native::load_unit(p,Bytes);
#if defined(__AVX512VBMI__)
            return _mm512_castsi512_si128(packet);
#else
            return _mm256_castsi256_si128(packet.a);
#endif
        }
#endif
    }
};
struct no_planes {};

// A scoped authored projection, not a SeriesPack Local<8> view: Local's stride
// separates eight-row tiles and cannot describe one byte in each four-byte word.
// Point transfers use SP's byte leaf. The owner admits sixteen initialized
// four-byte words; the fingerprint owns exactly byte 3 of each word.
struct word_tags {
    static byte get(const byte* p,unsigned i) {
        return byte(sp::detail::point_payload<sp::format<8>>(p+4*i+3,0));
    }
    static void put(byte* p,unsigned i,byte value) {
        sp::detail::put_payload<sp::format<8>>(p+4*i+3,0,value);
    }
};
struct word_tile {
    tp::reader<64> suffixes;
    word_tile():suffixes([] {
        std::array<tp::code,48> codes;
        std::array<byte,48> map;
        for(unsigned i=0;i<48;++i){codes[i]={byte(i/3*4+i%3),0,8};map[i]=byte(i);}
        auto layout=tp::layout::make(64,codes);require(bool(layout));
        auto read=tp::reader<64>::make(*layout,map);require(bool(read));return *read;
    }()) {
        require(suffixes.unit_bytes()==64);
        require((suffixes.read_bytes()&0x7777777777777777ULL)==0x7777777777777777ULL);
        require(suffixes.ordering().map_size()==48);
    }
    // Parent coalesces the disjoint SP/TP projections into one physical load.
    // Their assignment paths still issue only their own bytes. Every byte of
    // the wider read window is initialized, and its full 64 bytes are charged.
    template<bool Direct>[[gnu::always_inline]] auto load(const byte* p)const {
        if constexpr(!Direct)return tp::native::load_unit(p,64);
        else {
#if defined(__aarch64__)
            return tp::native::packet{vld1q_u8(p),vld1q_u8(p+16),vld1q_u8(p+32),vld1q_u8(p+48)};
#elif defined(__AVX512VBMI__)
            return _mm512_loadu_si512(p);
#else
            return tp::native::packet{_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)),
                                     _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p+32))};
#endif
        }
    }
    template<bool Direct>[[gnu::always_inline]] int find(const byte* p,std::uint32_t key)const {
        const auto words=load<Direct>(p);
#if defined(__aarch64__)
        const auto wanted=vdupq_n_u32(key);
        const auto a=vmovn_u32(vceqq_u32(vreinterpretq_u32_u8(words.a),wanted));
        const auto b=vmovn_u32(vceqq_u32(vreinterpretq_u32_u8(words.b),wanted));
        const auto c=vmovn_u32(vceqq_u32(vreinterpretq_u32_u8(words.c),wanted));
        const auto d=vmovn_u32(vceqq_u32(vreinterpretq_u32_u8(words.d),wanted));
        const auto equal=vcombine_u8(vmovn_u16(vcombine_u16(a,b)),vmovn_u16(vcombine_u16(c,d)));
        const word mask=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0)&0x1111111111111111ULL;
        return mask?int(std::countr_zero(mask)>>2):-1;
#elif defined(__AVX512VBMI__)
        const unsigned mask=_mm512_cmpeq_epi32_mask(words,_mm512_set1_epi32(int(key)));
        return mask?int(std::countr_zero(mask)):-1;
#else
        const auto wanted=_mm256_set1_epi32(int(key));
        const unsigned a=unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(words.a,wanted))));
        const unsigned b=unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(words.b,wanted))));
        const unsigned mask=a|(b<<8);return mask?int(std::countr_zero(mask)):-1;
#endif
    }
};
} // namespace hashtable_spike::tiered
