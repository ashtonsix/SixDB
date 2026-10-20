#pragma once
#include <ikea/boundlist/search.h>
#include <array>
#include <cmath>

namespace prediction_spike {
namespace p=ikea::boundlist;
struct interval { unsigned first,limit; };
template<class T> struct model {
    T first,last;
    double scale;
    bool useful;
    std::uint8_t adaptive_log=5;
    bool adaptive_useful=false;
    static constexpr unsigned fixed_window=32;
    static model prepare(const T* data,unsigned n) {
        if(!n) return {0,0,0,false};
        model result{data[0],data[n-1],0,false};
        if(n<=fixed_window || result.first==result.last) return result;
        result.scale=double(n-1)/double(T(result.last-result.first));
        // This is an applicability heuristic, never a correctness assumption.
        // Every predicted window is checked before local search. Unseen skew
        // still takes a bounded ordinary fallback.
        result.useful=true;
        unsigned maximum_error=0;
        for(unsigned part=1;part<8;++part) {
            const auto rank=unsigned(std::uint64_t(n-1)*part/8);
            const auto expected=unsigned(double(T(data[rank]-result.first))*result.scale);
            const auto error=expected>rank?expected-rank:rank-expected;
            maximum_error=std::max(maximum_error,error);
            if(error>fixed_window/2) result.useful=false;
        }
        const auto width=std::max<std::uint64_t>(4,std::bit_ceil(2ull*maximum_error+4));
        result.adaptive_log=std::countr_zero(width);
        result.adaptive_useful=width*8<=n;
        return result;
    }
    template<bool Guard,bool Adaptive=false> [[gnu::always_inline]] interval bracket(const T* data,unsigned n,T query) const {
        if constexpr(Guard) if(Adaptive?!adaptive_useful:!useful) return {0,n};
        const unsigned window=Adaptive?(1u<<adaptive_log):fixed_window;
        if(n<=window) return {0,n};
        if(!(first<query)) return {0,0};
        if(last<query) return {n,n};
        if(first==last) return {0,n};
        const auto estimate=std::min(n-1,unsigned(double(T(query-first))*scale));
        const auto base=std::min(estimate>window/2?estimate-window/2:0,n-window);
        const auto limit=base+window;
        if(!(data[base]<query)) return {0,base};
        if(data[limit-1]<query) return {limit,n};
        return {base+1,limit-1};
    }
};
template<class T> [[gnu::always_inline]] unsigned finish(const T* data,unsigned n,T query,interval range) {
    auto lo=range.first,remaining=range.limit-lo;
    while(remaining>p::detail::convergence_lanes<T>) {
        const auto half=remaining/2,mid=lo+half;
        lo=p::detail::select_position<p::edge::at_or_after>(p::detail::read_element(data,mid),query,mid,lo);
        remaining-=half;
    }
    return p::detail::transition_window<p::edge::at_or_after>(data,n,lo,lo+remaining,query);
}
template<bool Guard,bool Adaptive=false,class T> [[gnu::always_inline]] unsigned search(const T* data,unsigned n,T query,const model<T>& predictor) {
    return finish(data,n,query,predictor.template bracket<Guard,Adaptive>(data,n,query));
}
template<bool Guard,bool Adaptive=false,class T,std::size_t N>
[[gnu::always_inline]] std::array<unsigned,N> batch(const T* data,unsigned n,const std::array<T,N>& queries,const model<T>& predictor) {
    std::array<unsigned,N> lo{},remaining{},result{};
#pragma clang loop unroll(full)
    for(unsigned i=0;i<N;++i) {
        const auto window=predictor.template bracket<Guard,Adaptive>(data,n,queries[i]);
        lo[i]=window.first;remaining[i]=window.limit-window.first;
    }
    bool pending;
    do {
        pending=false;
#pragma clang loop unroll(full)
        for(unsigned i=0;i<N;++i) if(remaining[i]>p::detail::convergence_lanes<T>) {
            pending=true;const auto half=remaining[i]/2,mid=lo[i]+half;
            lo[i]=p::detail::select_position<p::edge::at_or_after>(p::detail::read_element(data,mid),queries[i],mid,lo[i]);
            remaining[i]-=half;
        }
    } while(pending);
#pragma clang loop unroll(full)
    for(unsigned i=0;i<N;++i) result[i]=p::detail::transition_window<p::edge::at_or_after>(data,n,lo[i],lo[i]+remaining[i],queries[i]);
    return result;
}
} // namespace prediction_spike
