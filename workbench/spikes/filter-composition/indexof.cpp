#include "tile-bench.h"
#if defined(FILTER_ARM_REDUCTION)
#include "arm-local.h"
#endif

// First-match consumer controls alongside the graduated public API. Paths return
// the lowest active matching rank, or N. No-match does not dereference a rank.
namespace index_probe {
using namespace tiles;
#if defined(FILTER_ARM_REDUCTION)
// Preserve the original NEON equality algorithm as the production helper
// evolves. Standard-word paths are unchanged. Queries retain full domain checks.
template<unsigned R> [[gnu::always_inline]] unsigned previous_planes(const std::uint8_t* p,word q) {
    unsigned bits=255;
    sp::detail::each<R>([&](auto b) { bits &= p[b] ^ ~(0u-unsigned((q>>b)&1)); });
    return bits&255;
}
template<unsigned K,unsigned N> [[gnu::always_inline]] word previous_mask(const std::uint8_t* p,word q,word active) {
    if constexpr(K%8) {
        if(q>>K) return 0;
        word result=0;
        sp::detail::each<N/8>([&](auto tile) {
            const auto* at=p+tile*K;
            unsigned mask=previous_planes<K%8>(at+8*(K/8),q);
            if constexpr(K>8) mask &= sp::detail::filter_body8<C::equal,K/8>(at,q>>(K%8));
            result |= word(mask)<<(8*tile);
        });
        return result&active;
    } else return call<K,N,C::equal,0>(p,q,active);
}
#endif
template<unsigned K,unsigned N,unsigned Group,unsigned First=0>
[[gnu::always_inline]] unsigned first(const std::uint8_t* p,word query,word active) {
    constexpr unsigned Count=std::min(Group,N-First);
    const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),N};
    const auto m=sp::filter_unchecked<C::equal>(source,First,Count,query,active>>First);
    if constexpr(First+Count==N) {
        // The final group is branchless, including misses: preserve the
        // comparison -> rank -> next-address dependency in the consumer.
        return First+std::countr_zero(m|(word{1}<<Count));
    } else {
        if(m) return First+std::countr_zero(m);
        return first<K,N,Group,First+Count>(p,query,active);
    }
}
template<unsigned K,unsigned N,unsigned Arm>
[[gnu::always_inline]] unsigned lookup(const std::uint8_t* p,word query,word active) {
#if defined(FILTER_ARM_REDUCTION)
    if constexpr(Arm==0) return std::countr_zero(previous_mask<K,N>(p,query,active)|(N<64 ? word{1}<<N : 0));
    else if constexpr(Arm==11) {
        const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),N};
        return sp::indexof_unchecked(source,0,N,query,active);
    } else if constexpr(Arm==12) {
        return std::countr_zero(call<K,N,C::equal,0>(p,query,active)|(N<64 ? word{1}<<N : 0));
    } else if constexpr(Arm>=7 && K%8>=4) {
        constexpr unsigned R=K%8;
        const auto prepared=arm_local_probe::prepare(query);
        word result=0;
        for(unsigned base=0;base<N;base+=8) {
            unsigned body=255;
            if constexpr(K>8) {
                const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),N};
                const auto expr=sp::author::expression(source);
                body=sp::author::filter8_unchecked<C::equal>(expr.payload.body,base,query>>R);
            }
            body &= active>>base;
            const auto* tail=p+(base/8)*K+8*(K/8);
            if constexpr(Arm==7 || Arm==8) {
                if(body) {
                    const auto exact=body & arm_local_probe::match<R,Arm==8>(tail,prepared);
                    if(exact) return base+std::countr_zero(exact);
                }
            } else {
                result|=word(body & arm_local_probe::match<R,Arm==10>(tail,prepared))<<base;
            }
        }
        if constexpr(Arm==7 || Arm==8) return N;
        else return std::countr_zero(result|(N<64 ? word{1}<<N : 0));
    } else
#endif
    if constexpr((Arm>=4) && K>8 && K%8) {
        // Use the authored body projection to reject most rows, then compare
        // residuals only at candidates. Original rank order is preserved.
        const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),N};
        const auto expr=sp::author::expression(source);
        sp::author::scalar_ops scalar;
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
        const auto basis=_mm_set1_epi64x(0x8040201008040201ULL);
        const auto reversed=_mm_gf2p8affine_epi64_epi8(_mm_set1_epi8(query),basis,0);
        const auto significant=_mm_set1_epi8(static_cast<char>(255u<<(8-K%8)));
