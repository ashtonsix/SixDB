#pragma once
#include "common.h"
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/native.h>
#include <ikea/seriespack/author/native.h>
#include <span>
namespace ht {
namespace tp=ikea::tuplepack;
namespace sp=ikea::seriespack;
// Cold admission, shared by physical field width rather than by table instance.
// First executable grammar is byte-aligned planes; bit-width packing follows
// only as an admitted, measured recipe, not an alternate public interface.
template<unsigned Bytes> struct field {
    static_assert(Bytes>0 && Bytes<=8);
    tp::reader<8> read;
    tp::writer<8> write;
    field():field([] {
        std::array<tp::code,Bytes> codes{};
        for(unsigned i=0;i<Bytes;++i) codes[i]={byte(i),0,8};
        auto l=tp::layout::make(Bytes,codes); require(bool(l),"field layout");
        return *l;
    }()) {}
    explicit field(const tp::layout& l):read([&] {
        std::array<byte,Bytes> order{}; for(unsigned i=0;i<Bytes;++i) order[i]=i;
        auto p=tp::reader<8>::make(l,order); require(bool(p),"field read");return *p;
    }()),write([&] {
        std::array<byte,Bytes> order{}; for(unsigned i=0;i<Bytes;++i) order[i]=i;
        auto p=tp::writer<8>::make(l,order); require(bool(p),"field write");return *p;
    }()) {
        require(read.controls().transfer.bytes==Bytes,"field transfer");
        require(write.controls().transfer.bytes==Bytes,"write transfer");
    }
};
template<unsigned Bytes> inline const field<Bytes> prepared_field{};
// A selected byte-plane recipe proves the entire transfer, not merely its
// length. These controls execute the production TuplePack bodies; the cold
// proof below checks all controls on the chosen branch against preparation.
// Keep this spike-local until non-identity projections qualify the API shape.
template<unsigned Bytes> struct identity {
    static bool accepts(const tp::detail::gpr_read& r,const tp::detail::gpr_write& w) {
        auto same=[](const auto& t) {
            return t.bytes==Bytes && t.offset==0 && t.shift==0 && t.mask==mask(Bytes*8);
        };
        if(!same(r.transfer) || r.slots!=Bytes || r.reads!=mask(Bytes) ||
           !same(w.transfer) || w.word.selected!=Bytes || w.read.slots!=Bytes ||
           w.writes!=mask(Bytes) || w.native_reads!=0 || w.runs!=1 ||
           w.footprint[0].offset!=0 || w.footprint[0].size!=Bytes) return false;
        if constexpr(Bytes==1) {
            const auto& b=w.stores[0];
            return b.offset==0 && b.mask==255 && b.input[0]==0 && b.shift[0]==0;
        }
        return true;
    }
    static constexpr auto read=[] {
        tp::detail::gpr_read p{}; p.slots=Bytes;
        p.transfer={mask(Bytes*8),0,0,Bytes}; return p;
    }();
    static constexpr auto write=[] {
        tp::detail::gpr_write p{};p.word.selected=Bytes;p.read.slots=Bytes;
        p.transfer={mask(Bytes*8),0,0,Bytes};
        p.stores[0].mask=255; // faithful one-byte path, selected before transfer
        return p;
    }();
    static void admit() {
        static const bool valid=[] {
            const auto& p=prepared_field<Bytes>;
            require(accepts(p.read.controls(),p.write.controls()),"identity projection admission");
            return true;
        }(); (void)valid;
    }
};
template<unsigned Bytes> void admit_identity() {if constexpr(Bytes) identity<Bytes>::admit();}
// Already encoded opaque bytes, disjoint exact windows. Explicit bulk copying
// for merge reservations; ordinary field kernels keep their existing lowering.
[[gnu::always_inline]] inline void copy_plain_bytes(byte* to,std::span<const byte> from) {
    std::size_t i=0;
#if defined(__AVX512BW__)
    for(;i+64<=from.size();i+=64)
        _mm512_storeu_si512(to+i,_mm512_loadu_si512(from.data()+i));
#elif defined(__AVX2__)
    for(;i+32<=from.size();i+=32)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(to+i),
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(from.data()+i)));
#endif
    for(;i+16<=from.size();i+=16) {
#if defined(__aarch64__)
        vst1q_u8(to+i,vld1q_u8(from.data()+i));
#else
        _mm_storeu_si128(reinterpret_cast<__m128i*>(to+i),
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(from.data()+i)));
#endif
    }
    if(i+8<=from.size()){__builtin_memcpy(to+i,from.data()+i,8);i+=8;}
    if(i+4<=from.size()){__builtin_memcpy(to+i,from.data()+i,4);i+=4;}
    if(i+2<=from.size()){__builtin_memcpy(to+i,from.data()+i,2);i+=2;}
    if(i<from.size())to[i]=from[i];
}
struct bag_value_window {std::span<const byte> bytes;std::uint32_t next=0;};
template<unsigned Bytes, bool Direct=false>
[[gnu::always_inline]] inline word get(const byte* p) {
    if constexpr(Bytes==0) return 0;
    else if constexpr(Direct) { word v=0; __builtin_memcpy(&v,p,Bytes); return v; }
    else return tp::native::read_transfer_body<1,Bytes>(identity<Bytes>::read,
                                                       [&](unsigned){return p;},1);
}
template<unsigned Bytes, bool Direct=false>
[[gnu::always_inline]] inline void put(byte* p,word v) {
    if constexpr(Bytes>0) {
        if constexpr(Direct) __builtin_memcpy(p,&v,Bytes);
        else tp::native::write_body(identity<Bytes>::write,p,v);
    }
}
// F8 is SeriesPack's byte-identity tile. Admit only its complete 8/16-byte
// physical window, including initialized inactive lanes; no neighbor overread.
template<unsigned Bytes, bool Direct=false>
[[gnu::always_inline]] inline word fingerprint_matches(const byte* p,byte f,unsigned active) {
    static_assert(Bytes==8 || Bytes==16);
#if defined(__aarch64__)
    auto raw=[&] {
        if constexpr(Direct && Bytes==8) return vcombine_u8(vld1_u8(p),vdup_n_u8(0));
        else if constexpr(Direct) return vld1q_u8(p);
        else return sp::native::load128<Bytes>(p);
    }();
    auto equal=vceqq_u8(raw,vdupq_n_u8(f));
    word bits=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0);
    return bits&mask(active*4);
#elif defined(__AVX2__)
    auto raw=[&] {
        if constexpr(Direct && Bytes==8) return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
        else if constexpr(Direct) return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        else return sp::native::load128<Bytes>(p);
    }();
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    return unsigned(_mm_cmpeq_epi8_mask(raw,_mm_set1_epi8(f)))&mask(active);
#else
    return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(raw,_mm_set1_epi8(f))))&mask(active);
#endif
#else
#error Explicit NEON or AVX2 required
#endif
}
inline constexpr unsigned match_step=
#if defined(__aarch64__)
4;
#else
1;
#endif
[[gnu::always_inline]] inline unsigned take_match(word& bits) {
    unsigned slot=std::countr_zero(bits)/match_step;
    bits&=~(mask(match_step)<<(slot*match_step)); return slot;
}
template<bool Direct=false>
[[gnu::always_inline]] inline void put_fingerprint(byte* p,unsigned slot,byte f) {
    if constexpr(Direct) p[slot]=f;
    else sp::detail::put_payload<sp::format<8>>(p+(slot/8)*8,slot%8,f);
}
} // namespace ht
