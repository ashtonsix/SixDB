// One-off comparison reuses the public test's independent wire fixture.
#include "fixtures/wire.h"
#include <benchmark/benchmark.h>
#include <string>

namespace boundary_probe {
constexpr std::uint64_t seed=0x349ed5a027b68fc1ULL;
constexpr std::size_t queries=512,batch=128;
std::uint64_t mix(std::uint64_t x) {
    x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;
    return x^(x>>31);
}
struct config { unsigned w,h;sp::geometry g;bool gaps; };
struct shape { const char* name;unsigned origin,count;bool tiles; };
void run(benchmark::State& state,config c,shape shape,sp::execution_target target) {
    const sp::description d{c.w+c.h,c.h,c.g};
    const auto n=std::max<std::size_t>(1024,(4096*8/d.width)&~std::size_t{255});
    fixture f(d,n,c.gaps);
    for(std::size_t i=0;i<n;++i) f.oracle_set(f.bytes,i,mix(seed+i)&mask(d.width));
    auto bound=sp::bind_reader(f.view().as_const(),target);
    if(!bound || bound->target()!=target) fail("boundary binding",d,n);
    const auto original=f.bytes;
    std::array<sp::index_range,queries> ranges;
    const auto grain=shape.tiles?f.values_per_tile:32;
    const auto count=shape.count?shape.count:f.values_per_tile+1;
    std::uint64_t hash=0;
    for(std::size_t i=0;i<queries;++i) {
        const auto begin=(mix(seed+i*0x9e3779b97f4a7c15ULL)%((n-shape.origin-count)/grain+1))*grain+shape.origin;
        ranges[i]={begin,begin+count};hash=mix(hash^begin^((begin+count)<<32));
    }
    alignas(64) std::array<std::uint64_t,264> storage;
    auto* output=storage.data()+1;
    for(auto rows:ranges) {
        storage.fill(0xdeadbeefcafebabeULL);
        bound->decode(rows,sp::output_values{std::span(output,count)});
        if(storage[0]!=0xdeadbeefcafebabeULL) fail("boundary prefix",d,n);
        for(std::size_t j=0;j<count;++j)
            if(output[j]!=(mix(seed+rows.begin+j)&mask(d.width))) fail("boundary oracle",d,n,j);
        for(std::size_t j=count+1;j<storage.size();++j)
            if(storage[j]!=0xdeadbeefcafebabeULL) fail("boundary suffix",d,n,j);
    }
    std::size_t cursor=0;std::uint64_t checksum=0;
    for(auto _:state) {
        for(std::size_t i=0;i<batch;++i) {
            const auto rows=ranges[cursor++];if(cursor==queries)cursor=0;
            bound->decode(rows,sp::output_values{std::span(output,rows.size())});
            checksum+=output[0]+output[rows.size()-1];
        }
        benchmark::DoNotOptimize(checksum);
    }
    f.compare(original,"boundary source changed");
    state.SetItemsProcessed(state.iterations()*batch);
    state.counters["queries_per_iteration"]=batch;
    state.counters["values_per_query"]=count;
    state.counters["query_count"]=queries;
    state.counters["query_grain"]=grain;
    state.counters["query_origin"]=shape.origin;
    state.counters["logical_values"]=n;
    state.counters["tile_values"]=f.values_per_tile;
    state.counters["encoded_bytes"]=n*d.width/8;
    state.counters["query_hash_lo"]=std::uint32_t(hash);
    state.counters["query_hash_hi"]=std::uint32_t(hash>>32);
    state.counters["checksum"]=static_cast<double>(checksum);
    state.counters["output_mod64"]=reinterpret_cast<std::uintptr_t>(output)%64;
    state.counters["output_mod4096"]=reinterpret_cast<std::uintptr_t>(output)%4096;
    for(unsigned p=0;p<3;++p) {
        state.counters["plane"+std::to_string(p)+"_stride"]=f.stride[p];
        state.counters["plane"+std::to_string(p)+"_extent"]=f.envelope[p];
        state.counters["plane"+std::to_string(p)+"_mod4096"]=reinterpret_cast<std::uintptr_t>(f.plane(p).data())%4096;
    }
}
}
namespace seriespack_measurement {
void register_boundary_probe() {
    using namespace boundary_probe;
    constexpr std::array configs{
        config{5,0,sp::geometry::striped,false},config{6,0,sp::geometry::striped,false},
        config{12,0,sp::geometry::striped,false},config{5,16,sp::geometry::striped,true},
        config{6,16,sp::geometry::striped,true},config{12,16,sp::geometry::striped,true},
        config{7,0,sp::geometry::local8,false}};
    constexpr std::array shapes{shape{"full",16,16,false},shape{"short",17,3,false},
        shape{"eight",16,8,false},shape{"crossing",17,16,false},
        shape{"complete-suffix",0,0,true},shape{"last-lane",31,2,false}};
    for(auto target:{sp::execution_target::avx2,sp::execution_target::avx512,sp::execution_target::neon}) {
        if(!sp::bind_reader(sp::const_view::assume_valid({1,0,sp::geometry::local8},0,{}),target))continue;
        for(auto c:configs)for(auto s:shapes) {
            auto name=std::string("boundary/series/")+(target==sp::execution_target::avx2?"avx2":target==sp::execution_target::avx512?"avx512":"neon")+
                (c.g==sp::geometry::striped?"/striped/k":"/local/k")+std::to_string(c.w+c.h)+"/h"+std::to_string(c.h)+"/u64/"+(c.gaps?"gapped/":"dense/")+s.name;
            benchmark::RegisterBenchmark(name.c_str(),[=](benchmark::State& state){run(state,c,s,target);});
        }
    }
}
}
