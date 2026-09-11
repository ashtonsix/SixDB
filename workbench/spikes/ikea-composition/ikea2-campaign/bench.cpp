#include "fixture.h"

enum class reader_kind { prior,current,range,region };
template<unsigned K,sp::geometry G,reader_kind Kind,bool Ends=false> void access(benchmark::State& state) {
    comparison_fixture<K,G> f(std::max<std::size_t>(256,(4096*8/K/256)*256));
    auto next=f.template next<std::uint64_t>();auto current=f.current();
    auto prior=[] {
        if constexpr(K==56)return seriespack_measurement::predecessor56().get16;
        else return seriespack_measurement::predecessor(K,G==sp::geometry::striped).get16;
    }();
    alignas(64) std::uint64_t output[16];
    auto call=[&](std::size_t q) {
        q&=~std::size_t{15};
        if constexpr(Kind==reader_kind::prior)prior(f.storage.get(),q,output);
        if constexpr(Kind==reader_kind::current)current.decode({q,q+16},old::output_values(std::span(output)));
        if constexpr(Kind==reader_kind::range)next.read_unchecked(q,16,output);
        if constexpr(Kind==reader_kind::region)next.read16_unchecked(q,output);
    };
    for(auto q:f.queries) {call(q);for(unsigned j=0;j<16;++j)if(output[j]!=f.values[(q&~std::size_t{15})+j])std::abort();}
    std::size_t cursor=0;
    for(auto _:state) {
        std::uint64_t total=0;
        for(unsigned b=0;b<256;++b) {
            call(f.queries[cursor++]);cursor&=f.queries.size()-1;
            if constexpr(Ends)total+=output[0]+output[15];else for(auto x:output)total+=x;
        }
        benchmark::DoNotOptimize(total);benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations()*256);
    state.counters["encoded_bytes"]=f.bytes;
    state.counters["logical_values"]=f.count;
    state.counters["queries_per_iteration"]=256;
}

template<unsigned K,sp::geometry G,reader_kind Kind> void point(benchmark::State& state) {
    comparison_fixture<K,G> f(std::max<std::size_t>(256,(4096*8/K/256)*256));
    auto next=f.template next<std::uint64_t>();auto current=f.current();
    auto prior=[] {if constexpr(K==56)return seriespack_measurement::predecessor56().get;
        else return seriespack_measurement::predecessor(K,G==sp::geometry::striped).get;}();
    auto call=[&](std::size_t q) {
        if constexpr(Kind==reader_kind::prior)return prior(f.storage.get(),q);
        else if constexpr(Kind==reader_kind::current)return current.get(q);
        else return next.get_unchecked(q);
    };
    for(auto q:f.queries)if(call(q)!=f.values[q])std::abort();
    std::size_t cursor=0;std::uint64_t total=0;
    for(auto _:state) {
        for(unsigned b=0;b<256;++b){total+=call(f.queries[cursor++]);cursor&=f.queries.size()-1;}
        benchmark::DoNotOptimize(total);
    }
    state.SetItemsProcessed(state.iterations()*256);
}
template<unsigned K,sp::geometry G,reader_kind Kind> void bulk(benchmark::State& state) {
    using U=sp::uint_for<K>;comparison_fixture<K,G> f(8192);
    auto next=f.template next<U>();auto current=f.current();std::vector<U> output(f.count);
    auto prior=[] {if constexpr(K==56)return seriespack_measurement::predecessor56().decode;
        else return seriespack_measurement::predecessor(K,G==sp::geometry::striped).decode;}();
    auto call=[&] {
        if constexpr(Kind==reader_kind::prior)prior(f.storage.get(),output.data(),f.count);
        else if constexpr(Kind==reader_kind::current)current.decode({0,f.count},old::output_values(std::span(output)));
        else next.read_unchecked(0,f.count,output.data());
    };
    call();for(unsigned i=0;i<f.count;++i)if(output[i]!=f.values[i])std::abort();
    for(auto _:state){call();benchmark::ClobberMemory();}
    state.SetItemsProcessed(state.iterations()*f.count);
}

enum class consumer_kind { composed,composed16,next_materialized,current_materialized,current_grouped };
template<unsigned K,sp::geometry G,consumer_kind Kind,bool Masked> void consumer(benchmark::State& state) {
    using F=sp::format<K,G>;using U=sp::uint_for<K>;
    comparison_fixture<K,G> f(8192);auto next=f.template next<U>();auto current=f.current();
    const auto source=sp::dense_source<F>{f.storage.get(),f.count};
    const auto expression=sp::composition::describe(source);
    std::vector<U> scratch(f.count);
    const std::uint64_t cutoff=K==64?0x9123456789abcdefULL:((std::uint64_t{1}<<(K-1))+3);
    constexpr std::uint16_t mask=Masked?0xeeee:0xffff;
    auto run=[&] {
        if constexpr(Kind==consumer_kind::composed)
            return sp::composition::sum_regions(expression,f.count,cutoff,[](std::size_t){return mask;});
        else if constexpr(Kind==consumer_kind::composed16)
            return sp::composition::sum_regions<false>(expression,f.count,cutoff,[](std::size_t){return mask;});
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        else if constexpr(Kind==consumer_kind::current_grouped) {
            using OF=old::static_format<K,old::geometry::local8>;
            using Ops=old::avx512::deferred_sum_ops<old::avx512::grouped_local_ops<OF>>;
            const auto ov=old::static_const_view<OF>::assume_valid(f.count,
                {{{reinterpret_cast<const std::byte*>(f.storage.get()),f.bytes},F::tile_bytes},{}});
            const auto ox=old::composition::describe(ov);Ops ops;
            auto total=old::avx512::deferred_u64_sum::zero();
            constexpr std::uint64_t active=Masked?0xeeeeeeeeeeeeeeeeULL:~std::uint64_t{0};
            for(std::size_t i=0;i<f.count;i+=Ops::lanes)
                total=total.plus(old::composition::selected_sum(ops,ox,old::avx512::grouped_position{i},Ops::active_bits(active),cutoff));
            return total.finish();
        }
#endif
        else {
            if constexpr(Kind==consumer_kind::next_materialized)next.read_unchecked(0,f.count,scratch.data());
            else current.decode({0,f.count},old::output_values(std::span(scratch)));
            std::uint64_t total=0;
            for(std::size_t i=0;i<f.count;++i)if((mask&(1u<<(i%16))) && scratch[i]<cutoff)total+=scratch[i];
            return total;
        }
    };
    std::uint64_t expected=0;for(std::size_t i=0;i<f.count;++i)if((mask&(1u<<(i%16)))&&f.values[i]<cutoff)expected+=f.values[i];
    if(run()!=expected)std::abort();
    for(auto _:state){auto total=run();benchmark::DoNotOptimize(total);benchmark::ClobberMemory();}
    state.SetItemsProcessed(state.iterations()*f.count);
    state.counters["logical_values"]=f.count;
    state.counters["encoded_bytes"]=f.bytes;
}

