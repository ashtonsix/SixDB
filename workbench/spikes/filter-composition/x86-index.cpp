#define FILTER_INDEX_NO_MAIN
#include "indexof.cpp"

namespace x86_index {
using namespace tiles;
// Independently authored packed-body compare. Exact eight-row loads, no Ikea
// read/filter/mask helper. The layout is the same public wire representation.
template<unsigned Q> [[gnu::always_inline]] unsigned body(const std::uint8_t* p, word q) {
    if constexpr(Q==1 || Q==2 || Q==4 || Q==8) return tile_control::words<Q,0>(p,q);
    else {
        constexpr unsigned L=Q<4?4:8;
        unsigned mask=0;
        for(unsigned part=0;part<L/4;++part) {
            auto packet=[&]<unsigned Half>() {
                const unsigned start=(part*2+Half)*(16/L)*Q;
                const unsigned back=start+16>8*Q?start+16-8*Q:0;
                alignas(16) std::uint8_t indices[16];
                for(unsigned j=0;j<16;++j) indices[j]=j%L<Q?back+(j/L)*Q+j%L:255;
                return _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(p+start-back)),
                                        _mm_load_si128((const __m128i*)indices));
            };
            const auto x=_mm256_inserti128_si256(_mm256_castsi128_si256(packet.template operator()<0>()),packet.template operator()<1>(),1);
#if defined(__AVX512BW__) && defined(__AVX512VL__)
            if constexpr(L==4) mask|=unsigned(_mm256_cmpeq_epi32_mask(x,_mm256_set1_epi32(q)))<<(part*8);
            else mask|=unsigned(_mm256_cmpeq_epi64_mask(x,_mm256_set1_epi64x(q)))<<(part*4);
#else
            if constexpr(L==4) mask|=unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(x,_mm256_set1_epi32(q)))))<<(part*8);
            else mask|=unsigned(_mm256_movemask_pd(_mm256_castsi256_pd(_mm256_cmpeq_epi64(x,_mm256_set1_epi64x(q)))))<<(part*4);
