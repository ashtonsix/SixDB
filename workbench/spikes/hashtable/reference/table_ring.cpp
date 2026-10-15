#include "table-plans.h"
#include "table.h"
#include <absl/container/flat_hash_map.h>
#include <absl/container/internal/hashtable_debug.h>
#include <loom/hardware.h>
#include <loom/ring.h>
#include <chrono>
#include <random>
#include <string_view>
#include <thread>

using namespace hashtable_spike;
namespace t=hashtable_spike::tiered;
using Clock=std::chrono::steady_clock;
namespace {
constexpr word missing_bit=word{1}<<39;
struct answer {word sum{},found{},terminal{};bool operator==(const answer&)const=default;};
struct start {word key{};std::size_t count{};};
struct workload {
    std::vector<word> keys;
    bool mixed{};
    workload(std::size_t n,unsigned generation,word seed,bool misses):keys(n),mixed(misses) {
        permutation<40,false> source(seed);
        for(std::size_t i=0;i<n;++i) {
            // Cycle walking restricts a permutation to the low half-domain.
            // The high bit then supplies an absent mate without a timed index.
            word key=generation*n+i;do{key=source(key);}while(key&missing_bit);
            keys[i]=key;
        }
        std::mt19937_64 rng(seed+generation);std::shuffle(keys.begin(),keys.end(),rng);
    }
    word request(std::size_t i)const {
        const auto key=keys[i%keys.size()];
        return key^((mixed&&(mix(key)&1))?missing_bit:0);
    }
    word value(std::size_t i)const{return request(i+1);}
    template<unsigned C>auto starts()const {
        std::array<start,C> out{};
        for(unsigned j=0;j<C;++j) {
            const auto first=j*keys.size()/C,last=(j+1)*keys.size()/C;
            out[j]={request(first),last-first};
            for(auto i=first;i<last;++i)out[j].count+=word(request(i)!=keys[i]);
        }
        return out;
    }
    template<unsigned C>answer expected()const {
        answer out{0,keys.size(),0};
        for(std::size_t i=0;i<keys.size();++i)out.sum+=value(i);
        for(unsigned j=0;j<C;++j)out.terminal^=request((j+1)*keys.size()/C);
        return out;
    }
};
struct baseline {
    absl::flat_hash_map<word,word> body;
    [[gnu::always_inline]] bool get(word key,word& out)const {
        const auto i=body.find(key);if(i==body.end())return false;out=i->second;return true;
    }
    [[gnu::always_inline]] void prefetch(word key)const{body.prefetch(key);}
};
struct lane {word key{},sum{},found{};std::size_t remaining{};};
template<class Map>struct job:lane {typename Map::read_probe probe;};
template<class Jobs>answer result(const Jobs& work) {
    answer out;for(const auto& j:work){out.sum+=j.sum;out.found+=j.found;out.terminal^=j.key;}return out;
}
template<class L>[[gnu::always_inline]] void consume(L& j,bool found,word value) {
    j.sum+=value;j.found+=word(found);j.key=found?value:(j.key^missing_bit);--j.remaining;
}
template<class Emit>[[gnu::always_inline]] void window(const byte* p,std::size_t bytes,std::size_t line,Emit&& emit) {
    require(bytes>0);emit(p);
    const auto first=reinterpret_cast<std::uintptr_t>(p),last=first+bytes-1;
    for(auto next=(first&~(line-1))+line;next<=last;next+=line)emit(reinterpret_cast<const byte*>(next));
}
template<class Map>struct context {
    const Map* map;
    loom::HintPolicy policy;
};
template<loom::Hint Kind,class Map>[[gnu::always_inline]] auto emitter(const context<Map>& c) {
    return [&c](const byte* p,std::size_t bytes) __attribute__((always_inline)) {
        (void)c;
        if constexpr(Kind!=loom::Hint::none)
            window(p,bytes,c.policy.profile.geometry.line_bytes,[&](const byte* q) __attribute__((always_inline)) {
                loom::prefetch<Kind>(q,c.policy.profile.geometry.page_bytes,c.policy.profile.geometry.line_bytes);
            });
    };
}
template<class Map>using scheduled=typename loom::Ring<job<Map>*>::template WithShared<const context<Map>*>;
template<loom::Hint Kind,class Map>
[[gnu::noinline]] LOOM_NATIVE_CC void visit_body(typename scheduled<Map>::Cursor ring,typename scheduled<Map>::Slot* slot,const context<Map>* c,job<Map>* j) {
    // The generic StreamPack resolution can contain ordinary calls. Keeping
    // those in this same-visit continuation avoids a frame on primary reads.
    __builtin_assume(j->probe.stage==Map::read_stage::chain_body);
    auto emit=emitter<Kind>(*c);word value=0;bool found=false;
    c->map->advance_read(j->probe,value,found,emit);
    [[clang::musttail]] return scheduled<Map>::yield_unchanged(ring,slot,c,j);
}
template<loom::Hint Kind,class Map>
LOOM_NATIVE_CC void visit(typename scheduled<Map>::Cursor ring,typename scheduled<Map>::Slot* slot,const context<Map>* c,job<Map>* j) {
    if(j->probe.stage==Map::read_stage::chain_body)
        [[clang::musttail]] return visit_body<Kind,Map>(ring,slot,c,j);
    auto emit=emitter<Kind>(*c);word value=0;bool found=false;
    if(c->map->template advance_read<false>(j->probe,value,found,emit)) {
        consume(*j,found,value);
        if(!j->remaining)[[clang::musttail]] return scheduled<Map>::finish(ring,slot,c,j);
        c->map->prepare_read(j->probe,j->key,emit);
    }
    [[clang::musttail]] return scheduled<Map>::yield_unchanged(ring,slot,c,j);
}
template<unsigned C,class Map,bool Prefetch=false>[[gnu::noinline]] answer fused(const Map& map,const std::array<start,C>& initial) {
    std::array<lane,C> work{};
    for(unsigned i=0;i<C;++i){work[i].key=initial[i].key;work[i].remaining=initial[i].count;if constexpr(Prefetch)map.prefetch(work[i].key);}
    unsigned active=C;
    while(active)for(auto& j:work)if(j.remaining) {
        word value=0;const bool found=map.get(j.key,value);consume(j,found,value);
        if(!j.remaining)--active;else if constexpr(Prefetch)map.prefetch(j.key);
    }
    return result(work);
}
template<unsigned C,loom::Hint Kind,class Map>[[gnu::noinline]] answer manual(const context<Map>& c,const std::array<start,C>& initial) {
    std::array<job<Map>,C> work{};auto emit=emitter<Kind>(c);
    for(unsigned i=0;i<C;++i){auto& j=work[i];j.key=initial[i].key;j.remaining=initial[i].count;c.map->prepare_read(j.probe,j.key,emit);}
    unsigned active=C;
    while(active)for(auto& j:work)if(j.remaining) {
        word value=0;bool found=false;
        if(c.map->advance_read(j.probe,value,found,emit)) {
            consume(j,found,value);if(!j.remaining)--active;else c.map->prepare_read(j.probe,j.key,emit);
        }
    }
    return result(work);
}
template<unsigned C,loom::Hint Kind,class Map>[[gnu::noinline]] answer ring_run(const context<Map>& c,const std::array<start,C>& initial,scheduled<Map>& ring) {
    std::array<job<Map>,C> work{};auto emit=emitter<Kind>(c);
    for(unsigned i=0;i<C;++i) {
        auto& j=work[i];j.key=initial[i].key;j.remaining=initial[i].count;c.map->prepare_read(j.probe,j.key,emit);
        require(ring.push(visit<Kind,Map>,{&j}));
    }
    while(ring.pending())ring.spin({4096});
    return result(work);
}
struct diagnostic {std::array<std::size_t,5> stages{};std::size_t lines{},requests{};};
template<class Map>diagnostic inspect(const Map& map,const workload& input,std::size_t line) {
    diagnostic d;
    // Separate untimed pass: no logical counters in native callbacks.
    for(std::size_t i=0;i<input.keys.size();++i)for(unsigned m=0;m<1+unsigned(input.request(i)!=input.keys[i]);++m) {
        const auto key=input.keys[i]^(m?missing_bit:0);
        auto emit=[&](const byte* p,std::size_t n){window(p,n,line,[&](const byte*){++d.lines;});};
        typename Map::read_probe probe{};map.prepare_read(probe,key,emit);word value=9173;bool found=false;
        for(;;){++d.stages[unsigned(probe.stage)];if(map.advance_read(probe,value,found,emit))break;}
        require(found==(m==0));require(value==(m==0?input.value(i):9173));++d.requests;
    }
    return d;
}
template<class F>double timed(std::size_t requests,unsigned rounds,F&& f,answer expected) {
    const auto begin=Clock::now();answer total;
    for(unsigned i=0;i<rounds;++i){const auto r=f();asm volatile(""::"r"(r.sum),"r"(r.found),"r"(r.terminal):"memory");total.sum+=r.sum;total.found+=r.found;total.terminal^=r.terminal;}
    const auto end=Clock::now();require(total==answer{expected.sum*rounds,expected.found*rounds,rounds%2?expected.terminal:0});
    return std::chrono::duration<double,std::nano>(end-begin).count()/(requests*rounds);
}
template<unsigned C,class Map>void compare(loom::Worker& worker,const Map& map,const baseline& absl,const workload& input,unsigned repetitions,unsigned generation,word seed) {
    const auto initial=input.template starts<C>();const auto expected=input.template expected<C>();
    scheduled<Map> ring;context<Map> c{&map,{}};
    require(bool(ring.configure(worker,{C,0},{&c})));c.policy=ring.hint_policy();
    const auto line=c.policy.profile.geometry.line_bytes;require(std::has_single_bit(line));
    const auto d=inspect(map,input,line);std::size_t visits=0;for(auto n:d.stages)visits+=n;
    require(fused<C>(map,initial)==expected&&fused<C>(absl,initial)==expected);
    auto demand=loom::bind_demand({loom::MemoryPattern::irregular,1},1);require(bool(demand));
    const auto hint=c.policy.codes[demand->code()];
    std::fprintf(stderr,"policy_revision=%llu\nline_bytes=%zu\npage_bytes=%zu\nhint_code=%u\n",
        (unsigned long long)c.policy.profile.revision,line,c.policy.profile.geometry.page_bytes,unsigned(hint));
    auto mh=&manual<C,loom::Hint::none,Map>;auto rh=&ring_run<C,loom::Hint::none,Map>;
    auto bind=[&]<loom::Hint H>(){mh=&manual<C,H,Map>;rh=&ring_run<C,H,Map>;};
    switch(hint){case loom::Hint::none:bind.template operator()<loom::Hint::none>();break;case loom::Hint::l1_1:bind.template operator()<loom::Hint::l1_1>();break;case loom::Hint::l2_1:bind.template operator()<loom::Hint::l2_1>();break;case loom::Hint::l3_1:bind.template operator()<loom::Hint::l3_1>();break;default:require(false);}
    std::size_t payload=0,backing=0,metadata=sizeof(map),preparation=0;
    map.allocations([&](const auto& a){payload+=a.size;backing+=oa::capacity(a);});
    map.metadata([&](const void*,std::size_t bytes){metadata+=bytes;});
    map.preparation([&](const void*,std::size_t bytes){preparation+=bytes;});
    const auto population=map.counts();
    const auto absl_bytes=absl::container_internal::AllocatedByteSize(absl.body);
    const auto rounds=unsigned(std::max<std::size_t>(1,(131072+d.requests-1)/d.requests));
    const char* names[]={"abseil","abseil_prefetch","fused","manual","manual_hints","ring","ring_hints"};
    for(unsigned rep=0;rep<repetitions;++rep)for(unsigned order=0;order<7;++order) {
        const unsigned arm=(order+rep)%7;
        auto execute=[&]() {
            switch(arm){case 0:return fused<C>(absl,initial);case 1:return fused<C,baseline,true>(absl,initial);case 2:return fused<C>(map,initial);case 3:return manual<C,loom::Hint::none>(c,initial);case 4:return mh(c,initial);case 5:return ring_run<C,loom::Hint::none>(c,initial,ring);default:return rh(c,initial,ring);}
        };
        require(execute()==expected); // Identical full-trace warmup per arm.
        const auto before=ring.counters().visits;const auto ns=timed(d.requests,rounds,execute,expected);
        const auto actual=ring.counters().visits-before;require(actual==(arm>=5?visits*rounds:0));
        std::printf("%zu,%u,%u,%llu,%u,%u,%s,%s,%.6f,%zu,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%u,%u,%u,%u,%u\n",
            input.keys.size(),C,generation,(unsigned long long)seed,rep,rounds,input.mixed?"mixed":"hit",names[arm],ns,d.requests,
            (unsigned long long)expected.sum,payload,backing,metadata,preparation,absl_bytes,input.keys.size()*sizeof(word),arm>=5?ring.storage_bytes():0,C*(arm>=3?sizeof(job<Map>):sizeof(lane)),
            population.primary,population.secondary,population.chain,d.stages[0],d.stages[1],d.stages[2],d.stages[3],d.stages[4],d.lines,unsigned(hint),
            1u<<Map::primary_plan.address_bits,Map::primary_plan.bytes,1u<<Map::secondary_plan.address_bits,Map::secondary_plan.bytes);
        std::fflush(stdout);
    }
    require(bool(ring.close()));
}
template<t::bucket_plan P,t::bucket_plan Q>void run(loom::Worker& worker,std::size_t n,unsigned turns,bool mixed,unsigned concurrency,unsigned reps,word seed) {
    using Map=t::table<P,Q,t::sharing::flat,true>;
    auto made=Map::make(*worker.memory,seed,mix(seed^0x9e3779b97f4a7c15ULL));require(bool(made));auto map=std::move(*made);
    baseline absl;absl.body.reserve(n);workload input(n,0,87191,mixed);
    auto put=[&](word k,word v){const auto r=map.set(k,v);require(bool(r)&&r->distinct_delta==1);require(absl.body.insert_or_assign(k,v).second);};
    for(std::size_t i=0;i<n;++i)put(input.keys[i],input.value(i));
    for(unsigned generation=1;generation<=turns;++generation) {
        workload next(n,generation,87191,mixed);
        for(std::size_t i=0;i<n;++i){const auto r=map.erase(input.keys[i]);require(bool(r)&&r->distinct_delta==-1);require(absl.body.erase(input.keys[i])==1);put(next.keys[i],next.value(i));}
        input=std::move(next);
    }
    require(map.size()==n&&absl.body.size()==n);map.audit();
    if(concurrency==1)compare<1>(worker,map,absl,input,reps,turns,seed);
    else if(concurrency==8)compare<8>(worker,map,absl,input,reps,turns,seed);
    else compare<32>(worker,map,absl,input,reps,turns,seed);
}
} // namespace
int main(int argc,char** argv) {
    require(argc==8);const unsigned a=std::stoul(argv[1]);const auto n=std::stoull(argv[2]);
    const unsigned turns=std::stoul(argv[3]),mixed=std::stoul(argv[4]),c=std::stoul(argv[5]),reps=std::stoul(argv[6]);const word seed=std::stoull(argv[7]);
    require(n>=32&&(c==1||c==8||c==32)&&mixed<=1&&reps>0&&n*(turns+1)<missing_bit);
    std::shared_ptr<loom::HardwareProfile> profile;
    {auto hardware=loom::discover_hardware();require(bool(hardware));loom::HardwareOptions options;options.calibrate=false;loom::HardwareService service(options);auto p=service.request(std::move(*hardware));require(bool(p));profile=std::move(*p);}
    std::fprintf(stderr,"hardware_context=%s\ncalibration_enabled=false\n",profile->context().signature.c_str());
    const auto inspection=profile->inspect();
    if(inspection.observations){const auto& o=*inspection.observations;std::fprintf(stderr,"width_origin=%u\nwidth_qualification=%u\ntraining_origin=%u\ntraining_qualification=%u\nretained_source=%s\n",unsigned(o.width.origin),unsigned(o.width.qualification),unsigned(o.training.origin),unsigned(o.training.qualification),o.retained_source_sha256.c_str());}
    loom::Worker worker;require(bool(loom::adopt(worker)));auto arena=oa::open_arena(4ULL<<30);require(bool(arena));
    require(bool(loom::configure(worker,std::move(*arena),0,profile)));require(bool(loom::set_hint_pressure(worker,loom::HintPressure::low)));
    std::puts("entries,concurrency,turnovers,seed,repetition,rounds,queries,execution,ns_per_lookup,requests,checksum,payload_bytes,backing_bytes,known_metadata_bytes,shared_preparation_bytes,abseil_payload_bytes,untimed_key_input_bytes,ring_storage_bytes,job_state_bytes,primary_entries,secondary_entries,chain_entries,primary_visits,secondary_visits,chain_directory_visits,chain_body_visits,chain_payload_visits,staged_hint_line_targets,hint_code,primary_buckets,primary_stride,secondary_buckets,secondary_stride");
    using namespace t;
#if defined(HASHTABLE_WORD_KEYS)
    require(a==8);
    run<k40v64_a8_n16_primary_key_words_byte,
        searching(k40v64_a5_n16_secondary_key_planes_byte,key_search::exact_raw)>(worker,n,turns,mixed,c,reps,seed);
#elif defined(HASHTABLE_EXACT_KEYS)
    switch(a) {
    case 5:run<searching(k40v64_a5_n16_primary_key_planes_byte,key_search::exact_raw),
               searching(k40v64_a2_n16_secondary_key_planes_byte,key_search::exact_raw)>(worker,n,turns,mixed,c,reps,seed);break;
    case 8:run<searching(k40v64_a8_n16_primary_key_planes_byte,key_search::exact_raw),
               searching(k40v64_a5_n16_secondary_key_planes_byte,key_search::exact_raw)>(worker,n,turns,mixed,c,reps,seed);break;
    case 20:run<searching(k40v64_a20_n16_primary_key_planes_byte,key_search::exact_raw),
                searching(k40v64_a17_n16_secondary_key_planes_byte,key_search::exact_raw)>(worker,n,turns,mixed,c,reps,seed);break;
    default:require(false);
    }
#else
    switch(a) {
    case 5:run<k40v64_a5_n16_primary_rows_byte,k40v64_a2_n16_secondary_rows_byte>(worker,n,turns,mixed,c,reps,seed);break;
    case 8:run<k40v64_a8_n16_primary_rows_byte,k40v64_a5_n16_secondary_rows_byte>(worker,n,turns,mixed,c,reps,seed);break;
    case 12:run<k40v64_a12_n16_primary_rows_byte,k40v64_a9_n16_secondary_rows_byte>(worker,n,turns,mixed,c,reps,seed);break;
    case 16:run<k40v64_a16_n16_primary_rows_byte,k40v64_a13_n16_secondary_rows_byte>(worker,n,turns,mixed,c,reps,seed);break;
    case 20:run<k40v64_a20_n16_primary_rows_byte,k40v64_a17_n16_secondary_rows_byte>(worker,n,turns,mixed,c,reps,seed);break;
    default:require(false);
    }
#endif
    const auto deadline=Clock::now()+std::chrono::seconds(10);
    for(;;){auto p=loom::shutdown(worker);if(p.kind()==orbital::Progress::Kind::done)break;require(p.kind()!=orbital::Progress::Kind::failed&&Clock::now()<deadline);loom::poll(worker,{128,0});std::this_thread::yield();}
    loom::relinquish(worker);
}
