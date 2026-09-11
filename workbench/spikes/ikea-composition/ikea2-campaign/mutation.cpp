#include "fixture.h"
#include <ikea/seriespack/detail/mutation/physical.h>

enum class mutation_kind {raw,coverage,maintenance,current,prior,materialized_maintenance,control_coverage};
template<unsigned K,sp::geometry G,mutation_kind Kind,bool Masked>
[[gnu::noinline]] std::uint64_t write_bound(const sp::view<sp::format<K,G>,std::uint8_t>& destination,
    const sp::uint_for<K>* input,sp::write_journal& journal) {
    constexpr std::uint16_t mask=Masked?0xeeee:0xffff;
    auto active=[](std::size_t){return mask;};
    if constexpr(Kind==mutation_kind::maintenance) {
        sp::sum_change summary;
        sp::replace_regions_unchecked(destination,0,destination.size(),input,active,summary,journal);
        return summary.finish();
    } else {
        sp::no_summary summary;
        if constexpr(Kind==mutation_kind::coverage)sp::replace_regions_unchecked(destination,0,destination.size(),input,active,summary,journal);
        else {sp::no_coverage nothing;sp::replace_regions_unchecked(destination,0,destination.size(),input,active,summary,nothing);}
        return 0;
    }
}
template<unsigned K,sp::geometry G,mutation_kind Kind,bool Masked> void mutation(benchmark::State& state) {
    using U=sp::uint_for<K>;using F=sp::format<K,G>;
    comparison_fixture<K,G> f(8192);
    const auto destination=sp::view<F,std::uint8_t>::attach(f.count,{{{{f.storage.get(),f.bytes},F::tile_bytes},{},{}}});
    if(!destination)std::abort();
    auto current_view=old::mutable_view::attach({K,0,comparison_fixture<K,G>::OG},f.count,
        {{{reinterpret_cast<std::byte*>(f.storage.get()),f.bytes},F::tile_bytes},{}});
    if(!current_view)std::abort();auto current=old::bind_encoder(*current_view);if(!current)std::abort();
    auto prior=[] {if constexpr(K==56){auto best=seriespack_measurement::predecessor56(true).encode;return best?best:seriespack_measurement::predecessor56(false).encode;}
        else return seriespack_measurement::predecessor(K,G==sp::geometry::striped).encode;}();
    std::array<std::vector<U>,2> input{std::vector<U>(f.count),std::vector<U>(f.count)};
    for(std::size_t i=0;i<f.count;++i){input[0][i]=f.values[i];input[1][i]=f.values[i]^(~std::uint64_t{0}>>(64-K));}
    std::vector<sp::byte_write> records(f.count/16*6);sp::write_journal journal{records};
    auto before_reader=f.current();std::vector<U> before(f.count);

    auto run=[&](unsigned which) {
        journal.used=0;
        if constexpr(Kind==mutation_kind::current){current->encode(old::input_values(std::span<const U>(input[which])));return std::uint64_t{0};}
        else if constexpr(Kind==mutation_kind::prior){if(!prior)std::abort();prior(input[which].data(),f.storage.get(),f.count);return std::uint64_t{0};}
        else if constexpr(Kind==mutation_kind::control_coverage) {
            journal.before({0,0,f.bytes});
            if constexpr(K<=7 || K==56){if(!prior)std::abort();prior(input[which].data(),f.storage.get(),f.count);}
            else current->encode(old::input_values(std::span<const U>(input[which])));
            return std::uint64_t{0};
        }
        else if constexpr(Kind==mutation_kind::materialized_maintenance) {
            before_reader.decode({0,f.count},old::output_values(std::span<U>(before)));
            std::uint64_t delta=0;
            for(std::size_t i=0;i<f.count;++i)if(!Masked || i%4!=0) {
                delta+=std::uint64_t(input[which][i])-before[i];before[i]=input[which][i];
            }
            journal.before({0,0,f.bytes});
            current->encode(old::input_values(std::span<const U>(before)));
            return delta;
        } else return write_bound<K,G,Kind,Masked>(*destination,input[which].data(),journal);
    };
    for(unsigned which:{1u,0u}) {
        const auto delta=run(which);std::uint64_t expected_delta=0;
        for(std::size_t i=0;i<f.count;++i) {
            const bool active=!Masked || i%4!=0;
            const auto expected=active?input[which][i]:input[0][i];
            if(sp::get_unchecked(*destination,i)!=expected)std::abort();
            if(active)expected_delta+=std::uint64_t(input[which][i])-input[1-which][i];
        }
        if constexpr(Kind==mutation_kind::maintenance || Kind==mutation_kind::materialized_maintenance)if(delta!=expected_delta)std::abort();
    }
    unsigned which=1;
    for(auto _:state){auto delta=run(which);which^=1;benchmark::DoNotOptimize(delta);benchmark::ClobberMemory();}
    state.SetItemsProcessed(state.iterations()*f.count);
    state.counters["logical_values"]=f.count;
    state.counters["selected_values"]=Masked?f.count*3/4:f.count;
    state.counters["write_records"]=journal.used;
}
template<unsigned K,sp::geometry G,bool Masked> void register_mutation() {
    const auto name=std::string("overwrite/")+(G==sp::geometry::local?"local/":"striped/")+"k"+std::to_string(K)+(Masked?"/prefilter75":"/all");
    benchmark::RegisterBenchmark((name+"/current-raw").c_str(),mutation<K,G,mutation_kind::raw,Masked>);
    benchmark::RegisterBenchmark((name+"/current-coverage").c_str(),mutation<K,G,mutation_kind::coverage,Masked>);
    benchmark::RegisterBenchmark((name+"/current-sum-coverage").c_str(),mutation<K,G,mutation_kind::maintenance,Masked>);
    benchmark::RegisterBenchmark((name+"/predecessor-materialized-sum-coverage").c_str(),mutation<K,G,mutation_kind::materialized_maintenance,Masked>);
    if constexpr(!Masked){
        benchmark::RegisterBenchmark((name+"/control-coverage").c_str(),mutation<K,G,mutation_kind::control_coverage,false>);
        benchmark::RegisterBenchmark((name+"/predecessor").c_str(),mutation<K,G,mutation_kind::current,false>);
        if constexpr(K<=7 || K==56)benchmark::RegisterBenchmark((name+"/prior").c_str(),mutation<K,G,mutation_kind::prior,false>);
    }
}
template<unsigned K,sp::geometry G> void register_overwrite_all() {
    const auto name=std::string("overwrite-all/")+(G==sp::geometry::local?"local/":"striped/")+"k"+std::to_string(K);
    benchmark::RegisterBenchmark((name+"/current").c_str(),mutation<K,G,mutation_kind::raw,false>);
    benchmark::RegisterBenchmark((name+"/predecessor").c_str(),mutation<K,G,mutation_kind::current,false>);
    if constexpr(K<=7 || K==56)benchmark::RegisterBenchmark((name+"/prior").c_str(),mutation<K,G,mutation_kind::prior,false>);
}
void register_mutations() {
    sp::detail::each<64>([](auto k){
        register_overwrite_all<k+1,sp::geometry::local>();
        if constexpr(sp::striped_width(k+1))register_overwrite_all<k+1,sp::geometry::striped>();
    });
    auto add=[]<unsigned K,sp::geometry G=sp::geometry::local>{register_mutation<K,G,false>();register_mutation<K,G,true>();};
    add.template operator()<3>();add.template operator()<7>();add.template operator()<12>();
    add.template operator()<31>();add.template operator()<56>();add.template operator()<64>();
    add.template operator()<3,sp::geometry::striped>();add.template operator()<7,sp::geometry::striped>();
    add.template operator()<12,sp::geometry::striped>();
}
