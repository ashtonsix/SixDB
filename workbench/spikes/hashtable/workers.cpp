#include "aggregate.h"
#include <barrier>
#include <fstream>
#include <sched.h>
#include <set>
#ifndef HT_CONCURRENT_WORKERS
#define HT_CONCURRENT_WORKERS 0
#endif
#if HT_CONCURRENT_WORKERS
#include "concurrent_baselines.h"
#endif

namespace ht {
unsigned partition(word key) {return unsigned(key>>39);}
double elapsed(clock_type::time_point begin) {
    return std::chrono::duration<double,std::nano>(clock_type::now()-begin).count();
}
struct worker_input {
    trace whole;
    std::array<std::vector<record>,2> intake;
    std::array<word,2> local_distinct{},partition_distinct{};
    word misroutes=0;
    worker_input(unsigned n,unsigned repetitions,bool skew,unsigned mismatch_percent)
      :whole(n,repetitions,skew) {
        std::array<std::set<word>,2> keys;
        std::mt19937_64 random(384193);
        for(auto r:whole.rows) {
            auto home=partition(r.key),target=home;
            if(random()%100<mismatch_percent) {target^=1;++misroutes;}
            intake[target].push_back(r);keys[target].insert(r.key);
        }
        for(unsigned i=0;i<2;++i)local_distinct[i]=keys[i].size();
        for(auto k:whole.keys)++partition_distinct[partition(k)];
    }
};

template<class Map> void verify_map(const Map& map,const std::unordered_map<word,word>& expected) {
    map.validate();std::unordered_map<word,word> actual;
    map.each([&](word k,word v) {require(actual.emplace(k,v).second,"unique merged keys");});
    require(actual==expected,"independent merged values");
}
template<class Map> [[gnu::noinline]] word rebuild_merge(Map& target,const Map& source) {
    word added=0;
    source.each([&](word k,word v) __attribute__((always_inline)) {
        auto delta=target.upsert(k,v,add{});require(delta>=0,"rebuild merge capacity");added+=delta;
    });
    return added;
}
template<class Map> [[gnu::noinline]] word route_local(
        Map& map,std::span<const record> input,unsigned worker,std::vector<record>& outgoing) {
    word distinct=0;
    for(auto r:input) {
        if(partition(r.key)!=worker) {outgoing.push_back(r);continue;}
        auto delta=map.upsert(r.key,r.value,add{});
        require(delta>=0,"routed accumulation capacity");distinct+=delta;
    }
    return distinct;
}
template<bool Partitioned,class Map> [[gnu::noinline]] word read_result(
        const std::array<Map*,2>& maps,unsigned receiver,std::span<const word> keys) {
    word sum=0;
    for(auto k:keys) {
        word v=0;auto* map=maps[Partitioned?partition(k):receiver];
        require(map->get_value(k,v),"post-retirement result read");sum+=v;
    }
    asm volatile("" :: "r"(sum) : "memory");return sum;
}
std::array<unsigned,2> worker_cpus() {
    cpu_set_t allowed;CPU_ZERO(&allowed);
    require(!sched_getaffinity(0,sizeof(allowed),&allowed),"read worker affinity");
    std::array<unsigned,2> out{};unsigned n=0;std::set<std::pair<int,int>> cores;
    for(unsigned cpu=0;cpu<CPU_SETSIZE && n<2;++cpu)if(CPU_ISSET(cpu,&allowed)) {
        auto base="/sys/devices/system/cpu/cpu"+std::to_string(cpu)+"/topology/";
        int package=-1,core=-1;
        std::ifstream(base+"physical_package_id")>>package;std::ifstream(base+"core_id")>>core;
        require(package>=0&&core>=0,"read physical CPU topology");
        if(cores.emplace(package,core).second)out[n++]=cpu;
    }
    require(n==2,"worker comparison requires two allowed physical cores");return out;
}
void pin(unsigned cpu) {
    cpu_set_t selected;CPU_ZERO(&selected);CPU_SET(cpu,&selected);
    require(!sched_setaffinity(0,sizeof(selected),&selected),"pin worker");
}

// Every table stays on its original owner Lane through allocation, merge,
// read-only result borrowing and destruction. The coordinator only reads after
// both owners have stopped mutation; no object moves to a different Pool.
template<class Map,bool Partitioned,bool Encoded=false>
void worker_trial(const worker_input& input,const char* arm,const char* history,unsigned rep,
                  const std::array<unsigned,2>& cpus) {
    struct alignas(64) state {
        Map* map=nullptr;
        std::vector<record> outgoing;
        word distinct=0,added=0,processed=0;
        memory before{},after{};
        double local_ns=0,finish_ns=0;
    };
    std::array<state,2> states;
    std::barrier sync(3);unsigned receiver=0;
    auto start=clock_type::now();
    auto work=[&](unsigned id) {
        pin(cpus[id]);pool_owner owner;std::optional<Map> storage;
        sync.arrive_and_wait(); // long-lived runtime ready
        sync.arrive_and_wait(); // begin setup
        storage.emplace(*owner.pool,mix(8301+rep*2),mix(8302+rep*2));auto& map=*storage;
        if constexpr(requires{map.reserve(1);})
            map.reserve(Partitioned?input.partition_distinct[id]:input.local_distinct[id]);
        states[id].map=&map;
        sync.arrive_and_wait(); // setup done
        sync.arrive_and_wait(); // begin intake
        auto begin=clock_type::now();
        if constexpr(Partitioned) states[id].distinct=route_local(map,input.intake[id],id,states[id].outgoing);
        else states[id].distinct=accumulate(map,input.intake[id]);
        states[id].local_ns=elapsed(begin);
        sync.arrive_and_wait(); // every source stable; receiver can be chosen
        sync.arrive_and_wait(); // untimed diagnostic census
        states[id].before=map.footprint();
        sync.arrive_and_wait();
        sync.arrive_and_wait(); // begin exchange/merge
        begin=clock_type::now();
        if constexpr(Partitioned) states[id].distinct+=accumulate(map,states[id^1].outgoing);
        else if(id==receiver) {
            if constexpr(Encoded) {
                typename Map::merge_cursor cursor;
                auto result=map.merge_from(*states[id^1].map,cursor,add{});
                require(result.complete,"encoded merge capacity");
                states[id].added=result.added;states[id].processed=result.processed;
            } else {
                states[id].added=rebuild_merge(map,*states[id^1].map);
                states[id].processed=states[id^1].distinct;
            }
            states[id].distinct+=states[id].added;
        }
        states[id].finish_ns=elapsed(begin);
        sync.arrive_and_wait(); // final result stable, sources still owned
        sync.arrive_and_wait(); // untimed diagnostic census
        states[id].after=map.footprint();
        sync.arrive_and_wait();
        sync.arrive_and_wait(); // retire source and routing buffers
        if constexpr(!Partitioned)if(id!=receiver)storage.reset();
        std::vector<record>().swap(states[id].outgoing);
        sync.arrive_and_wait();
        sync.arrive_and_wait(); // coordinator finished borrowed result reads
        storage.reset();
        sync.arrive_and_wait(); // allocations released, Pool drain separately
    };
    std::array<std::thread,2> threads{std::thread(work,0),std::thread(work,1)};
    sync.arrive_and_wait();double runtime=elapsed(start);
    start=clock_type::now();sync.arrive_and_wait();sync.arrive_and_wait();double setup=elapsed(start);
    start=clock_type::now();sync.arrive_and_wait();sync.arrive_and_wait();double intake=elapsed(start);
    sync.arrive_and_wait();sync.arrive_and_wait(); // before census, outside operation timing
    receiver=states[1].distinct>states[0].distinct?1:0;
    start=clock_type::now();sync.arrive_and_wait();sync.arrive_and_wait();double finish=elapsed(start);
    sync.arrive_and_wait();sync.arrive_and_wait(); // after census, outside operation timing
    auto bytes=[](memory a,memory b) {return memory{a.requested+b.requested,a.backing+b.backing,a.metadata+b.metadata};};
    auto coexist=bytes(states[0].after,states[1].after);
    auto retained=Partitioned?coexist:states[receiver].after;
    word routing_capacity=(states[0].outgoing.capacity()+states[1].outgoing.capacity())*sizeof(record);
    word routed=states[0].outgoing.size()+states[1].outgoing.size();
    require(routed==(Partitioned?input.misroutes:0),"routing count");
    // Validate before source retirement as well as through the final interface.
    std::unordered_map<word,word> actual;
    word primary=0,secondary=0,chain=0;
    for(unsigned id=0;id<2;++id)if(Partitioned||id==receiver) {
        auto& map=*states[id].map;map.validate();
        map.each([&](word k,word v) {
            require(!Partitioned||partition(k)==id,"partition ownership");
            require(actual.emplace(k,v).second,"adopted/merged uniqueness");
        });
        if constexpr(requires{map.distribution();}) {
            auto d=map.distribution();primary+=d.primary;secondary+=d.secondary;chain+=d.chain;
        }
    }
    require(actual==input.whole.expected,"worker aggregate oracle");
    if constexpr(Partitioned)require(states[0].distinct+states[1].distinct==actual.size(),"adopted count");
    else require(states[receiver].distinct==actual.size()&&states[receiver].processed==states[receiver^1].distinct,"merged deltas");
    start=clock_type::now();sync.arrive_and_wait();sync.arrive_and_wait();double retire=elapsed(start);
    word expected_sum=0;for(auto [k,v]:actual)expected_sum+=v;
    std::array<Map*,2> maps{states[0].map,states[1].map};
    require(read_result<Partitioned>(maps,receiver,input.whole.keys)==expected_sum,"worker read warmup");
    start=clock_type::now();auto sum=read_result<Partitioned>(maps,receiver,input.whole.keys);double read_ns=elapsed(start);
    require(sum==expected_sum,"worker read checksum");
    start=clock_type::now();sync.arrive_and_wait();sync.arrive_and_wait();double release=elapsed(start);
    start=clock_type::now();for(auto& thread:threads)thread.join();double drain=elapsed(start);
    auto before=bytes(states[0].before,states[1].before);
    std::printf("%s,%s,%zu,%zu,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
        arm,history,input.whole.keys.size(),input.whole.rows.size(),rep,cpus[0],cpus[1],receiver,
        (unsigned long long)input.local_distinct[0],(unsigned long long)input.local_distinct[1],
        (unsigned long long)input.misroutes,(unsigned long long)routed,runtime,setup,intake,finish,retire,read_ns,release,drain,
        (unsigned long long)before.requested,(unsigned long long)coexist.requested,(unsigned long long)retained.requested,
        (unsigned long long)coexist.backing,(unsigned long long)retained.backing,
        (unsigned long long)coexist.metadata,(unsigned long long)retained.metadata,(unsigned long long)routing_capacity,
        (unsigned long long)states[receiver].added,(unsigned long long)primary,(unsigned long long)secondary,(unsigned long long)chain);
    unsigned a=0,b=0,p=0,q=0;
    if constexpr(requires{Map::primary_bits;}) {a=Map::primary_bits;b=Map::secondary_bits;p=Map::primary.slots;q=Map::secondary.slots;}
    std::printf(",%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f\n",a,b,p,q,states[0].local_ns,states[1].local_ns,
        states[0].finish_ns,states[1].finish_ns);
    std::fflush(stdout);
}

#if HT_CONCURRENT_WORKERS
// These are shared ordinary library owners, not worker-local maps with a
// synthetic merge cost. Addition is unsigned modular arithmetic, so the final
// oracle is independent of the interleaving. No concurrent-reader claim is
// needed: result consumption starts after both writers stop.
template<class Map> [[gnu::noinline]] word shared_accumulate(
        Map& map,std::span<const record> input) {
    word inserted=0;
    for(auto r:input)inserted+=map.add(r.key,r.value);
    return inserted;
}
template<class Map> [[gnu::noinline]] word read_shared_result(
        const Map& map,std::span<const word> keys) {
    word sum=0;
    for(auto key:keys)require(map.template consume<true>(key,[&](word value){sum+=value;}),
                             "shared result lookup");
    asm volatile("" :: "r"(sum) : "memory");return sum;
}
template<class Map> void shared_worker_trial(const worker_input& input,const char* arm,
        const char* history,unsigned rep,const std::array<unsigned,2>& cpus) {
    struct alignas(64) state {word inserted=0;double local_ns=0;};
    std::array<state,2> states;
    // Worker 0 constructs/reserves, first-touches and destroys the shared owner.
    // Synchronization publishes the pointer and ends mutation before borrowing.
    // No Orbital Pool or counting allocator is inserted into these baselines.
    Map* map=nullptr;std::barrier sync(3);
    auto start=clock_type::now();
    auto work=[&](unsigned id) {
        pin(cpus[id]);std::optional<Map> storage;
        sync.arrive_and_wait(); // reusable worker threads ready
        sync.arrive_and_wait(); // setup begins
        if(!id){storage.emplace();storage->reserve(input.whole.keys.size());map=&*storage;}
        sync.arrive_and_wait(); // owner published
        sync.arrive_and_wait(); // intake begins
        auto begin=clock_type::now();
        states[id].inserted=shared_accumulate(*map,input.intake[id]);
        states[id].local_ns=elapsed(begin);
        sync.arrive_and_wait(); // final result; both writers are quiescent
        sync.arrive_and_wait(); // coordinator finished verification and reads
        if(!id)storage.reset();
        sync.arrive_and_wait(); // ordinary owner released
    };
    std::array<std::thread,2> threads{std::thread(work,0),std::thread(work,1)};
    sync.arrive_and_wait();const double runtime=elapsed(start);
    start=clock_type::now();sync.arrive_and_wait();sync.arrive_and_wait();const double setup=elapsed(start);
    start=clock_type::now();sync.arrive_and_wait();sync.arrive_and_wait();const double intake=elapsed(start);
    // Enumeration and the oracle are outside timing. A successful insertion is
    // counted by exactly one writer even when their keys overlap.
    std::unordered_map<word,word> actual;
    map->each_quiescent([&](word key,word value){
        require(actual.emplace(key,value).second,"shared unique result keys");
    });
    require(actual==input.whole.expected,"shared worker aggregate oracle");
    require(map->size_quiescent()==actual.size() &&
        states[0].inserted+states[1].inserted==actual.size(),"shared insertion deltas");
    word expected_sum=0;for(auto [key,value]:actual)expected_sum+=value;
    require(read_shared_result(*map,input.whole.keys)==expected_sum,"shared result read warmup");
    start=clock_type::now();const auto sum=read_shared_result(*map,input.whole.keys);const double read_ns=elapsed(start);
    require(sum==expected_sum,"shared result read checksum");
    start=clock_type::now();sync.arrive_and_wait();sync.arrive_and_wait();const double release=elapsed(start);
    start=clock_type::now();for(auto& thread:threads)thread.join();const double drain=elapsed(start);
    // Preserve the existing columns. Finish/retire and routing are genuinely
    // absent. Empty allocation cells mean unavailable, never zero bytes: these
    // owners are uninstrumented. Owner metadata is known; no RSS/peak claim.
    std::printf("%s,%s,%zu,%zu,%u,%u,%u,0,%llu,%llu,%llu,0,%.3f,%.3f,%.3f,0,0,%.3f,%.3f,%.3f,",
        arm,history,input.whole.keys.size(),input.whole.rows.size(),rep,cpus[0],cpus[1],
        (unsigned long long)input.local_distinct[0],(unsigned long long)input.local_distinct[1],
        (unsigned long long)input.misroutes,runtime,setup,intake,read_ns,release,drain);
    std::printf(",,,,,%zu,%zu,0,0,0,0,0,0,0,0,0,%.3f,%.3f,0,0\n",
        Map::owner_bytes(),Map::owner_bytes(),states[0].local_ns,states[1].local_ns);
    std::fflush(stdout);
}
#endif

// Mix placements independently, leave vacancies above descendants, and verify
// every key/value. Source refs identify independently allocated storage. The
// worker trials additionally keep source and receiver in distinct Pools.
void merge_check() {
    pool_owner owner;
    using map_type=table<40,3,0,128,64,false,true>;
    map_type source(*owner.pool,17,29),target(*owner.pool,17,29);
    std::unordered_map<word,word> expected,source_expected;
    auto input=make_input<40>(420,5421);
    for(unsigned i=0;i<300;++i) {
        auto k=input.keys[i],v=mix(i);require(target.set(k,v)==1,"merge target setup");expected[k]=v;
    }
    for(unsigned i=0;i<300;i+=3) {target.erase(input.keys[i]);expected.erase(input.keys[i]);}
    for(unsigned j=0;j<350;++j) {
        auto i=419-j;auto k=input.keys[i],v=j%7?mix(j+1):~word(0);
        require(source.set(k,v)==1,"merge source setup");source_expected[k]=v;expected[k]+=v;
    }
    typename map_type::merge_cursor cursor;
    auto result=target.merge_from(source,cursor,add{});
    require(result.complete&&result.processed==source_expected.size(),"complete merge cursor");
    verify_map(target,expected);verify_map(source,source_expected);
    auto again=target.merge_from(source,cursor,add{});
    require(again.complete&&!again.processed&&!again.added,"completed cursor is empty");
    verify_map(target,expected);
}

void merge_refusal_check() {
    pool_owner owner(1u<<20);
    using map_type=table<40,3,0,128,64,false,true>;
    map_type source(*owner.pool,17,29),target(*owner.pool,17,29);
    std::unordered_map<word,word> expected,source_expected;
    permutation<40,3> hash(17);std::vector<word> collisions;
    for(word k=0;collisions.size()<80;++k)if(hash.split(k).bucket==0)collisions.push_back(k);
    for(auto k:collisions) {require(target.set(k,mix(k))==1,"pressure target");expected[k]=mix(k);}
    // This key is chained in the receiver, but inserted first into source P.
    auto duplicate=collisions[map_type::primary.slots+map_type::secondary.slots+5];
    require(source.set(duplicate,~word(0))==1,"source primary duplicate");source_expected[duplicate]=~word(0);
    target.erase(collisions[0]);expected.erase(collisions[0]); // vacancy above duplicate
    for(word k=100000;k<104000;++k) {
        require(source.set(k,mix(k))==1,"pressure source");source_expected[k]=mix(k);
    }
    std::vector<record> order;
    source.each([&](word k,word v) {order.push_back({k,v});});
    require(order.front().key==duplicate,"first encoded source entry is downstream duplicate");
    std::vector<oa::Allocation> pressure;
    while(auto a=oa::allocate(*owner.pool,{.bytes=4096,.alignment=64}))pressure.push_back(std::move(*a));
    typename map_type::merge_cursor cursor;
    word processed=0,added=0,calls=0,expected_calls=0;
    auto combine=[&](word old,word incoming)noexcept {++calls;return old+incoming;};
    auto accept=[&](auto result) {
        word inserted=0;
        for(word i=0;i<result.processed;++i) {
            auto r=order[processed+i];auto [it,fresh]=expected.try_emplace(r.key,r.value);
            if(fresh)++inserted;else {it->second+=r.value;++expected_calls;}
        }
        require(inserted==result.added&&calls==expected_calls,"partial merge deltas/combiner count");
        processed+=result.processed;added+=result.added;
        verify_map(target,expected);verify_map(source,source_expected);
    };
    auto partial=target.merge_from(source,cursor,combine);
    require(!partial.complete&&partial.processed>0,"partial merge refusal after duplicate");accept(partial);
    auto refused_cursor=cursor;
    auto repeated=target.merge_from(source,cursor,combine);accept(repeated);
    require(!repeated.complete&&!repeated.processed&&!repeated.added&&
        cursor.index==refused_cursor.index&&cursor.slot==refused_cursor.slot&&
        cursor.phase==refused_cursor.phase&&cursor.link==refused_cursor.link,"repeated refusal preserves cursor");
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*owner.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"merge pressure trim");orbital::worker::poll(owner.lane,1024);
    }
    bool complete=false;
    while(!complete) {
        auto r=target.merge_from(source,cursor,combine);accept(r);complete=r.complete;
        require(clock_type::now()<deadline,"merge pressure recovery");orbital::worker::poll(owner.lane,1024);
    }
    require(processed==order.size()&&added==source_expected.size()-1,"resumed merge total deltas");
    require(calls==1,"downstream duplicate combined once across refusal");
}

