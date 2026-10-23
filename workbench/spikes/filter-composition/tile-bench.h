#pragma once
#include "tile-baseline.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>

namespace tiles {
namespace sp=ikea::seriespack;
using word=std::uint64_t;
using C=ikea::comparison;
inline unsigned iterations=20000,repeats=3;
inline bool reverse_order=false;
inline word observed=0;
template<unsigned K,unsigned N> struct Fixture {
    alignas(64) std::array<std::uint8_t,65536> storage{};
    std::array<std::array<word,N>,64> truth{};
    std::array<word,64> queries{};
    unsigned stride,offset;
    Fixture(unsigned shift, unsigned pitch=0):stride(pitch ? pitch : ((N*K/8+shift+63)/64)*64),offset(shift) {
        std::mt19937_64 rng(0x12e97+K*37+N);
        constexpr word limit=~word{0}>>(64-K);
        for(unsigned w=0;w<64;++w) {
            auto* p=storage.data()+w*stride+offset;
            for(unsigned i=0;i<N;++i) {
                const auto v=(i%7==0?0:i%7==1?limit:rng()&limit);
                truth[w][i]=v;
                auto* t=p+(i/8)*K;
                for(unsigned b=0;b<K/8;++b) t[(i%8)*(K/8)+b]=v>>(K%8+8*b);
                for(unsigned b=0;b<K%8;++b) t[8*(K/8)+b]|=((v>>b)&1)<<(i%8);
            }
            queries[w]=(w%2)?truth[w][w%N]:rng()&limit;
        }
    }
};
// Freeze the original complete16 lowering, including its runtime activity
// decoder. This remains the pre-change control as production evolves.
template<unsigned K,C Op>
[[gnu::always_inline]] unsigned old16(const std::uint8_t* p,word query,unsigned active) {
    if(!active) return 0;
    if constexpr(K<64) if(query>>K) return ikea::compare<Op>(0,query)?active:0;
    if constexpr(K<8 && sp::detail::use_packed_filter<Op,K>)
        return (sp::detail::filter_planes8<Op,K>(p,query) |
               (sp::detail::filter_planes8<Op,K>(p+K,query)<<8)) & active;
    else {
        const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),16};
        sp::author::native_ops ops;
        return sp::author::filter_values<Op>(sp::author::read(ops,sp::author::expression(source),0,active),query,active);
    }
}
template<unsigned K,unsigned N,C Op,unsigned Arm>
[[gnu::always_inline]] word call(const std::uint8_t* p,word query,word active) {
    active &= ~word{0}>>(64-N);
    if(!active) return 0;
    if constexpr(Arm==0) {
        const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),N};
        return sp::filter_unchecked<Op>(source,0,N,query,active);
    } else if constexpr(Arm==1) {
        word out=0;
        // Frozen pre-change algorithm: native16, then scalar points.
        for(unsigned i=0;i<N/16*16;i+=16)
            out|=word(old16<K,Op>(p+(i/8)*K,query,(active>>i)&65535))<<i;
        for(unsigned i=N/16*16;i<N;++i)
            if((active>>i)&1) out|=word(ikea::compare<Op>(sp::get_unchecked(
                sp::const_view<sp::layout<K>>::attach_unchecked(N, {{{std::as_bytes(std::span{p,N*K/8}),K},{},{}}}),i),query))<<i;
        return out;
    } else if constexpr(Arm==2) {
        return tile_control::window<K,N,int(Op)>(p,query)&active;
    } else if constexpr(Arm==3) {
        word out=0;
        const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),N};
        const auto expr=sp::author::expression(source);
        for(unsigned i=0;i<N;i+=8) out|=word(sp::author::filter8_unchecked<Op>(expr,i,query))<<i;
        return out&active;
    } else {
        // Original complete16 algorithm with the new bounded traversal/native8
        // tail. Isolates direct comparisons from the traversal correction.
        word out=0;
        for(unsigned i=0;i<N/16*16;i+=16)
            out|=word(old16<K,Op>(p+(i/8)*K,query,65535))<<i;
        if constexpr(N%16) {
            const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),N};
            out|=word(sp::author::filter8_unchecked<Op>(sp::author::expression(source),N/16*16,query))<<(N/16*16);
        }
        return out&active;

    }
}
template<unsigned K,unsigned N,C Op,unsigned Arm>
[[gnu::noinline]] void measure(const Fixture<K,N>& f,word active) {
    auto invoke=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        return call<K,N,Op,Arm>(f.storage.data()+w*f.stride+f.offset,f.queries[w],active);
    };
    auto next=[](word mask,unsigned salt) __attribute__((always_inline)) {
        return unsigned((mask*0x9e3779b97f4a7c15ULL)>>58)^(salt&63);
    };
    for(unsigned w=0;w<64;++w) {
        word expected=0;
        for(unsigned i=0;i<N;++i) if((active>>i)&1)
            expected|=word(int(Op)==0?f.truth[w][i]==f.queries[w]:int(Op)==1?f.truth[w][i]<f.queries[w]:int(Op)==2?f.truth[w][i]<=f.queries[w]:int(Op)==3?f.truth[w][i]>f.queries[w]:f.truth[w][i]>=f.queries[w])<<i;
        assert(invoke(w)==expected);
    }
    for(bool serial:{true,false}) for(unsigned rep=0;rep<repeats;++rep) {
        std::array<unsigned,8> cursor{0,7,13,21,32,41,54,63};
        for(unsigned i=0;i<256;++i) for(unsigned j=0;j<8;++j)
            cursor[j]=next(invoke(cursor[j]),i+j*0x9e37u);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<iterations;++i) {
            if(serial) cursor[0]=next(invoke(cursor[0]),i);
            else sp::detail::each<8>([&](auto j) {cursor[j]=next(invoke(cursor[j]),i+j*0x9e37u);});
        }
        const auto stop=std::chrono::steady_clock::now();
        for(auto c:cursor) observed+=c;
        const auto ns=std::chrono::duration<double,std::nano>(stop-start).count()/(double(iterations)*(serial?1:8));
        std::printf("%u,%u,%u,%u,%u,%llu,%u,%s,%u,%.6f\n",K,N,unsigned(Op),f.offset,f.stride,
            (unsigned long long)active,Arm,serial?"latency":"throughput",rep,ns);
    }
}
template<unsigned K,unsigned N,C Op> void fixture(unsigned offset,unsigned pitch=0) {
    constexpr word all=~word{0}>>(64-N);
    Fixture<K,N> f(offset,pitch);
    for(word active:{all,word(all&0xa53c0918800f7315ULL)}) {
        if(!reverse_order) {
            measure<K,N,Op,0>(f,active); measure<K,N,Op,1>(f,active);
            measure<K,N,Op,2>(f,active); measure<K,N,Op,3>(f,active); measure<K,N,Op,4>(f,active);
        } else {
            measure<K,N,Op,4>(f,active); measure<K,N,Op,3>(f,active); measure<K,N,Op,2>(f,active);
            measure<K,N,Op,1>(f,active); measure<K,N,Op,0>(f,active);
        }
    }
}
template<unsigned K,unsigned N,C Op> void run() {
    for(unsigned offset:{0u,1u,31u,63u}) fixture<K,N,Op>(offset);
}
template<unsigned K> void width() {
    run<K,8,C::equal>(); run<K,24,C::equal>();
    run<K,8,C::less>(); run<K,24,C::less>();
}
template<unsigned K> void sweep() {
    width<K>();
    fixture<K,8,C::less_equal>(1);
    fixture<K,8,C::greater>(1);
    fixture<K,8,C::greater_equal>(1);
    // Exact layout requested by Iceberg–Swiss, B96 alternates line offsets 0/32.
    if constexpr(K>=10) {
        constexpr unsigned B=K<=30?64:96;
        constexpr unsigned N=8*((B-1)/(K+1));
        fixture<K,N,C::equal>(0,B);
        fixture<K,N,C::less>(0,B);
    }
}
} // namespace tiles
