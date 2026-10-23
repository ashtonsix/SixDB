#pragma once
#include "baseline.h"
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>
#include <cassert>
namespace sp=ikea::seriespack;
namespace tp=ikea::tuplepack;
using C=ikea::comparison;
constexpr unsigned windows=64;
extern unsigned repeats,iterations;
extern std::uint64_t observed;
[[gnu::always_inline]] inline unsigned next_window(std::uint64_t mask, unsigned salt) {
    // Every result bit feeds the next address. Taking only mask&63 would let
    // the compiler discard comparisons for the higher original positions.
    return unsigned((mask * 0x9e3779b97f4a7c15ULL) >> 58) ^ (salt & 63);
}

// The serial result selects the next window and query. Independent throughput
// uses eight independent chains: results remain observable without one shared
// accumulator or query dependency inside the measured loop.
template<class Call> [[gnu::noinline]] void measure(const char* module,unsigned width,unsigned op,
                                                    const char* candidate,Call call,unsigned lanes=16) {
    for(bool serial:{true,false})for(unsigned rep=0;rep<repeats;++rep) {
        std::array<unsigned,8> cursor{0,7,13,21,32,41,54,63};
        const unsigned streams=serial?1:8;
        for(unsigned i=0;i<2048;++i)
            for(unsigned j=0;j<8;++j)cursor[j]=next_window(call(cursor[j]),i+j*0x9e37u);
        auto begin=std::chrono::steady_clock::now();
        for(unsigned i=0;i<iterations;++i) {
            if(serial)cursor[0]=next_window(call(cursor[0]),i);
            else sp::detail::each<8>([&](auto j){cursor[j]=next_window(call(cursor[j]),i+unsigned(j)*0x9e37u);});
        }
        auto end=std::chrono::steady_clock::now();
        for(auto c:cursor)observed+=c;
        const auto ns=std::chrono::duration<double,std::nano>(end-begin).count()/(iterations*streams);
        std::printf("%s,%u,%u,%s,%s,%u,%.6f,%.6f\n",module,width,op,candidate,
                    serial?"dependent":"independent8",rep,ns,ns/lanes);
    }
}
template<unsigned K,int Op> void series() {
    using F=sp::layout<K>;
    constexpr unsigned bytes=K*2;
    alignas(64) std::array<std::uint8_t,windows*bytes> storage{};
    std::array<std::uint64_t,windows> queries;
    sp::dense_source<F> source{std::as_bytes(std::span{storage}).data(),windows*16};
    std::mt19937_64 random(K*7+Op);
    constexpr auto limit=~std::uint64_t{0}>>(64-K);
    // Independent Local wire encoder: row bytes then low-bit planes per tile.
    std::array<std::array<std::uint64_t,16>,windows> values;
    for(unsigned w=0;w<windows;++w) {
        queries[w]=random()&limit;
        for(unsigned j=0;j<16;++j) {
            auto x=random()&limit;values[w][j]=x;
            auto* tile=storage.data()+std::size_t(w)*bytes+(j/8)*K;
            if constexpr(K<8)for(unsigned bit=0;bit<K;++bit)tile[bit]|=((x>>bit)&1)<<(j%8);
            else for(unsigned b=0;b<K/8;++b)tile[(j%8)*(K/8)+b]=x>>(8*b);
        }
    }
    for(unsigned w=0;w<windows;w+=2)queries[w]=values[w][w%16];
    auto ours=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        return sp::filter_unchecked<C(Op)>(source,std::size_t(w)*16,16,queries[w]);
    };
    auto fused=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        return control::run<K,Op>(storage.data()+std::size_t(w)*bytes,queries[w]);
    };
    auto packed=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        auto* p=storage.data()+std::size_t(w)*bytes;
        if constexpr(K<8)return sp::detail::filter_local8<C(Op),K>(p,queries[w])|
            (sp::detail::filter_local8<C(Op),K>(p+K,queries[w])<<8);
        else return 0u;
    };
    for(unsigned w=0;w<windows;++w) {
        unsigned expected=0;
        for(unsigned j=0;j<16;++j) {
            bool match=false;auto x=values[w][j],q=queries[w];
            if constexpr(Op==0)match=x==q;
            if constexpr(Op==1)match=x<q;
            if constexpr(Op==2)match=x<=q;
            if constexpr(Op==3)match=x>q;
            if constexpr(Op==4)match=x>=q;
            expected|=unsigned(match)<<j;
        }
        assert(ours(w)==expected&&fused(w)==expected);
        if constexpr(K<8)assert(packed(w)==expected);
    }
    measure("series",K,Op,"ikea",ours);
    measure("series",K,Op,"fused",fused);
    if constexpr(K<8)measure("series",K,Op,"packed",packed);
}
template<unsigned Shift,unsigned Width,int Op> void tuple() {
    std::array<tp::code,1> code{{{0,Shift,Width}}};
    auto layout=*tp::layout::make(1,code);
    alignas(64)std::array<std::uint8_t,windows*16> storage{};
    std::array<std::uint64_t,windows> queries;
    std::mt19937_64 random(Width+Op);
    for(auto& x:storage)x=random();
    for(auto& q:queries)q=random()&((1u<<Width)-1);
    auto view=*tp::view::attach(layout,std::as_writable_bytes(std::span{storage}),storage.size(),1);
    auto plan=*tp::read_plan<64,16>::make(layout,std::array<std::uint8_t,1>{0});
    auto read=*tp::bind_reader(plan,view);
    const auto& filter=read;
    auto ours=[&](unsigned w) __attribute__((always_inline)) {asm("" : "+r"(w));return filter.template filter_unchecked<C(Op)>(std::size_t(w)*16,queries[w]);};
    // Independently fused packed byte control: compare in-place bits, avoiding
    // reconstruction. Other bits cannot affect the unsigned comparison.
    auto fused=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        const auto* p=storage.data()+std::size_t(w)*16;
        if constexpr(Shift==0&&Width==8)return control::run<8,Op>(p,queries[w]);
        else {
            alignas(16)std::array<std::uint8_t,16> decoded;
#if defined(__aarch64__)
            auto x=vandq_u8(vshrq_n_u8(vld1q_u8(p),Shift),vdupq_n_u8((1u<<Width)-1));
            vst1q_u8(decoded.data(),x);
#else
            auto x=_mm_and_si128(_mm_srli_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)),Shift),_mm_set1_epi8((1u<<Width)-1));
            _mm_store_si128(reinterpret_cast<__m128i*>(decoded.data()),x);
