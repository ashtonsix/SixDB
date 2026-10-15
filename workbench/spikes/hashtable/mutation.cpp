#include "aggregate.h"

namespace ht {
template<class Map> [[gnu::noinline]] word accumulate_bound(Map& map,std::span<const record> input) {
    auto writer=map.bind_write();word distinct=0;
    for(auto r:input) {
        auto delta=writer.upsert(r.key,r.value,add{});
        require(delta>=0,"bound accumulation admission");distinct+=delta;
    }
    return distinct;
}
template<class Map,bool Bound=false> void mutation_trial(
        oa::Pool& pool,const trace& input,const char* arm,const char* history,unsigned rep) {
    auto ns=[](auto at) {return std::chrono::duration<double,std::nano>(clock_type::now()-at).count();};
    auto begin=clock_type::now();std::optional<Map> storage;
    storage.emplace(pool,mix(8301+rep*2),mix(8302+rep*2));auto& map=*storage;
    if constexpr(requires{map.reserve(1);})map.reserve(input.keys.size());
    auto setup=ns(begin);begin=clock_type::now();word distinct;
    if constexpr(Bound)distinct=accumulate_bound(map,input.rows);
    else distinct=accumulate(map,input.rows);
    auto ingest=ns(begin);
    require(distinct==input.expected.size(),"bound caller occupancy");
    map.validate();std::unordered_map<word,word> actual;
    map.each([&](word k,word v) {require(actual.emplace(k,v).second,"mutation unique keys");});
    require(actual==input.expected,"mutation independent oracle");auto mem=map.footprint();
    word primary=0,secondary=0,chain=0;
    if constexpr(requires{map.distribution();}) {auto d=map.distribution();primary=d.primary;secondary=d.secondary;chain=d.chain;}
    begin=clock_type::now();storage.reset();auto release=ns(begin);
    std::printf("%s,%s,%zu,%zu,%u,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu\n",arm,history,
        input.keys.size(),input.rows.size(),rep,setup,ingest,release,(unsigned long long)mem.requested,
        (unsigned long long)mem.backing,(unsigned long long)mem.metadata,(unsigned long long)primary,
        (unsigned long long)secondary,(unsigned long long)chain);
    std::fflush(stdout);
}
template<unsigned K> void bound_check(oa::Pool& pool) {
    table<K,3,0,64,64> map(pool,43,62);auto writer=map.bind_write();
    std::unordered_map<word,word> oracle;std::mt19937_64 random(18492);
    std::vector<word> keys{0,mask(K)};
    while(keys.size()<200) {
        auto k=random()&mask(K);
        if(std::find(keys.begin(),keys.end(),k)==keys.end())keys.push_back(k);
    }
    for(unsigned i=0;i<3000;++i) {
        if(i%11==0) {
            auto victim=keys[random()%keys.size()];
            require(map.erase(victim)==(oracle.erase(victim)!=0),"bound cohort dense deletion");
        }
        auto key=keys[random()%keys.size()],value=random();auto [it,fresh]=oracle.try_emplace(key,value);
        unsigned calls=0;
        auto delta=writer.upsert(key,value,[&](word old,word incoming)noexcept {++calls;return old+incoming;});
        require(delta==int(fresh)&&calls==unsigned(!fresh),"bound single combination");
        if(!fresh)it->second+=value;
    }
    map.validate();std::unordered_map<word,word> actual;
    map.each([&](word k,word v){require(actual.emplace(k,v).second,"bound uniqueness");});
    require(actual==oracle,"all-width bound oracle");
}
void bound_refusal() {
    pool_owner pool(1u<<20);table<40,2,0,64,64> map(*pool.pool,71,83);
    auto writer=map.bind_write();std::unordered_map<word,word> oracle;
    for(word i=0;i<100;++i) {require(writer.upsert(i,i,add{})==1,"bound pressure setup");oracle[i]=i;}
    std::vector<oa::Allocation> pressure;
    while(auto a=oa::allocate(*pool.pool,{.bytes=4096,.alignment=64}))pressure.push_back(std::move(*a));
    word refused=0;
    for(word i=100;i<4000;++i) {
        auto d=writer.upsert(i,i,add{});if(d<0) {refused=i;break;}
        require(d==1,"bound new-key delta");oracle[i]=i;
    }
    require(refused>0,"bound forced refusal");
    word sentinel=891;require(!map.get_value(refused,sentinel)&&sentinel==891,"bound refusal invisible");
    require(writer.upsert(0,3,add{})==0,"bound existing update under pressure");oracle[0]+=3;
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"bound trim");orbital::worker::poll(pool.lane,1024);
    }
    // Chain-only movement does not invalidate primary geometry. The miss body
    // resolves its own StreamPack windows again, including after refusal.
    while(writer.upsert(refused,refused,add{})<0) {
        require(clock_type::now()<deadline,"bound recovery");orbital::worker::poll(pool.lane,1024);
    }
    oracle[refused]=refused;map.validate();std::unordered_map<word,word> actual;
    map.each([&](word k,word v){require(actual.emplace(k,v).second,"bound pressure uniqueness");});
    require(actual==oracle,"bound pressure oracle");
}
template<unsigned A> void campaign(oa::Pool& pool,unsigned n,unsigned reps) {
    using map=table<40,A,A-4,128,128>;
    const char* histories[]={"distinct","repeated","skewed"};
    for(unsigned h=0;h<3;++h) {
        trace input(n,h?16:1,h==2);
        for(unsigned rep=0;rep<reps;++rep)for(unsigned j=0;j<3;++j)switch((rep+j)%3) {
            case 0:mutation_trial<abseil<40>>(pool,input,"abseil",histories[h],rep);break;
            case 1:mutation_trial<map>(pool,input,"ordinary",histories[h],rep);break;
            case 2:mutation_trial<map,true>(pool,input,"bound",histories[h],rep);break;
        }
    }
}
} // namespace ht
int main(int argc,char** argv) {
    using namespace ht;auto mode=argc>1?std::string_view(argv[1]):"check";
    auto reps=argc>2?std::stoul(argv[2]):9;require(reps>0,"mutation repetitions");
    std::puts("arm,history,n,rows,rep,setup_ns,ingest_ns,release_ns,requested,owned_capacity,metadata,primary,secondary,chain");
    if(mode=="check") {
        bound_refusal();pool_owner pool;
        bound_check<8>(*pool.pool);bound_check<16>(*pool.pool);bound_check<24>(*pool.pool);bound_check<32>(*pool.pool);
        bound_check<40>(*pool.pool);bound_check<48>(*pool.pool);bound_check<56>(*pool.pool);bound_check<64>(*pool.pool);
        trace input(300,16,true);mutation_trial<table<40,3,0,64,64>,true>(*pool.pool,input,"forced_chain","skewed",0);
    } else {
        pool_owner pool;
        if(mode=="hot")campaign<8>(*pool.pool,1800,reps);
        else {require(mode=="large","mutation mode");campaign<14>(*pool.pool,110000,reps);}
    }
}
