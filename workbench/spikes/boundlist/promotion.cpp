// Reuse the independently checked complete query/child trace and fixture. The
// production sources below replace candidate routing; controls stay independent.
#define main boundlist_search_experiment_main
#include "bench.cpp"
#undef main
#include "legacy-reverse.h"
#include <ikea/boundlist/author/search.h>

namespace production = ikea::boundlist;
template <bool Width,class W,class O,bool Point,bool Legacy,bool Padded=false> struct promoted {
    using offset_type=O;
    static constexpr bool is_width=Width;
    const fixture& f;
    [[gnu::always_inline]] bl::rank_window operator()(unsigned node,unsigned low,unsigned high) const {
        const auto v=f.stream(node);
        const sp::author::access<Width?sp::geometry::width:sp::geometry::direct,W,O,const sp::byte> a(v);
        const auto extent=f.typed_extent<Width,O>(node);
        auto owner=[&](unsigned p) __attribute__((always_inline)) {
            if constexpr(Legacy) return boundlist_legacy::reverse(a,p).rank;
            else if constexpr(Padded && Width) {
                const auto n=(a.size()+15)/16;
                __builtin_assume(n<=16);
                const auto mask=production::detail::native_mask<production::edge::after>(
                    reinterpret_cast<const O*>(a.geometry_data()),16,O(p))&unsigned(production::detail::low_bits(n));
                const auto next=mask?unsigned(std::countr_zero(mask)):n;
                const auto group=next-1;
                const auto base=ikea::boundlist::detail::load<O>(a.geometry_data()+group*sizeof(O));
                return std::uint16_t(group*16+ikea::boundlist::detail::owner16_native(a.widths(group),p-base));
            } else return a.reverse(p).rank;
        };
        if constexpr(Point) {
            const auto r=!low?0u:low>extent?f.count:owner(low-1);
            return r==f.count?bl::rank_window{r,r}:bl::rank_window{r,r+1};
        } else {
            if(low>=high||low>=extent) return {f.count,f.count};
            return {owner(low),hi_last(owner,high,extent)};
        }
    }
    template<class Find> [[gnu::always_inline]] unsigned hi_last(Find& owner,unsigned hi,unsigned extent) const {
        return hi-1<extent?owner(hi-1)+1:f.count;
    }
};
template<bool Width,class W,class O,bool Point> struct bound_source {
    using offset_type=O;
    static constexpr bool is_width=Width;
    const fixture& f;
    [[gnu::always_inline]] bl::rank_window operator()(unsigned node,unsigned low,unsigned high) const {
        const auto v=f.stream(node);
        const sp::author::access<Width?sp::geometry::width:sp::geometry::direct,W,O,const sp::byte> a(v);
        if constexpr(Point) {
            const auto r=a.template boundary<production::edge::at_or_after>(low);
            return r==f.count?bl::rank_window{r,r}:bl::rank_window{r,r+1};
        } else {
            const auto r=production::sidecar::author::overlap(a,low,high);
            return {r.first,r.limit};
        }
    }
};
template<bool Width,class W,class O,bool Point> struct scan_control {
    using offset_type=O;
    static constexpr bool is_width=Width;
    const fixture& f;
    [[gnu::always_inline]] bl::rank_window operator()(unsigned node,unsigned low,unsigned) const {
        const auto v=f.direct<O>(node);
        const production::author::scan_source<O> s(v.ends,v.count);
        const auto r=production::lower_bound(s,low);
        return r==f.count?bl::rank_window{r,r}:bl::rank_window{r,r+1};
    }
};
// Same complete point consumer, with probes interleaved across eight queries.
template<class W,class O> struct batch_control:bound_source<false,W,O,true> {
    template<std::size_t N> [[gnu::always_inline]] auto batch(const std::array<unsigned,N>& nodes,
        const std::array<unsigned,N>& points,const std::array<unsigned,N>&) const {
        std::array<production::direct_source<O>,N> sources;
#pragma clang loop unroll(full)
        for(unsigned i=0;i<N;++i) {
            const auto v=this->f.template direct<O>(nodes[i]); sources[i]={v.ends,v.count};
        }
        const auto ranks=production::author::lower_bounds(sources,points);
        std::array<bl::rank_window,N> result;
#pragma clang loop unroll(full)
        for(unsigned i=0;i<N;++i)result[i]={ranks[i],ranks[i]==this->f.count?ranks[i]:ranks[i]+1};
        return result;
    }
};
// Point marker is inherited only by point consumers, preserving the trace's
// distinction between a point query and a range with a small span.
template<class Body> struct point_body:Body { static constexpr auto point_edge=bl::edge::at_or_after; };
template <bool Width,class W,class O,bool Point>
void qualify(fixture& f,unsigned ops,unsigned reps) {
    auto oracle=[&](unsigned n,unsigned low,unsigned high) {
        if constexpr(Point) return point_oracle{f}(n,low,high);
        else return f.oracle(n,low,high);
    };
    const auto expected1=[&] { if constexpr(Point) return consume<1>(f,ops,point_oracle{f}); else return consume<1>(f,ops,oracle); }();
    const auto expected8=[&] { if constexpr(Point) return consume<8>(f,std::max(1u,ops/8),point_oracle{f}); else return consume<8>(f,std::max(1u,ops/8),oracle); }();
    auto run=[&](const char* name,auto body,unsigned rep) {
        if constexpr(Point) {
            const point_body<decltype(body)> p{body};
            measure<1>(f,name,ops,rep,expected1,p);measure<8>(f,name,ops,rep,expected8,p);
        } else {measure<1>(f,name,ops,rep,expected1,body);measure<8>(f,name,ops,rep,expected8,body);}
    };
    for(unsigned rep=0;rep<reps;++rep) {
        run("before_reverse",promoted<Width,W,O,Point,true>{f},rep);
        run("production_reverse",promoted<Width,W,O,Point,false>{f},rep);
        if constexpr(Width)run("candidate_padded_reverse",promoted<Width,W,O,Point,false,true>{f},rep);
        run("production_boundlist",bound_source<Width,W,O,Point>{f},rep);
        if constexpr(Point) {
            run("independent_fused",unique_point<Width,W,O,bl::method::binary,1>{f},rep);
            if constexpr(!Width) {
                run("production_scan",scan_control<Width,W,O,Point>{f},rep);
                run("production_batch8",batch_control<W,O>{{f}},rep);
            }
        } else run("independent_fused",fused<Width,W,O>{f},rep);
    }
}
int main(int argc,char**argv) {
    const unsigned nodes=argc>1?std::stoul(argv[1]):1;
    const unsigned maximum=argc>2?std::stoul(argv[2]):255;
    const bool point=argc>3?bool(std::stoul(argv[3])):false;
    const unsigned ops=argc>4?std::stoul(argv[4]):131072;
    const unsigned reps=argc>5?std::stoul(argv[5]):5;
    if(!std::has_single_bit(nodes)||(maximum!=255&&maximum!=4095))return 2;
    std::puts("layout,maximum,count,nodes,range_span,implementation,metric,rep,ns,checksum,metadata_bytes,child_bytes,shared_payload_bytes");
    for(bool width:{false,true}) {
        fixture f(nodes,256,maximum,width,257);
        if(maximum==255) {
            if(width) {if(point) qualify<true,std::uint8_t,std::uint16_t,true>(f,ops,reps);else qualify<true,std::uint8_t,std::uint16_t,false>(f,ops,reps);}
            else {if(point) qualify<false,std::uint8_t,std::uint16_t,true>(f,ops,reps);else qualify<false,std::uint8_t,std::uint16_t,false>(f,ops,reps);}
        } else {
            if(width) {if(point) qualify<true,std::uint16_t,std::uint32_t,true>(f,ops,reps);else qualify<true,std::uint16_t,std::uint32_t,false>(f,ops,reps);}
            else {if(point) qualify<false,std::uint16_t,std::uint32_t,true>(f,ops,reps);else qualify<false,std::uint16_t,std::uint32_t,false>(f,ops,reps);}
        }
    }
}
