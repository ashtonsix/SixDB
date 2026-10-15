#include "map.h"
#include "baseline.h"
#include <loom/hardware.h>
#include <loom/ring.h>
#include <array>
#include <chrono>
#include <random>
#include <string_view>
#include <thread>
using namespace hashtable_spike;
using Clock=std::chrono::steady_clock;

// Both schedulers use these same complete stages and the same qualified hint
// windows. Secondary/chain fallback is complete but still synchronous; tier
// population is reported so that limitation is visible.
template<class Map> struct Job {
    const Map* map{};
    typename Map::primary_probe probe{};
    word key{},candidates{},sum{},found{};
    std::size_t remaining{};
};
struct Hints { loom::HintPolicy policy; loom::DemandClass demand; };

template<class Emit> [[gnu::always_inline]] void hint_window(const Hints& hints,const byte* begin,std::size_t bytes,Emit&& emit) {
    // Only admitted primary/row windows reach this helper. Map construction
    // proves both extents positive, and the stable read epoch preserves them.
    __builtin_assume(bytes>0);
    emit(begin);
    const auto line=hints.policy.profile.geometry.line_bytes;
    const auto first=reinterpret_cast<std::uintptr_t>(begin),last=first+bytes-1;
    // Every hinted address is inside this actual initialized window; no N+1
    // prefetch assumption or over-allocation is used to authorize a hint.
    for (auto next=(first & ~(std::uintptr_t(line)-1))+line;next<=last;next+=line) emit(reinterpret_cast<const byte*>(next));
}

template<bool Whole,class Map,class Emit>
[[gnu::always_inline]] void primary_window(const Map& map,const typename Map::primary_probe& probe,Emit&& emit) {
    if constexpr(Whole) map.primary_bucket_window(probe,std::forward<Emit>(emit));
    else map.primary_window(probe,std::forward<Emit>(emit));
}

template<class Map> struct Context {
    Hints hints;
};
// The Context pointer is carried explicitly; it belongs to the enclosing full
// operation and is never a callback-local object escaping through a tail call.
template<class Map> using Scheduled=loom::Ring<Job<Map>*,const Context<Map>*>;
template<bool Hint,class Map,loom::Hint Kind,bool Whole=false> LOOM_NATIVE_CC void payload(typename Scheduled<Map>::Cursor,typename Scheduled<Map>::Slot*,Job<Map>*,const Context<Map>*);

template<bool Hint,class Map,loom::Hint Kind>
LOOM_NATIVE_CC void metadata(typename Scheduled<Map>::Cursor ring,typename Scheduled<Map>::Slot* slot,Job<Map>* job,const Context<Map>* context) {
    job->candidates=job->map->match_primary(job->probe);
    if constexpr(Hint) job->map->candidate_windows(job->probe,job->candidates,[&](const byte* p,std::size_t n) __attribute__((always_inline)) {
        hint_window(context->hints,p,n,[&](const byte* q) __attribute__((always_inline)) {loom::prefetch<Kind>(q,context->hints.policy.profile.geometry.page_bytes,context->hints.policy.profile.geometry.line_bytes);});
    });
    slot->function=payload<Hint,Map,Kind>;
    [[clang::musttail]] return Scheduled<Map>::yield_unchanged(ring,slot,job,context);
}
template<bool Hint,class Map,loom::Hint Kind,bool Whole>
LOOM_NATIVE_CC void payload(typename Scheduled<Map>::Cursor ring,typename Scheduled<Map>::Slot* slot,Job<Map>* job,const Context<Map>* context) {
    if constexpr(Whole) job->candidates=job->map->match_primary(job->probe);
    word value=0; const bool found=job->map->finish_primary(job->probe,job->candidates,value);
    job->sum+=value; job->found+=word(found); job->key=value;
    if (!--job->remaining) {
        [[clang::musttail]] return Scheduled<Map>::finish(ring,slot,job,context);
    }
    else {
        job->probe=job->map->prepare_primary(job->key);
        if constexpr(Hint) primary_window<Whole>(*job->map,job->probe,[&](const byte* p,std::size_t n) __attribute__((always_inline)) {
            hint_window(context->hints,p,n,[&](const byte* q) __attribute__((always_inline)) {loom::prefetch<Kind>(q,context->hints.policy.profile.geometry.page_bytes,context->hints.policy.profile.geometry.line_bytes);});
        });
        if constexpr(!Whole) slot->function=metadata<Hint,Map,Kind>;
    }
    [[clang::musttail]] return Scheduled<Map>::yield_unchanged(ring,slot,job,context);
}

