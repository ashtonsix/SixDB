#include "aggregate.h"

namespace ht {
template<class Map,bool TwoSearch=false>
void trial(oa::Pool& pool,const char* name,const char* history,const trace& input,unsigned rep) {
    auto begin=clock_type::now();std::optional<Map> storage;
    storage.emplace(pool,mix(8301+rep*2),mix(8302+rep*2));auto& map=*storage;
    if constexpr(requires{map.reserve(input.keys.size());})map.reserve(input.keys.size());
    auto ns=[](auto before) {return std::chrono::duration<double,std::nano>(clock_type::now()-before).count();};
    double setup=ns(begin);begin=clock_type::now();
    auto distinct=accumulate<TwoSearch>(map,input.rows);double ingest=ns(begin);
    require(distinct==input.expected.size(),"caller distinct occupancy");
    auto mem=map.footprint();word expected_sum=0;
    map.validate();word seen=0;
    map.each([&](word k,word v) {
        auto it=input.expected.find(k);require(it!=input.expected.end()&&it->second==v,"independent aggregate oracle");
        ++seen;
    });
    require(seen==distinct,"aggregate iteration cardinality");
    for(auto [k,v]:input.expected)expected_sum+=v;
    require(consume_values(map,input.keys)==expected_sum,"aggregate read warmup");
    begin=clock_type::now();auto sum=consume_values(map,input.keys);double read=ns(begin);
    require(sum==expected_sum,"aggregate read checksum");
    word primary=0,secondary=0,chain=0;
    if constexpr(requires{map.distribution();}) {
        auto d=map.distribution();primary=d.primary;secondary=d.secondary;chain=d.chain;
    }
    begin=clock_type::now();storage.reset();double release=ns(begin);
    std::printf("%s,%s,%zu,%zu,%u,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu\n",name,history,
        input.keys.size(),input.rows.size(),rep,setup,ingest,read,release,(unsigned long long)mem.requested,
        (unsigned long long)mem.backing,(unsigned long long)mem.metadata,(unsigned long long)primary,
        (unsigned long long)secondary,(unsigned long long)chain);
    std::fflush(stdout);
}
template<unsigned A> void campaign(oa::Pool& pool,unsigned n,unsigned repeats) {
    using compact=table<40,A,A-1,64,128>;using fuller=table<40,A,A-4,128,128>;
    const char* histories[]={"distinct","repeated","skewed"};
    for(unsigned h=0;h<3;++h) {
        trace input(n,h?16:1,h==2);
        for(unsigned rep=0;rep<repeats;++rep)for(unsigned j=0;j<4;++j)switch((rep+j)%4) {
            case 0:trial<abseil<40>>(pool,"abseil",histories[h],input,rep);break;
            case 1:trial<compact>(pool,"compact",histories[h],input,rep);break;
            case 2:trial<fuller>(pool,"fuller",histories[h],input,rep);break;
            case 3:trial<fuller,true>(pool,"fuller_twosearch",histories[h],input,rep);break;
        }
    }
}
} // namespace ht
int main(int argc,char** argv) {
    using namespace ht;
    auto mode=argc>1?std::string_view(argv[1]):"check";auto reps=argc>2?std::stoul(argv[2]):7;
    require(reps>0,"repetitions");pool_owner pool;
    std::puts("arm,history,n,rows,rep,setup_ns,ingest_ns,read_ns,release_ns,requested,owned_capacity,metadata,primary,secondary,chain");
    if(mode=="check") {
        trace input(300,16,true);
        trial<table<40,3,0,64,64>>(*pool.pool,"forced_chain","skewed",input,0);
        trial<table<40,3,0,64,64>,true>(*pool.pool,"forced_chain_twosearch","skewed",input,0);
        trial<abseil<40>>(*pool.pool,"abseil","skewed",input,0);
    } else if(mode=="hot") campaign<8>(*pool.pool,1800,reps);
    else {require(mode=="large","known mode");campaign<14>(*pool.pool,110000,reps);}
}