#endif
        }
        return mask;
    }
}
template<unsigned R> [[gnu::always_inline]] word exact_load(const std::uint8_t* p) {
    // Separate from Ikea's load helper; exact bytes, zero upper bits.
    if constexpr(R==1) return *p;
    else if constexpr(R==2) {std::uint16_t x; __builtin_memcpy(&x,p,2);return x;}
    else if constexpr(R==3) {std::uint16_t x; __builtin_memcpy(&x,p,2);return x|(word(p[2])<<16);}
    else if constexpr(R==4) {std::uint32_t x; __builtin_memcpy(&x,p,4);return x;}
    else {std::uint32_t lo,hi;__builtin_memcpy(&lo,p,4);__builtin_memcpy(&hi,p+R-4,4);return lo|(word(hi)<<(8*(R-4)));}
}
template<unsigned R> [[gnu::always_inline]] unsigned fold(word raw,word q) {
    word x=~(raw^q);
    if constexpr(R==1) return x&255;
    if constexpr(R==2) return (x&(x>>8))&255;
    if constexpr(R==3) return (x&(x>>8)&(x>>16))&255;
    if constexpr(R>=4) {
        if constexpr(R>4) x&=x>>(8*(R-4));
        auto y=std::uint32_t(x); y&=y>>16;return (y&(y>>8))&255;
    }
}
template<unsigned K,unsigned N,unsigned Arm>
[[gnu::always_inline]] unsigned lookup(const std::uint8_t* p,word q,word active) {
    const sp::dense_source<sp::layout<K>> source{reinterpret_cast<const std::byte*>(p),N};
    if constexpr(Arm==0) return sp::indexof_unchecked(source,0,N,q,active);
    else if constexpr(Arm==10) return std::min(unsigned(std::countr_zero(sp::filter_unchecked<C::equal>(source,0,N,q,active))),N);
    else if constexpr(Arm==9) return std::countr_zero(tiles::call<K,N,C::equal,0>(p,q,active)|(N<64 ? word{1}<<N : 0));
    else if constexpr(Arm==1) return index_probe::first<K,N,16>(p,q,active);
    else if constexpr(Arm==2) return std::countr_zero((tile_control::window<K,N,0>(p,q)&active)|(N<64 ? word{1}<<N : 0));
    else if constexpr(Arm==3) return index_probe::lookup<K,N,4>(p,q,active);
    else if constexpr(K%8) {
        if(q>>K || !active) return N;
        constexpr unsigned R=K%8;
        const sp::author::local_equal8<R> prepared(q&((word{1}<<R)-1));
        // Independent GFNI query preparation or AVX2/BMI2 bit expansion.
#if defined(__GFNI__)
        const auto basis=_mm_set1_epi64x(0x8040201008040201ULL);
        const auto reversed=_mm_gf2p8affine_epi64_epi8(_mm_set1_epi8(q),basis,0);
        const auto significant=_mm_set1_epi8(static_cast<char>(255u<<(8-R)));
#else
        const auto expanded=_pdep_u64(q,0x0101010101010101ULL)*255;
#endif
        word result=0;
        for(unsigned base=0;base<N;base+=8) {
            unsigned candidates=255;
            if constexpr(K>8) {
                if constexpr(Arm>=7 && Arm!=11) candidates=body<K/8>(p+(base/8)*K,q>>R);
                else candidates=sp::author::filter8_unchecked<C::equal>(sp::author::expression(source).payload.body,base,q>>R);
            }
            candidates&=active>>base;
            if constexpr(Arm!=6 && Arm!=8 && Arm!=11 && Arm!=12) if(!candidates) continue;
            const auto* tail=p+(base/8)*K+8*(K/8);
            const auto raw=[&] {if constexpr(Arm==5 || Arm==11 || Arm==12) {word v;__builtin_memcpy(&v,tail,8);return v;}
                else return exact_load<R>(tail);}();
            const auto exact=[&] {
                if constexpr(Arm>=7 && Arm!=11) {
#if defined(__GFNI__)
                    const auto v=_mm_gf2p8affine_epi64_epi8(basis,_mm_set1_epi64x(raw),0);
                    return unsigned(_mm_mask_testn_epi8_mask(candidates,_mm_xor_si128(v,reversed),significant))&255;
#else
                    return fold<R>(raw,expanded);
#endif
                } else return unsigned(prepared(raw));
            }()&candidates;
            if constexpr(Arm==6 || Arm==8 || Arm==11 || Arm==12) result|=word(exact)<<base;
            else if(exact) return base+std::countr_zero(exact);
        }
        if constexpr(Arm==6 || Arm==8 || Arm==11 || Arm==12) return std::countr_zero(result|(N<64 ? word{1}<<N : 0));
        else return N;
    } else return index_probe::first<K,N,8>(p,q,active);
}
template<unsigned K,unsigned N,unsigned Arm>
[[gnu::noinline]] void measure(const index_probe::Data<K,N>& data,unsigned pattern,word active) {
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
        index_probe::Data<K,N> data(pattern);
        for(word active:{all,word(all&0xa53c0918800f7315ULL)}) {
#if defined(FILTER_X86_CONFIRM)
            if(reverse_order) {
                x86_index::measure<K,N,10>(data,pattern,active);
                x86_index::measure<K,N,8>(data,pattern,active);
                x86_index::measure<K,N,7>(data,pattern,active);
                x86_index::measure<K,N,2>(data,pattern,active);
                x86_index::measure<K,N,0>(data,pattern,active);
            } else {
                x86_index::measure<K,N,0>(data,pattern,active);
                x86_index::measure<K,N,2>(data,pattern,active);
                x86_index::measure<K,N,7>(data,pattern,active);
                x86_index::measure<K,N,8>(data,pattern,active);
                x86_index::measure<K,N,10>(data,pattern,active);
            }
#else
            if(reverse_order) {
                x86_index::measure<K,N,8>(data,pattern,active);
                x86_index::measure<K,N,7>(data,pattern,active);
                x86_index::measure<K,N,6>(data,pattern,active);
                x86_index::measure<K,N,5>(data,pattern,active);
                x86_index::measure<K,N,4>(data,pattern,active);
                x86_index::measure<K,N,3>(data,pattern,active);
                x86_index::measure<K,N,2>(data,pattern,active);
                x86_index::measure<K,N,1>(data,pattern,active);
                x86_index::measure<K,N,0>(data,pattern,active);
            } else {
                x86_index::measure<K,N,0>(data,pattern,active);
                x86_index::measure<K,N,1>(data,pattern,active);
                x86_index::measure<K,N,2>(data,pattern,active);
                x86_index::measure<K,N,3>(data,pattern,active);
                x86_index::measure<K,N,4>(data,pattern,active);
                x86_index::measure<K,N,5>(data,pattern,active);
                x86_index::measure<K,N,6>(data,pattern,active);
                x86_index::measure<K,N,7>(data,pattern,active);
                x86_index::measure<K,N,8>(data,pattern,active);
            }
#endif
        }
    }
}
}
#ifndef FILTER_X86_NO_MAIN
int main(int argc,char** argv) {
    if(argc>1) tiles::iterations=std::stoul(argv[1]);
    if(argc>2) tiles::repeats=std::stoul(argv[2]);
    if(argc>3) tiles::reverse_order=std::stoul(argv[3]);
    std::puts("width,count,pattern,active,arm,metric,rep,ns");
    x86_index::run<4,8>();
    x86_index::run<5,8>();
    x86_index::run<6,8>();
    x86_index::run<7,8>();
    x86_index::run<10,40>();
    x86_index::run<12,32>();
    x86_index::run<13,32>();
    x86_index::run<14,32>();
    x86_index::run<15,32>();
    x86_index::run<16,24>();
    x86_index::run<20,24>();
    x86_index::run<29,16>();
    x86_index::run<30,16>();
    x86_index::run<31,16>();
    x86_index::run<38,16>();
    x86_index::run<40,16>();
    x86_index::run<54,8>();
    x86_index::run<62,8>();
    x86_index::run<64,8>();
#if defined(FILTER_X86_CONFIRM)
    x86_index::run<30,64>(); x86_index::run<12,64>();
#endif
    std::fprintf(stderr,"x86 indexof truth checks passed; observation=%llu\n",(unsigned long long)tiles::observed);
}
#endif