struct Result { word sum{},found{},terminal{}; bool operator==(const Result&) const=default; };
template<unsigned C,class Map> auto jobs(const Map& map,const std::vector<word>& keys) {
    std::array<Job<Map>,C> work{};
    for (unsigned j=0;j<C;++j) {
        auto& w=work[j]; w.map=&map;w.key=keys[j*(keys.size()/C)];w.remaining=keys.size()/C;w.probe=map.prepare_primary(w.key);
    }
    return work;
}
template<class Jobs> Result result(const Jobs& work) {
    Result r;
    for (const auto& w:work) { r.sum+=w.sum;r.found+=w.found;r.terminal^=w.key; }
    return r;
}
template<unsigned C,bool Hint,class Map,loom::Hint Kind=loom::Hint::none,bool Whole=false>
[[gnu::noinline]] Result scheduled(const Map& map,const std::vector<word>& keys,Scheduled<Map>& ring,const Context<Map>& context) {
    auto work=jobs<C>(map,keys);
    for (auto& w:work) {
        if constexpr(Hint) primary_window<Whole>(map,w.probe,[&](const byte* p,std::size_t n) __attribute__((always_inline)) {
            hint_window(context.hints,p,n,[&](const byte* q) __attribute__((always_inline)) {loom::prefetch<Kind>(q,context.hints.policy.profile.geometry.page_bytes,context.hints.policy.profile.geometry.line_bytes);});
        });
        constexpr auto stage=Whole?payload<Hint,Map,Kind,true>:metadata<Hint,Map,Kind>;
        require(ring.push(stage,{&w,&context})); // empty dependency: no hidden enqueue hint
    }
    while (ring.pending()) ring.spin({4096});
    return result(work);
}
template<unsigned C,bool Hint,class Map,loom::Hint Kind=loom::Hint::none,bool Whole=false>
[[gnu::noinline]] Result manual(const Map& map,const std::vector<word>& keys,const Context<Map>& context) {
    auto work=jobs<C>(map,keys);
    auto emit=[&](const byte* p,std::size_t n) __attribute__((always_inline)) {
        if constexpr(Hint) hint_window(context.hints,p,n,[&](const byte* q) __attribute__((always_inline)) {loom::prefetch<Kind>(q,context.hints.policy.profile.geometry.page_bytes,context.hints.policy.profile.geometry.line_bytes);});
    };
    for (auto& w:work) primary_window<Whole>(map,w.probe,emit);
    for (std::size_t i=0;i<keys.size()/C;++i) {
        if constexpr(!Whole) for (auto& w:work) { w.candidates=map.match_primary(w.probe);map.candidate_windows(w.probe,w.candidates,emit); }
        for (auto& w:work) {
            if constexpr(Whole) w.candidates=map.match_primary(w.probe);
            word value=0;w.found+=word(map.finish_primary(w.probe,w.candidates,value));w.sum+=value;w.key=value;
            if (i+1<keys.size()/C) {w.probe=map.prepare_primary(w.key);primary_window<Whole>(map,w.probe,emit);}
        }
    }
    return result(work);
}
template<unsigned C,bool Hint,class Map>
[[gnu::noinline]] Result complete(const Map& map,const std::vector<word>& keys) {
    struct Chain { word key{},sum{},found{}; };
    std::array<Chain,C> work{}; Result r;
    for (unsigned j=0;j<C;++j) {
        work[j].key=keys[j*(keys.size()/C)];
        if constexpr(Hint) map.prefetch(work[j].key);
    }
    for (std::size_t i=0;i<keys.size()/C;++i) for (auto& w:work) {
        word value=0;w.found+=word(map.get(w.key,value));w.sum+=value;w.key=value;
        if constexpr(Hint) if (i+1<keys.size()/C) map.prefetch(w.key);
    }
    for (const auto& w:work) {r.sum+=w.sum;r.found+=w.found;r.terminal^=w.key;}
    return r;
}

template<class F> double timed(std::size_t n,std::size_t rounds,F&& f,Result expected) {
    Result total;
    const auto start=Clock::now();
    for (std::size_t i=0;i<rounds;++i) {
        const auto r=f();
        asm volatile(""::"r"(r.sum),"r"(r.found),"r"(r.terminal):"memory");
        total.sum+=r.sum;total.found+=r.found;total.terminal^=r.terminal;
    }
    const auto end=Clock::now();
    require(total==Result{expected.sum*rounds,expected.found*rounds,rounds%2?expected.terminal:0});
    return std::chrono::duration<double,std::nano>(end-start).count()/double(n*rounds);
}

