#include "baseline.h"
#include "support.h"

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

using namespace hashtable_spike;

namespace {
struct sample { double ns{}; word checksum{}; };
template<class F> sample timed(std::size_t operations,F&& f) {
    const auto begin=clock_type::now();
    word sum=f();
    asm volatile("" : "+r"(sum) :: "memory");
    const auto end=clock_type::now();
    return {std::chrono::duration<double,std::nano>(end-begin).count()/double(operations),sum};
}

// Disjoint generations, not alternating the same two key populations. No
// modulo/division in the operation loops, so arbitrary n has the same driver
// cost as a power of two. Values form one full-width dependent lookup cycle.
struct trace {
    std::vector<word> keys,values;
    trace(std::size_t n,unsigned generation,word seed) : keys(n),values(n) {
        const word base=word(generation)*n;
        for(std::size_t i=0;i<n;++i) keys[i]=mix(base+i+1);
        std::mt19937_64 random(seed+generation);
        std::shuffle(keys.begin(),keys.end(),random);
        for(std::size_t i=0;i<n;++i) values[i]=keys[i+1==n?0:i+1];
    }
};

template<class Map> word fill(Map& map,const trace& data) {
    word sum=0;
    for(std::size_t i=0;i<data.keys.size();++i)
        sum+=map.insert_or_assign(data.keys[i],data.values[i]);
    return sum;
}
template<class Map> word churn(Map& map,const trace& before,const trace& after) {
    word sum=0;
    for(std::size_t i=0;i<before.keys.size();++i) {
        sum+=map.erase(before.keys[i]);
        sum+=map.insert_or_assign(after.keys[i],after.values[i]);
    }
    return sum;
}
template<class Map> word independent(const Map& map,const trace& data,unsigned rounds) {
    word sum=0;
    for(unsigned r=0;r<rounds;++r) for(word key:data.keys) {
        word value=0;const bool found=map.get(key,value);sum+=value+word(found);
    }
    return sum;
}
template<class Map> word serial(const Map& map,const trace& data,unsigned rounds) {
    word key=data.keys[0],sum=0;
    for(std::size_t i=0;i<data.keys.size()*rounds;++i) {
        word value=0;const bool found=map.get(key,value);
        sum+=value+word(found);key=value;
        asm volatile("" : "+r"(key) : "r"(found));
    }
    return sum^key;
}
template<class Map> void verify(const Map& map,const trace& data) {
    require(map.size()==data.keys.size());
    for(std::size_t i=0;i<data.keys.size();++i) {
        word value=0;require(map.get(data.keys[i],value) && value==data.values[i]);
    }
}
template<class Map> void absent(const Map& map,const trace& data) {
    for(word key:data.keys) { word value=97;require(!map.get(key,value) && value==97); }
}

// These are the already-measured memory-saving and low-spill controls, not new
// layout choices. Both still use the prototype's fixed B/16 secondary table.
template<bool Direct> using candidate=basic_map<Direct,1>;

template<bool Direct> void run(std::size_t n,unsigned slots,unsigned turnovers,
                               unsigned repetition,bool output=true) {
    pool_owner owner;
    std::unique_ptr<candidate<Direct>> p;
    std::unique_ptr<abseil_map> a;
    trace current(n,0,9381+repetition);
    const unsigned rounds=unsigned(std::max<std::size_t>(1,(262144+n-1)/n));
    const word initial_seed=mix(repetition+773);
    sample ps{},as{};
    auto build_p=[&] {
        p=std::make_unique<candidate<Direct>>(*owner.pool,n,slots,false,1,initial_seed);
        return fill(*p,current);
    };
    auto build_a=[&] {
        a=std::make_unique<abseil_map>();a->reserve(n);return fill(*a,current);
    };
    if(repetition%2) { as=timed(n,build_a);ps=timed(n,build_p); }
    else { ps=timed(n,build_p);as=timed(n,build_a); }
    require(ps.checksum==n && ps.checksum==as.checksum);

    auto emit=[&](const char* operation,unsigned generation,sample pp,sample aa,
                  std::size_t overlap_payload=0,std::size_t overlap_capacity=0,
                  std::size_t baseline_overlap=0) {
        if(!output) return;
        const auto memory=p->memory();const auto counts=p->counts();
        const auto& baseline=a->allocation_stats();
        for(unsigned arm=0;arm<2;++arm) {
            const auto s=arm?aa:pp;
            std::printf("%zu,%u,%u,%u,%s,%s,%.6f,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",
                n,slots,repetition,generation,arm?"abseil":Direct?"direct":"packed",operation,s.ns,
                static_cast<unsigned long long>(s.checksum),p->size(),
                arm?a->capacity():(std::size_t{1}<<p->primary_bits())*slots,
                arm?baseline.current_bytes:memory.payload,arm?0:memory.capacity,
                arm?abseil_map::ordinary_object_bytes:memory.catalog+memory.plans,
                arm?0:counts.primary,arm?0:counts.secondary,arm?0:counts.overflow,
                arm?baseline_overlap:overlap_payload,arm?0:overlap_capacity,
                arm?baseline.peak_bytes:0,arm?0:std::size_t(memory.pool.mapped_bytes),
                arm?0:std::size_t(memory.pool.metadata_bytes),current.keys.size()*2*sizeof(word));
        }
        std::fflush(stdout);
    };
    emit("build",0,ps,as);
    auto verify_current=[&] { verify(*p,current);verify(*a,current);p->audit(); };
    auto read=[&](unsigned generation) {
        auto paired=[&](const char* name,auto&& operation) {
            auto one=[&](auto& map) {
                const word warm=operation(map);asm volatile(""::"r"(warm):"memory");
                return timed(current.keys.size()*rounds,[&]{return operation(map);});
            };
            if((repetition+generation)%2) { as=one(*a);ps=one(*p); }
            else { ps=one(*p);as=one(*a); }
            require(ps.checksum==as.checksum);emit(name,generation,ps,as);
        };
        paired("independent_hit",[&](auto& map){return independent(map,current,rounds);});
        paired("serial_hit",[&](auto& map){return serial(map,current,rounds);});
        const trace missing(current.keys.size(),generation+turnovers+3,9381+repetition);
        paired("independent_miss",[&](auto& map){return independent(map,missing,rounds);});
    };
    verify_current();if(output) read(0);
    for(unsigned generation=1;generation<=turnovers;++generation) {
        trace next(n,generation,9381+repetition);
        auto pp=[&]{return churn(*p,current,next);};
        auto aa=[&]{return churn(*a,current,next);};
        if((repetition+generation)%2) { as=timed(n,aa);ps=timed(n,pp); }
        else { ps=timed(n,pp);as=timed(n,aa); }
        require(ps.checksum==2*n && ps.checksum==as.checksum);
        absent(*p,current);absent(*a,current);current=std::move(next);
        verify_current();emit("erase_insert_pair",generation,ps,as);
        if(output) read(generation);
    }

    // Explicit maintenance control. Rebuild both live tables into a fresh table
    // of the same requested size, and then into twice the reserved size. Charge
    // construction/reinsertion and retirement separately, including inverse
    // quotienting in the candidate. A same-size rebuild is not a policy.
    // The overlap is observed while old and new tables coexist; it is NOT a
    // measured transient allocation/RSS peak, and includes no MVCC versions.
    for(unsigned growth=0;growth<2;++growth) {
        const std::size_t reserve=n*(growth?2:1);
        const word seed=mix(initial_seed+turnovers+growth+1);
        std::unique_ptr<candidate<Direct>> next_p;
        std::unique_ptr<abseil_map> next_a;
        auto pp=[&] {
            next_p=std::make_unique<candidate<Direct>>(*owner.pool,reserve,slots,false,1,seed);
            p->each([&](word k,word v){require(next_p->insert_or_assign(k,v));});
            return word(next_p->size());
        };
        auto aa=[&] {
            next_a=std::make_unique<abseil_map>();next_a->reserve(reserve);
            // Baseline iteration comes from its live map, not an external key
            // trace that would give rebuilding more locality than the candidate.
            a->each([&](word k,word v){require(next_a->insert_or_assign(k,v));});
            return word(next_a->size());
        };
        if((repetition+growth)%2) { as=timed(n,aa);ps=timed(n,pp); }
        else { ps=timed(n,pp);as=timed(n,aa); }
        require(ps.checksum==n && ps.checksum==as.checksum);
        // Accounting is outside both timed intervals. Report the observed
        // coexistence, not a sampled or modeled RSS peak.
        const auto old=p->memory(),fresh=next_p->memory();
        const auto overlap_payload=old.payload+fresh.payload;
        const auto overlap_capacity=old.capacity+fresh.capacity;
        const auto baseline_overlap=a->allocation_stats().current_bytes+next_a->allocation_stats().current_bytes;
        sample retire_p{},retire_a{};
        auto drop_p=[&]{p=std::move(next_p);return word(p->size());};
        auto drop_a=[&]{a=std::move(next_a);return word(a->size());};
        if((repetition+growth)%2) {retire_a=timed(n,drop_a);retire_p=timed(n,drop_p);}
        else {retire_p=timed(n,drop_p);retire_a=timed(n,drop_a);}
        verify_current();
        emit(growth?"reseed_reserve_double_build":"reseed_same_size_build",turnovers,ps,as,overlap_payload,overlap_capacity,baseline_overlap);
        emit(growth?"reseed_reserve_double_retire":"reseed_same_size_retire",turnovers,retire_p,retire_a);
        if(output) read(turnovers+growth+1);
    }
}

void check() {
    for(unsigned slots:{10u,16u}) for(std::size_t n:{1u,31u,51u,52u,81u,82u,127u,205u}) {
        run<false>(n,slots,4,0,false);run<true>(n,slots,4,1,false);
    }
    // An independent oracle checks mutations; the benchmark trace is not its
    // own expected result. Includes duplicate replacement and boundary values.
    pool_owner pool;candidate<false> p(*pool.pool,51,10,false,1,727);
    std::unordered_map<word,word> expected;std::mt19937_64 random(87331);
    for(unsigned i=0;i<10000;++i) {
        const word key=mix(random()%257),value=i%3?random():~word{};
        if(i%4) {
            const bool fresh=!expected.contains(key);expected[key]=value;
            require(p.insert_or_assign(key,value)==fresh);
        } else require(p.erase(key)==(expected.erase(key)!=0));
        if(i%257==0) {
            p.audit();require(p.size()==expected.size());
            for(const auto& [k,v]:expected) {word got=0;require(p.get(k,got) && got==v);}
        }
    }
    std::puts("lifecycle checks passed: non-power cardinalities, disjoint turnover, value dependencies, fresh-seed maintenance and independent mutation oracle");
}
} // namespace

int main(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="check") {check();return 0;}
    if(argc!=5) {std::fprintf(stderr,"usage: lifecycle check | lifecycle N SLOTS TURNOVERS REPS\n");return 2;}
    const auto n=std::stoull(argv[1]);const unsigned slots=std::stoul(argv[2]);
    const unsigned turnovers=std::stoul(argv[3]),repetitions=std::stoul(argv[4]);
    require(n && n<(word{1}<<32) && (slots==10 || slots==16) && turnovers && turnovers<=64 && repetitions);
    std::puts("initial_n,slots,rep,generation,table,operation,ns_per_operation,checksum,live_keys,primary_or_abseil_slots,requested_payload_bytes,backing_capacity_bytes,control_bytes,primary_keys,secondary_keys,overflow_keys,rebuild_overlap_payload_bytes,rebuild_overlap_capacity_bytes,abseil_current_instance_allocator_peak_bytes,pool_mapped_bytes,pool_metadata_bytes,active_trace_bytes");
    for(unsigned r=0;r<repetitions;++r) {
        if(r%2) {run<true>(n,slots,turnovers,r);run<false>(n,slots,turnovers,r);}
        else {run<false>(n,slots,turnovers,r);run<true>(n,slots,turnovers,r);}
    }
}
