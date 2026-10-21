// Complete geometry consumers: query -> interval/window -> dependent payload.
// Before uses the equivalent consumer-owned composition of the old endpoints;
// fused uses independent SIMD search, never Boundlist's routing implementation.
#include <ikea/boundlist.h>
#include "fused.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <vector>
namespace bl=ikea::boundlist;
using word=std::uint64_t;
word mix(word x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
struct request {unsigned rank, link; word token;};
struct answer {unsigned rank; word token;};
template<class T> struct fixture {
    unsigned n;
    T origin;
    std::vector<T> cuts;
    std::vector<word> payload;
    std::array<request,1024> requests;
    fixture(unsigned count):n(count),origin(sizeof(T)==8?word(1)<<48:0),cuts(n+1),payload(n+1){
        cuts[0]=origin;
        for(unsigned i=0;i<n;++i){cuts[i+1]=cuts[i]+3+mix(i+713)%7;payload[i]=mix(i+91);}
        payload[n]=mix(n+91);
        std::array<unsigned,1024> order;std::iota(order.begin(),order.end(),0);
        for(unsigned i=1023;i;--i)std::swap(order[i],order[mix(i+713)%(i+1)]);
        for(unsigned i=0;i<1024;++i){const auto at=order[i],next=order[(i+1)%1024],r=unsigned(mix(at+57)%n);
            requests[at]={r,next-unsigned(payload[r])-r,word(cuts[r])^word(cuts[r+1])};}
    }
};
template<unsigned Method,unsigned Op,class T> [[gnu::always_inline]] answer find(const fixture<T>&f,unsigned r){
    const T q=T(f.cuts[r]+1);
    unsigned rank; word token=0;
    if constexpr(Method==0){
#ifdef BOUNDLIST_API_CONTROL
        const bl::direct_source<T> ends{f.cuts.data()+1,f.n};
        if constexpr(Op==0)rank=q<f.origin || q>=ends.at(f.n-1) ? f.n : bl::upper_bound(ends,q);
        else if constexpr(Op==1){
            const T high=T(q+1),extent=ends.at(f.n-1);
            if(q>=high || q>=extent || high<=f.origin) {rank=f.n;token=f.n+word(f.n)*257;}
            else {
                const auto first=bl::upper_bound(ends,std::max(q,f.origin));
                const auto stop=bl::lower_bound(ends,std::min(high,extent));
                const auto limit=stop==f.n?f.n:stop+1;
                rank=first;token=first+word(limit)*257;
            }
        }
        else {rank=r;const auto begin=r?ends.at(r-1):f.origin;const auto end=ends.at(r);
            token=word(begin)^word(end);}
#else
        const auto geometry=bl::from_cuts(std::span(f.cuts));
        if constexpr(Op==0)rank=bl::reverse(geometry,q);
        else if constexpr(Op==1){const auto window=bl::overlap(geometry,q,T(q+1));rank=window.first;token=window.first+word(window.limit)*257;}
        else {const auto bounds=bl::locate(geometry,r);assert(bounds);
            rank=r;token=word(bounds->first)^word(bounds->limit);}
#endif
    }else if constexpr(Method==1){
        if constexpr(Op==0)rank=boundlist_fused::flat<false>(f.cuts.data()+1,f.n,q);
        else if constexpr(Op==1){const auto first=boundlist_fused::flat<false>(f.cuts.data()+1,f.n,q);
            const auto limit=boundlist_fused::flat<true>(f.cuts.data()+1,f.n,T(q+1))+1;
            rank=first;token=first+word(limit)*257;}
        else {rank=r;token=word(f.cuts[r])^word(f.cuts[r+1]);}
    }else {rank=r;if constexpr(Op==2)token=word(f.cuts[r])^word(f.cuts[r+1]);
        if constexpr(Op==1)token=r+word(r+1)*257;}
    return {rank,token};
}
template<unsigned C,unsigned M,unsigned Op,class T> [[gnu::noinline]] word consume(const fixture<T>&f,unsigned ops){
    std::array<unsigned,C> state;for(unsigned c=0;c<C;++c)state[c]=c*127;
    for(unsigned i=0;i<ops/C;++i)[&]<std::size_t...I>(std::index_sequence<I...>) __attribute__((always_inline)) {
        auto one=[&](auto c) __attribute__((always_inline)){const auto q=f.requests[state[c]];
            const auto result=find<M,Op>(f,q.rank);
            const auto adjustment=Op==2 ? unsigned(q.token) : Op==1 ? q.rank+(q.rank+1)*257 : 0u;
            state[c]=(result.rank+unsigned(result.token)+unsigned(f.payload[result.rank])+q.link-adjustment)&1023;};
        (one(std::integral_constant<unsigned,I>{}),...);
    }(std::make_index_sequence<C>{});
    word sum=0;for(auto x:state)sum+=x;return sum;
}
volatile word observed;
template<unsigned C,unsigned Op,class T>void panel(const fixture<T>&f,unsigned ops,unsigned reps){
    const auto expected=consume<C,2,Op>(f,ops);
    for(unsigned r=0;r<f.n;++r){assert((find<0,Op>(f,r).rank==r));assert((find<1,Op>(f,r).rank==r));}
    for(unsigned rep=0;rep<reps;++rep)for(unsigned order=0;order<2;++order){
        auto run=[&]<unsigned M>(){observed=consume<C,M,Op>(f,4096);
            const auto start=std::chrono::steady_clock::now();const auto result=consume<C,M,Op>(f,ops);
            const auto stop=std::chrono::steady_clock::now();observed=result;assert(result==expected);
            std::printf("%zu,%u,%u,%s,%u,%u,%.6f,%zu,%zu,%llu\n",sizeof(T)*8,f.n,Op,M?"independent_fused":"geometry",C,rep,
                double(std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count())/(ops/C*C),
                f.cuts.size()*sizeof(T),f.payload.size()*sizeof(word),(unsigned long long)result);};
        if((rep+order)%2)run.template operator()<1>();else run.template operator()<0>();
    }
}
int main(int argc,char**argv){const unsigned ops=argc>1?std::strtoul(argv[1],nullptr,10):262144,reps=argc>2?std::strtoul(argv[2],nullptr,10):5;
    std::puts("bits,count,operation,method,concurrency,rep,ns_request,geometry_bytes,payload_bytes,checksum");
    auto all=[&]<class T>(){for(unsigned n:{16u,256u,4096u}){fixture<T>f(n);
        panel<1,0>(f,ops,reps);panel<8,0>(f,ops,reps);panel<1,1>(f,ops,reps);panel<8,1>(f,ops,reps);
        panel<1,2>(f,ops,reps);panel<8,2>(f,ops,reps);}};
    all.template operator()<std::uint16_t>();all.template operator()<std::uint64_t>();
}