template<unsigned C,unsigned Slots> void run(loom::Worker& worker,std::size_t n,unsigned reps) {
    using Packed=basic_map<false,Slots==8?4:1>;
    using Direct=basic_map<true,Slots==8?4:1>;
    std::vector<word> keys(n);for (std::size_t i=0;i<n;++i) keys[i]=mix(i+1);
    std::mt19937_64 rng(81731);std::shuffle(keys.begin(),keys.end(),rng);
    Packed packed(*worker.memory,n,Slots,false,1,37913);
    Direct direct(*worker.memory,n,Slots,false,1,37913);
    abseil_map baseline;baseline.reserve(n);
    Result expected{0,n,0};
    for (std::size_t i=0;i<n;++i) {
        const word v=keys[(i+1)&(n-1)];expected.sum+=v;
        require(packed.insert_or_assign(keys[i],v)); require(direct.insert_or_assign(keys[i],v));require(baseline.insert_or_assign(keys[i],v));
    }
    for (unsigned j=0;j<C;++j) expected.terminal^=keys[((j+1)*(n/C))&(n-1)];
    packed.audit();direct.audit();
    auto demand=loom::bind_demand({loom::MemoryPattern::irregular,1},1);require(bool(demand));
    Scheduled<Packed> ring;require(bool(ring.configure(worker,{C,0})));
    Context<Packed> context{{ring.hint_policy(),*demand}};
    require(std::has_single_bit(context.hints.policy.profile.geometry.line_bytes));
    // Resolve short samples without dropping operation setup or Ring enqueue /
    // drain from the timing. Every arm executes exactly the same number of keys.
    const auto rounds=std::max<std::size_t>(1,262144/n);
    // A one-byte demand permits only a single target. Bind the snapshot's
    // concrete native body once for both schedulers, outside the timed loop.
    decltype(&manual<C,false,Packed>) manual_hints=nullptr;
    decltype(&scheduled<C,false,Packed>) ring_hints=nullptr;
    decltype(manual_hints) manual_whole_hints=nullptr;
    decltype(ring_hints) ring_whole_hints=nullptr;
    const auto bind=[&]<loom::Hint Kind>() {
        manual_hints=&manual<C,Kind!=loom::Hint::none,Packed,Kind>;
        ring_hints=&scheduled<C,Kind!=loom::Hint::none,Packed,Kind>;
        manual_whole_hints=&manual<C,Kind!=loom::Hint::none,Packed,Kind,true>;
        ring_whole_hints=&scheduled<C,Kind!=loom::Hint::none,Packed,Kind,true>;
    };
    switch(context.hints.policy.codes[demand->code()]) {
    case loom::Hint::none:bind.template operator()<loom::Hint::none>();break;
    case loom::Hint::l1_1:bind.template operator()<loom::Hint::l1_1>();break;
    case loom::Hint::l2_1:bind.template operator()<loom::Hint::l2_1>();break;
    case loom::Hint::l3_1:bind.template operator()<loom::Hint::l3_1>();break;
    default:require(false);
    }
    std::fprintf(stderr,"policy_revision=%llu\npressure=%u\nline_bytes=%zu\npage_bytes=%zu\ndemand_code=%u\nselected_hint=%u\n",
        static_cast<unsigned long long>(context.hints.policy.profile.revision),unsigned(context.hints.policy.pressure),
        context.hints.policy.profile.geometry.line_bytes,context.hints.policy.profile.geometry.page_bytes,
        demand->code(),unsigned(context.hints.policy.codes[demand->code()]));

    const auto pm=packed.memory(),dm=direct.memory();const auto counts=packed.counts();
    for (unsigned rep=0;rep<reps;++rep) for (unsigned k=0;k<12;++k) {
        const unsigned variant=rep%2?11-k:k;
        auto operation=[&]()->Result {
            switch (variant) {
            case 0:return complete<C,false>(baseline,keys);
            case 1:return complete<C,true>(baseline,keys);
            case 2:return complete<C,false>(packed,keys);
            case 3:return complete<C,false>(direct,keys);
            case 4:return manual<C,false>(packed,keys,context);
            case 5:return manual_hints(packed,keys,context);
            case 6:return scheduled<C,false>(packed,keys,ring,context);
            case 7:return ring_hints(packed,keys,ring,context);
            case 8:return manual<C,false,Packed,loom::Hint::none,true>(packed,keys,context);
            case 9:return manual_whole_hints(packed,keys,context);
            case 10:return scheduled<C,false,Packed,loom::Hint::none,true>(packed,keys,ring,context);
            default:return ring_whole_hints(packed,keys,ring,context);
            }
        };
        require(operation()==expected); // matching full-workload warmup for every arm
        const auto before=ring.counters().visits;
        const double ns=timed(n,rounds,operation,expected);
        const auto visits=ring.counters().visits-before;
        require(visits==((variant==6 || variant==7)?2*n*rounds:variant>=10?n*rounds:0));
        const char* names[]{"abseil","abseil-prefetch","packed","direct","manual","manual-hints","ring","ring-hints","manual-whole","manual-whole-hints","ring-whole","ring-whole-hints"};
        std::printf("%zu,%u,%u,%s,%.6f,%llu,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%u,%zu,%u,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",n,C,rep,names[variant],ns,
            static_cast<unsigned long long>(expected.sum),static_cast<unsigned long long>(visits),pm.payload,dm.payload,
            baseline.allocation_stats().current_bytes,keys.size()*sizeof(word),ring.storage_bytes(),counts.primary,counts.secondary,counts.overflow,
            unsigned(context.hints.policy.codes[demand->code()]),rounds,Slots,
            pm.capacity,dm.capacity,pm.plans+pm.catalog,dm.plans+dm.catalog,
            abseil_map::ordinary_object_bytes,variant>=4?C*sizeof(Job<Packed>):C*3*sizeof(word),sizeof(context));
        std::fflush(stdout);
    }
    require(bool(ring.close()));
}
int main(int argc,char** argv) {
    require(argc==4 || argc==5);const auto n=std::stoull(argv[1]);const unsigned c=std::stoul(argv[2]),reps=std::stoul(argv[3]);
    const unsigned slots=argc==5?std::stoul(argv[4]):16;require(slots==8 || slots==16);
    require(n>=128 && std::has_single_bit(n) && (c==1 || c==8 || c==32) && reps>0);
    std::shared_ptr<loom::HardwareProfile> profile;
    { // No background calibration or surviving service thread during timing.
        auto context=loom::discover_hardware();require(bool(context));
        loom::HardwareOptions options;options.calibrate=false;loom::HardwareService service(options);
        auto p=service.request(std::move(*context));require(bool(p));profile=std::move(*p);
    }
    const auto inspection=profile->inspect();
    std::fprintf(stderr,"hardware_context=%s\ncalibration_enabled=false\nstate=%u\n",profile->context().signature.c_str(),unsigned(inspection.snapshot.state));
    if (inspection.observations) {
        const auto& o=*inspection.observations;
        std::fprintf(stderr,"width_origin=%u\nwidth_qualification=%u\ntraining_origin=%u\ntraining_qualification=%u\nretained_source=%s\n",
            unsigned(o.width.origin),unsigned(o.width.qualification),unsigned(o.training.origin),unsigned(o.training.qualification),o.retained_source_sha256.c_str());
    }
    loom::Worker worker;require(bool(loom::adopt(worker)));
    auto arena=oa::open_arena(4ULL<<30);require(bool(arena));require(bool(loom::configure(worker,std::move(*arena),0,profile)));
    require(bool(loom::set_hint_pressure(worker,loom::HintPressure::low)));
    std::puts("n,chains,rep,execution,ns_per_lookup,checksum,ring_visits,packed_requested_bytes,direct_requested_bytes,abseil_requested_bytes,key_input_bytes,ring_storage_bytes,primary_keys,secondary_keys,overflow_keys,hint_code,rounds,slots,packed_backing_capacity,direct_backing_capacity,packed_control_bytes,direct_control_bytes,abseil_control_bytes,continuation_state_bytes,context_bytes");
    auto execute=[&]<unsigned S>() {if (c==1) run<1,S>(worker,n,reps);else if (c==8) run<8,S>(worker,n,reps);else run<32,S>(worker,n,reps);};
    if (slots==8) execute.template operator()<8>();else execute.template operator()<16>();
    const auto deadline=Clock::now()+std::chrono::seconds(10);
    for (;;) {
        auto p=loom::shutdown(worker);if (p.kind()==orbital::Progress::Kind::done) break;
        require(p.kind()!=orbital::Progress::Kind::failed && Clock::now()<deadline);loom::poll(worker,{128,0});std::this_thread::yield();
    }
    loom::relinquish(worker);
}