template<unsigned A> void worker_campaign(unsigned n,unsigned reps,const std::array<unsigned,2>& cpus) {
    using local=table<40,A,A-4,128,128>;using roomy=table<40,A+1,A-3,128,128>;
    constexpr unsigned arms=HT_CONCURRENT_WORKERS?8:6;
    const char* histories[]={"distinct","repeated","skewed"};
    for(unsigned h=0;h<3;++h) {
        worker_input input(n,h?16:1,h==2,h==1?50:10);
        for(unsigned rep=0;rep<reps;++rep)for(unsigned j=0;j<arms;++j)switch((rep+j)%arms) {
            case 0:worker_trial<abseil<40>,true>(input,"abseil_partition",histories[h],rep,cpus);break;
            case 1:worker_trial<local,true>(input,"fuller_partition",histories[h],rep,cpus);break;
            case 2:worker_trial<abseil<40>,false>(input,"abseil_merge",histories[h],rep,cpus);break;
            case 3:
                if(h)worker_trial<roomy,false,true>(input,"fuller_merge",histories[h],rep,cpus);
                else worker_trial<local,false,true>(input,"fuller_merge",histories[h],rep,cpus);
                break;
            case 4:
                if(h)worker_trial<roomy,false>(input,"fuller_rebuild",histories[h],rep,cpus);
                else worker_trial<local,false>(input,"fuller_rebuild",histories[h],rep,cpus);
                break;
            case 5:worker_trial<roomy,false,true>(input,"fuller_roomy",histories[h],rep,cpus);break;
#if HT_CONCURRENT_WORKERS
            case 6:shared_worker_trial<concurrent_baselines::table<concurrent_baselines::backend::boost,40,
                concurrent_baselines::kind::map>>(input,"boost_shared",histories[h],rep,cpus);break;
            case 7:shared_worker_trial<concurrent_baselines::table<concurrent_baselines::backend::gtl,40,
                concurrent_baselines::kind::map>>(input,"gtl_shared",histories[h],rep,cpus);break;
#endif
        }
    }
}
} // namespace ht
int main(int argc,char** argv) {
    using namespace ht;
    auto mode=argc>1?std::string_view(argv[1]):"check";auto reps=argc>2?std::stoul(argv[2]):7;
    require(reps>0,"worker repetitions");auto cpus=worker_cpus();
    std::puts("arm,history,n,rows,rep,cpu0,cpu1,receiver,local0,local1,misroutes,routed,runtime_ns,setup_ns,intake_ns,finish_ns,retire_ns,read_ns,release_ns,drain_ns,before_requested,coexist_requested,retained_requested,coexist_capacity,retained_capacity,coexist_metadata,retained_metadata,routing_capacity,added,primary,secondary,chain,primary_bits,secondary_bits,primary_slots,secondary_slots,local0_ns,local1_ns,finish0_ns,finish1_ns");
    if(mode=="check") {merge_check();merge_refusal_check();worker_campaign<5>(200,1,cpus);}
    else if(mode=="hot")worker_campaign<7>(1800,reps,cpus);
    else {require(mode=="large","worker mode");worker_campaign<13>(110000,reps,cpus);}
}