#endif
            // The local register carrier folds with the inline control. Audit
            // the generated kernel to ensure no stack materialization survives.
            return control::run<8,Op>(decoded.data(),queries[w]);
        }
    };
    for(unsigned w=0;w<windows;++w)assert(ours(w)==fused(w));
    measure("tuple",Width,Op,"ikea",ours);
    measure("tuple",Width,Op,"fused",fused);
}

#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
template<unsigned K,int Op> void wide_series() {
    using F=sp::layout<K>;
    alignas(64)std::array<std::uint8_t,windows*K*8> storage{};
    std::array<std::uint64_t,windows> queries;
    std::mt19937_64 random(K*17+Op);
    constexpr auto limit=~std::uint64_t{0}>>(64-K);
    for(auto& b:storage)b=random();
    for(auto& q:queries)q=random()&limit;
    sp::dense_source<F> source{std::as_bytes(std::span{storage}).data(),windows*64};
    auto ours=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        return sp::filter_unchecked<C(Op)>(source,std::size_t(w)*64,64,queries[w]);
    };
    auto fused=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        std::uint64_t out=0;auto* p=storage.data()+std::size_t(w)*K*8;auto q=queries[w];
#if defined(__AVX512BW__) && defined(__AVX512VL__)
        constexpr int imm=Op==0?_MM_CMPINT_EQ:Op==1?_MM_CMPINT_LT:Op==2?_MM_CMPINT_LE:Op==3?_MM_CMPINT_NLE:_MM_CMPINT_NLT;
        sp::detail::each<K/8>([&](auto j){
            auto x=_mm512_loadu_si512(p+j*64);
            std::uint64_t bits;
            if constexpr(K==8)bits=_mm512_cmp_epu8_mask(x,_mm512_set1_epi8(q),imm);
            if constexpr(K==16)bits=_mm512_cmp_epu16_mask(x,_mm512_set1_epi16(q),imm);
            if constexpr(K==32)bits=_mm512_cmp_epu32_mask(x,_mm512_set1_epi32(q),imm);
            if constexpr(K==64)bits=_mm512_cmp_epu64_mask(x,_mm512_set1_epi64(q),imm);
            out|=bits<<(j*(512/K));
        });
#else
        sp::detail::each<4>([&](auto j){out|=std::uint64_t(control::run<K,Op>(p+j*K*2,q))<<(j*16);});
#endif
        return out;
    };
    for(unsigned w=0;w<windows;++w)assert(ours(w)==fused(w));
    measure("series64",K,Op,"ikea",ours,64);
    measure("series64",K,Op,"fused",fused,64);
}
#endif