#endif
        for(unsigned base=0;base<N;base+=8) {
            auto matches=unsigned(sp::author::filter8_unchecked<C::equal>(
                expr.payload.body,base,query>>(K%8),active>>base));
            if constexpr(Arm==4) {
                while(matches) {
                    const auto rank=base+std::countr_zero(matches);
                    if(sp::author::read(scalar,expr.payload.tail,rank,true).value==(query&((word{1}<<(K%8))-1)))
                        return rank;
                    matches&=matches-1;
                }
            } else if(matches) {
                // Bitplane residuals are already rank masks: checking the
                // candidate group's planes can be cheaper than extracting a
                // residual word at each candidate rank.
                const auto exact=[&] {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
                    if constexpr(Arm==6) {
                        // OWNER-ONLY control: the fixture supplies eight readable
                        // bytes at every residual, including beyond the final tile.
                        // Generic SeriesPack leaf bounds do not grant this.
                        word raw; __builtin_memcpy(&raw,p+(base/8)*K+8*(K/8),8);
                        const auto values=_mm_gf2p8affine_epi64_epi8(basis,_mm_set1_epi64x(raw),0);
                        return unsigned(_mm_testn_epi8_mask(_mm_xor_si128(values,reversed),significant))&matches;
                    } else
#endif
                    // Freeze this baseline's scalar-plane tail when the new
                    // default is selected; body comparison is unchanged.
#if defined(FILTER_ARM_REDUCTION)
                    if constexpr(Arm==5 || Arm==6)
                        return previous_planes<K%8>(p+(base/8)*K+8*(K/8),query)&matches;
#endif
                    return unsigned(sp::author::filter8_unchecked<C::equal>(
                        expr.payload.tail,base,query&((word{1}<<(K%8))-1),matches));
                }();
                if(exact) return base+std::countr_zero(unsigned(exact));
            }
        }
        return N;
    } else if constexpr(Arm>=4) return first<K,N,8>(p,query,active);
    else if constexpr(Arm==1) return first<K,N,8>(p,query,active);
    else if constexpr(Arm==2) return first<K,N,16>(p,query,active);
    else {
        const auto m=call<K,N,C::equal,Arm==0?0:2>(p,query,active);
        return std::countr_zero(m|(N<64 ? word{1}<<N : 0));
    }
}
template<unsigned K,unsigned N> struct Data {
    Fixture<K,N> f{1};
    Data(unsigned pattern) {
        constexpr word limit=~word{0}>>(64-K);
        f.storage.fill(0);
        std::mt19937_64 rng(0x429e + K*37+N);
        for(unsigned w=0;w<64;++w) {
            const word start=rng()&limit;
            for(unsigned i=0;i<N;++i) {
                // This second panel distinguishes random bodies from shared
                // prefixes. The first capture used only the clustered control.
                word v;
                do {v=pattern>=12?((start&~((word{1}<<(K%8))-1))+i)&limit:
                      pattern>=6?rng()&limit:(start+i*17)&limit;}
                while(std::find(f.truth[w].begin(),f.truth[w].begin()+i,v)!=f.truth[w].begin()+i);
                f.truth[w][i]=v;
            }
            const auto shape=pattern>=12?(pattern==12?3u:1u):pattern%6;
            unsigned pos=shape==0?rng()%8:shape==1?N-8+rng()%8:rng()%N;
            if(shape==3 || (shape==4 && w%2)) pos=N;
            if(pos<N) f.queries[w]=f.truth[w][pos];
            else {
                word q=pattern>=12?((start&~((word{1}<<(K%8))-1))+N)&limit:(start+N*17)&limit;
                while(std::find(f.truth[w].begin(),f.truth[w].end(),q)!=f.truth[w].end()) q=(q+1)&limit;
                f.queries[w]=q;
            }
            if(shape==5) {
                pos=rng()%8;
                f.queries[w]=f.truth[w][pos];
                f.truth[w][N-1]=f.truth[w][pos];
            }
            auto* p=f.storage.data()+w*f.stride+1;
            for(unsigned i=0;i<N;++i) {
                const auto v=f.truth[w][i]; auto* t=p+(i/8)*K;
                for(unsigned b=0;b<K/8;++b) t[(i%8)*(K/8)+b]=v>>(K%8+8*b);
                for(unsigned b=0;b<K%8;++b) t[8*(K/8)+b]|=((v>>b)&1)<<(i%8);
            }
        }
    }
};
template<unsigned K,unsigned N,unsigned Arm>
[[gnu::noinline]] void measure(const Data<K,N>& data,unsigned pattern,word active) {
    const auto& f=data.f;
    auto invoke=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        return lookup<K,N,Arm>(f.storage.data()+w*f.stride+1,f.queries[w],active);
    };
    for(unsigned w=0;w<64;++w) {
        unsigned expected=N;
        for(unsigned i=0;i<N;++i) if(((active>>i)&1) && f.truth[w][i]==f.queries[w]) {expected=i;break;}
        assert(invoke(w)==expected);
    }
    auto next=[](unsigned rank,unsigned salt) __attribute__((always_inline)) {
        return unsigned((word(rank)*0x9e3779b97f4a7c15ULL)>>58)^(salt&63);
    };
    for(bool serial:{true,false}) for(unsigned rep=0;rep<repeats;++rep) {
        std::array<unsigned,8> cursor{0,7,13,21,32,41,54,63};
        for(unsigned i=0;i<256;++i) for(unsigned j=0;j<8;++j) cursor[j]=next(invoke(cursor[j]),i+j*0x9e37u);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<iterations;++i) {
            if(serial) cursor[0]=next(invoke(cursor[0]),i);
            else sp::detail::each<8>([&](auto j) {cursor[j]=next(invoke(cursor[j]),i+j*0x9e37u);});
        }
        const auto stop=std::chrono::steady_clock::now();
        for(auto c:cursor) observed+=c;
        const auto ns=std::chrono::duration<double,std::nano>(stop-start).count()/(double(iterations)*(serial?1:8));
        std::printf("%u,%u,%u,%llu,%u,%s,%u,%.6f\n",K,N,pattern,(unsigned long long)active,Arm,
            serial?"serial":"throughput",rep,ns);
    }
}
template<unsigned K,unsigned N> void run() {
    constexpr word all=~word{0}>>(64-N);
    for(unsigned pattern=0;pattern<(K%8?14:12);++pattern) {
        Data<K,N> data(pattern);
        for(word active:{all,word(all&0xa53c0918800f7315ULL)}) {
#if defined(FILTER_ARM_CONFIRM)
            if(reverse_order) {
                measure<K,N,12>(data,pattern,active); measure<K,N,11>(data,pattern,active);
                measure<K,N,7>(data,pattern,active); measure<K,N,3>(data,pattern,active);
                measure<K,N,0>(data,pattern,active);
            } else {
                measure<K,N,0>(data,pattern,active); measure<K,N,3>(data,pattern,active);
                measure<K,N,7>(data,pattern,active); measure<K,N,11>(data,pattern,active);
                measure<K,N,12>(data,pattern,active);
            }
#else
            if(reverse_order) {
#if defined(FILTER_ARM_REDUCTION)
                measure<K,N,11>(data,pattern,active);
                measure<K,N,10>(data,pattern,active); measure<K,N,9>(data,pattern,active);
                measure<K,N,8>(data,pattern,active); measure<K,N,7>(data,pattern,active);
#endif
                measure<K,N,6>(data,pattern,active);
                measure<K,N,5>(data,pattern,active);
                measure<K,N,4>(data,pattern,active);
                measure<K,N,3>(data,pattern,active); measure<K,N,2>(data,pattern,active);
                measure<K,N,1>(data,pattern,active); measure<K,N,0>(data,pattern,active);
            } else {
                measure<K,N,0>(data,pattern,active); measure<K,N,1>(data,pattern,active);
                measure<K,N,2>(data,pattern,active); measure<K,N,3>(data,pattern,active);
                measure<K,N,4>(data,pattern,active);
                measure<K,N,5>(data,pattern,active);
                measure<K,N,6>(data,pattern,active);
#if defined(FILTER_ARM_REDUCTION)
                measure<K,N,7>(data,pattern,active); measure<K,N,8>(data,pattern,active);
                measure<K,N,9>(data,pattern,active); measure<K,N,10>(data,pattern,active);
                measure<K,N,11>(data,pattern,active);
#endif
            }
#endif
        }
    }
}
}
#ifndef FILTER_INDEX_NO_MAIN
int main(int argc,char** argv) {
    if(argc>1) tiles::iterations=std::stoul(argv[1]);
    if(argc>2) tiles::repeats=std::stoul(argv[2]);
    if(argc>3) tiles::reverse_order=std::stoul(argv[3]);
    std::puts("width,count,pattern,active,arm,metric,rep,ns");
    index_probe::run<10,40>(); index_probe::run<12,32>(); index_probe::run<16,24>();
    index_probe::run<30,16>(); index_probe::run<40,16>(); index_probe::run<64,8>();
#if defined(FILTER_ARM_REDUCTION)
    index_probe::run<4,8>(); index_probe::run<5,8>(); index_probe::run<6,8>(); index_probe::run<7,8>();
    index_probe::run<13,32>(); index_probe::run<14,32>(); index_probe::run<15,32>();
    index_probe::run<20,24>(); index_probe::run<29,16>(); index_probe::run<31,16>();
    index_probe::run<38,16>(); index_probe::run<54,8>(); index_probe::run<62,8>();
#endif
    std::fprintf(stderr,"indexof truth checks passed; observation=%llu\n",(unsigned long long)tiles::observed);
}
#endif