template<unsigned K,sp::geometry G> void register_bulk() {
    const auto suffix=std::string(G==sp::geometry::local?"local/":"striped/")+"k"+std::to_string(K);
    benchmark::RegisterBenchmark(("decode/"+suffix+"/predecessor").c_str(),bulk<K,G,reader_kind::current>);
    benchmark::RegisterBenchmark(("decode/"+suffix+"/current").c_str(),bulk<K,G,reader_kind::range>);
    if constexpr(K<=7 || K==56) {
        benchmark::RegisterBenchmark(("decode/"+suffix+"/prior").c_str(),bulk<K,G,reader_kind::prior>);
        benchmark::RegisterBenchmark(("point/"+suffix+"/prior").c_str(),point<K,G,reader_kind::prior>);
        benchmark::RegisterBenchmark(("point/"+suffix+"/predecessor").c_str(),point<K,G,reader_kind::current>);
        benchmark::RegisterBenchmark(("point/"+suffix+"/current").c_str(),point<K,G,reader_kind::range>);
    }
}
template<unsigned K,sp::geometry G,bool Ends=false> void register_access() {
    const auto name=std::string(Ends?"get16-ends/":"get16/")+(G==sp::geometry::local?"local":"striped")+"/k"+std::to_string(K);
    benchmark::RegisterBenchmark((name+"/prior").c_str(),access<K,G,reader_kind::prior,Ends>);
    benchmark::RegisterBenchmark((name+"/predecessor").c_str(),access<K,G,reader_kind::current,Ends>);
    benchmark::RegisterBenchmark((name+"/current-range").c_str(),access<K,G,reader_kind::range,Ends>);
    benchmark::RegisterBenchmark((name+"/current-region").c_str(),access<K,G,reader_kind::region,Ends>);
}
template<unsigned K,sp::geometry G,bool Masked> void register_consumer() {
    const auto name=std::string("sum/")+(G==sp::geometry::local?"local":"striped")+"/k"+std::to_string(K)+(Masked?"/prefilter75":"/all");
    benchmark::RegisterBenchmark((name+"/current-native").c_str(),consumer<K,G,consumer_kind::composed,Masked>);
    benchmark::RegisterBenchmark((name+"/current-native16").c_str(),consumer<K,G,consumer_kind::composed16,Masked>);
    benchmark::RegisterBenchmark((name+"/current-materialized").c_str(),consumer<K,G,consumer_kind::next_materialized,Masked>);
    benchmark::RegisterBenchmark((name+"/predecessor-materialized").c_str(),consumer<K,G,consumer_kind::current_materialized,Masked>);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    if constexpr(G==sp::geometry::local && K>=9 && K<=32)
        benchmark::RegisterBenchmark((name+"/predecessor-grouped-deferred").c_str(),consumer<K,G,consumer_kind::current_grouped,Masked>);
#endif
}
void register_ordinary();
void register_mutations();
void register_pipelines();
void register_packet_pipelines();
void register_placed_low();void register_placed_mid();void register_placed_wide();
int main(int argc,char** argv) {
    register_ordinary();
    register_mutations();
    register_pipelines();
    register_packet_pipelines();
    register_placed_low();register_placed_mid();register_placed_wide();
    sp::detail::each<64>([](auto k){register_bulk<k+1,sp::geometry::local>();
        if constexpr(sp::striped_width(k+1))register_bulk<k+1,sp::geometry::striped>();});
    sp::detail::each<7>([](auto k) {register_access<k+1,sp::geometry::local>();register_access<k+1,sp::geometry::striped>();});
    register_access<56,sp::geometry::local>();
    sp::detail::each<7>([](auto k) {register_access<k+1,sp::geometry::local,true>();register_access<k+1,sp::geometry::striped,true>();});
    register_access<56,sp::geometry::local,true>();
    auto sums=[]<unsigned K,sp::geometry G=sp::geometry::local> {
        register_consumer<K,G,false>();register_consumer<K,G,true>();
    };
    sums.template operator()<5>();sums.template operator()<7>();sums.template operator()<12>();
    sums.template operator()<31>();sums.template operator()<56>();sums.template operator()<64>();
    sums.template operator()<12,sp::geometry::striped>();
    benchmark::Initialize(&argc,argv);if(benchmark::ReportUnrecognizedArguments(argc,argv))return 1;
    benchmark::RunSpecifiedBenchmarks();benchmark::Shutdown();
}
