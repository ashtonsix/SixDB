#include "migration.h"
#include "small_baseline.h"
#include <absl/container/internal/hashtable_debug.h>
#include <map>
#include <numeric>
#include <random>
#include <string>

#ifndef HT_CYCLE_KEY
#define HT_CYCLE_KEY 40
#endif
#ifndef HT_CYCLE_KIND
#define HT_CYCLE_KIND 1
#endif
#ifndef HT_CYCLE_WIDTH
#define HT_CYCLE_WIDTH 2
#endif
#ifndef HT_CYCLE_SLOTS
#define HT_CYCLE_SLOTS 14
#endif
#ifndef HT_CYCLE_STEP
#define HT_CYCLE_STEP 1
#endif
#ifndef HT_CYCLE_LOAD
#define HT_CYCLE_LOAD 90
#endif
#ifndef HT_CYCLE_ALLOCATING_ORACLE
#define HT_CYCLE_ALLOCATING_ORACLE 0
#endif

namespace ht::cycle {
constexpr unsigned K=HT_CYCLE_KEY,W=HT_CYCLE_WIDTH,Slots=HT_CYCLE_SLOTS;
constexpr unsigned Step=HT_CYCLE_STEP,Load=HT_CYCLE_LOAD,Chunk=64;
constexpr auto Kind=container_kind(HT_CYCLE_KIND);
static_assert(Step==1||Step==2);
static_assert(Slots==14||Slots==28);
static_assert(Load>=75&&Load<=100);
using value=std::array<byte,W>;
using Bags=linked_bags<(W?W:8)>;
constexpr unsigned Inline=Kind==container_kind::multimap&&W<=16?16:8;
using Small=small_table<K,Kind,W,false,Bags,Inline>;
template<unsigned A> struct shape {
    using Planner=record_table<K,Kind,Small::stored_bytes,A,A-2,128,128,
        record_format::exact,false,record_access::both,Small::tag_bits>;
    static constexpr unsigned P=Planner::unit_for_slots(Slots,false,8);
    static constexpr unsigned Q=Planner::unit_for_slots(Slots==14?5:8,true,8);
    using Table=promoted_table<K,Kind,W,A,A-2,P,Q,record_format::exact,false,
        record_access::both,false,Bags,Inline>;
    using Index=record_table<K,Kind,Small::stored_bytes,A,A-2,P,Q,
        record_format::exact,false,record_access::both,Small::tag_bits>;
    static constexpr word capacity=(word(1)<<A)*Index::primary.shape.slots;
};
template<unsigned A=2> consteval unsigned first_bits() {
    if constexpr(shape<A>::capacity>=512||A==K-2)return A;
    else return first_bits<A+1>();
}
constexpr unsigned First=first_bits();
struct entry {word key;value bytes;};
using oracle=std::map<word,std::vector<value>>;
struct fixture {
    unsigned seed;
    std::vector<entry> input;
    std::vector<word> keys,digests;
    std::vector<unsigned> successor;
    oracle expected;
    mutable std::map<word,unsigned> seen;
};
word digest(std::span<const byte,W> bytes) {
    word out=0;lanes<W/8>([&](auto i){word v;std::memcpy(&v,bytes.data()+8*i,8);out^=v;});
    if constexpr(W%8){word v=0;std::memcpy(&v,bytes.data()+W/8*8,W%8);out^=v;}return out;
}
fixture make_input(unsigned n,unsigned fanout,unsigned seed) {
    fixture f;f.seed=seed;std::vector<unsigned> order(n);std::iota(order.begin(),order.end(),0);
    std::mt19937_64 rng(seed+719);std::shuffle(order.begin(),order.end(),rng);
    f.keys.resize(n);f.digests.resize(n);f.successor.resize(n);
    for(unsigned i=0;i<n;++i) {
        const auto key=word(i)*0xd6e8feb86659fd93ULL&mask(K);f.keys[i]=key;
        f.successor[order[i]]=order[(i+1)%n];
    }
    // Contiguous per-key fanout exposes duplicate admission conservatism. The
    // owner sees only each bounded chunk, never this final population.
    for(auto i:order)for(unsigned j=0;j<fanout;++j) {
        entry e{f.keys[i],{}};
        for(unsigned b=0;b<W;++b)e.bytes[b]=byte(mix(word(i)+word(j)*911+b*137));
        f.input.push_back(e);auto& bag=f.expected[e.key];
        if constexpr(Kind==container_kind::multimap)bag.push_back(e.bytes);
        else bag.assign(1,e.bytes);
    }
    for(unsigned i=0;i<n;++i) {
        f.seen.emplace(f.keys[i],0);
        for(const auto& v:f.expected[f.keys[i]])f.digests[i]^=digest(v);
    }
    return f;
}

template<bool Exact> struct baseline {
    using key_type=std::conditional_t<Exact,hashtable_spike::exact_unsigned_key<K/8>,typename abseil<K>::key_type>;
    using bag=absl::InlinedVector<value,1>;
    using mapped=std::conditional_t<Kind==container_kind::multimap,bag,value>;
    using storage_type=std::conditional_t<Kind==container_kind::set,absl::flat_hash_set<key_type>,absl::flat_hash_map<key_type,mapped>>;
    storage_type storage;
    bool set_bytes(word k,const value& bytes,no_small_effects&) {
        if constexpr(Kind==container_kind::set){[[clang::always_inline]]storage.insert(key_type(k));}
        else if constexpr(Kind==container_kind::map){[[clang::always_inline]]storage.insert_or_assign(key_type(k),bytes);}
        else {
            std::pair<typename storage_type::iterator,bool> p;
            [[clang::always_inline]]p=storage.try_emplace(key_type(k));p.first->second.push_back(bytes);
        }
        return true;
    }
    template<class F> [[gnu::always_inline]] bool get_bytes(word k,F&& f)const {
        typename storage_type::const_iterator it;
        [[clang::always_inline]]it=storage.find(key_type(k));if(it==storage.end())return false;
        if constexpr(Kind==container_kind::set)f(std::span<const byte,0>{});
        else if constexpr(Kind==container_kind::map)f(it->second);
        else for(const auto& v:it->second)f(v);
        return true;
    }
    unsigned size()const{return storage.size();}
    template<class F> void each_bytes(F&& f)const {
        auto key=[](const auto& k)->word {if constexpr(Exact)return k.value();else return k;};
        if constexpr(Kind==container_kind::set)for(auto k:storage)f(key(k),std::span<const byte,0>{});
        else for(const auto& [k,v]:storage) {
            if constexpr(Kind==container_kind::map)f(key(k),v);
            else for(const auto& item:v)f(key(k),item);
        }
    }
    memory footprint()const {
        word bytes=absl::container_internal::AllocatedByteSize(storage);
        if constexpr(Kind==container_kind::multimap)for(const auto& [k,v]:storage) {
            const auto p=reinterpret_cast<std::uintptr_t>(v.data()),b=reinterpret_cast<std::uintptr_t>(&v);
            if(p<b||p>=b+sizeof(v))bytes+=v.capacity()*W;
        }
        return {bytes,0,sizeof(*this)};
    }
};

struct sample {unsigned accepted,keys,level;memory bytes;};
struct result {
    double build_ns=0,growth_ns=0,retire_ns=0,hit_ns=0,serial_ns=0;
    word scan_work=0,moved=0,coexist_logical=0,coexist_backing=0,control_objects=0;
    unsigned migrations=0,final_level=0;
    std::vector<sample> samples;
};
struct run {
    const fixture& f;bool inspect;unsigned at=0;word control_objects=0;
    result out{};clock_type::time_point start{};
    template<class T> void sample_owner(const T& table,unsigned level) {
        if(!inspect)return;
        const auto m=table.footprint();out.samples.push_back({at,table.size(),level,m});
        out.coexist_logical=std::max(out.coexist_logical,m.requested+m.metadata);
        out.coexist_backing=std::max(out.coexist_backing,m.backing);
    }
    template<class T> void verify(const T& table) {
#if HT_CYCLE_ALLOCATING_ORACLE
        oracle actual;table.each_bytes([&](word k,const auto& bytes) {
            value v{};std::copy(bytes.begin(),bytes.end(),v.begin());actual[k].push_back(v);
        });
        auto expected=f.expected;
        for(auto& [k,v]:actual)std::sort(v.begin(),v.end());
        for(auto& [k,v]:expected)std::sort(v.begin(),v.end());
        require(actual==expected,"growth cycle full-byte/multiplicity oracle");
#else
        // Allocate the oracle once with the caller input. Validation between
        // construction and retirement must not reshape the malloc free lists.
        for(auto& [key,seen]:f.seen)seen=0;
        table.each_bytes([&](word key,const auto& bytes) {
            const auto expected=f.expected.find(key);
            const auto at=f.seen.find(key);
            require(expected!=f.expected.end()&&at!=f.seen.end(),"independently expected key");
            bool matched=false;
            for(unsigned i=0;i<expected->second.size();++i)if(!(at->second&(1u<<i))&&
                    std::equal(bytes.begin(),bytes.end(),expected->second[i].begin(),expected->second[i].end())) {
                at->second|=1u<<i;matched=true;break;
            }
            require(matched,"complete value bytes and multiplicity");
        });
        for(const auto& [key,expected]:f.expected)
            require(f.seen.find(key)->second==(1u<<expected.size())-1,"all expected values visited exactly once");
#endif
    }
    // One call per measurement, outside both timed loops. Keep the complete
    // read consumer identifiable without outlining lookup within each query.
    template<class T> [[gnu::noinline]] void reads(const T& table) {
        if(inspect)return;
        const auto n=f.keys.size();const unsigned queries=std::max<std::size_t>(65536,n*16);
        word sum=0;
        auto before=clock_type::now();
        for(unsigned i=0;i<queries;++i) {
            const auto rank=i%n;
            const bool found=table.get_bytes(f.keys[rank],[&] [[gnu::always_inline]] (const auto& bytes){sum+=digest(bytes);});
            sum+=found;
        }
        out.hit_ns=std::chrono::duration<double,std::nano>(clock_type::now()-before).count()/queries;
        asm volatile("" : "+r"(sum) :: "memory");
        unsigned rank=0;before=clock_type::now();
        for(unsigned i=0;i<queries;++i) {
            word d=0;const bool found=table.get_bytes(f.keys[rank],[&] [[gnu::always_inline]] (const auto& bytes){d^=digest(bytes);});
            rank=(f.successor[rank]^(d^f.digests[rank])^word(!found))%n;
        }
        out.serial_ns=std::chrono::duration<double,std::nano>(clock_type::now()-before).count()/queries;
        asm volatile("" : "+r"(rank) :: "memory");
        unsigned expected=0;for(unsigned i=0;i<queries;++i)expected=f.successor[expected];
        require(rank==expected,"dependent traversal consumes the complete value digest");
    }
    template<class T> void finish(std::optional<T>& table,unsigned level) {
        out.build_ns=std::chrono::duration<double,std::nano>(clock_type::now()-start).count();
        out.final_level=level;verify(*table);reads(*table);
        const auto before=clock_type::now();table.reset();
        out.retire_ns=std::chrono::duration<double,std::nano>(clock_type::now()-before).count();
    }
    template<class T> bool fill(T& table,word limit,unsigned level) {
        no_small_effects effects;sample_owner(table,level);
        while(at<f.input.size()) {
            const unsigned count=std::min<std::size_t>(Chunk,f.input.size()-at);
            // K8 never needs hierarchy: even an all-new chunk cannot exceed
            // its complete domain. Other domains admit a conservative bound.
            const auto upper=K==8?std::min<word>(256,table.size()+count):table.size()+count;
            // A compact list can distinguish a full new-key insertion from an
            // existing-key update. Preserve that regime through 256 distinct
            // keys even if a pending chunk consists mostly of duplicate values.
            if constexpr(!std::is_same_v<T,Small>)if(upper>limit)return false;
            for(unsigned i=0;i<count;++i) {
                const auto& e=f.input[at+i];
                const auto r=[&] [[gnu::always_inline]] {
                    if constexpr(requires{table.try_set_bytes(e.key,e.bytes,effects);})return table.try_set_bytes(e.key,e.bytes,effects);
                    else return table.set_bytes(e.key,e.bytes,effects);
                }();
                if constexpr(std::is_same_v<T,Small>) {
                    if(!r) {
                        require(table.size()==256&&r.error()==mutation_error::capacity&&
                            !table.get_bytes(e.key,[](const auto&){}),"full-list new key; other refusal is not promotion");
                        at+=i;sample_owner(table,level);return false;
                    }
                }else require(bool(r),"complete bounded growth input");
            }
            at+=count;sample_owner(table,level);
        }
        return true;
    }
    template<unsigned A,class Source> void grow(std::optional<Source>& source,unsigned level) {
        using Target=typename shape<A>::Table;
        const auto before=clock_type::now();
        // Caller-owned deterministic entropy fixture, fresh at each epoch.
        auto move=hierarchy_migration<Source,Target>::prepare(source,
            mix(117+level*931+word(f.seed)*65537),mix(9931+level*7919+word(f.seed)*2347));
        control_objects+=sizeof(move)+sizeof(std::optional<Target>);
        out.control_objects=std::max(out.control_objects,control_objects);
        require(bool(move),"growth cycle prepare");
        while(!move->ready()) {
            const auto r=move->step(64);require(r.state!=record_scan_state::refused,"growth cycle migration capacity");
            out.scan_work+=r.work;out.moved+=r.records;
        }
        if(inspect) {
            const auto a=source->footprint(),b=move->footprint();
            out.coexist_logical=std::max(out.coexist_logical,a.requested+a.metadata+b.requested+b.metadata);
            out.coexist_backing=std::max(out.coexist_backing,a.backing+b.backing);
        }
        std::optional<Target> target(move->finish());move.reset();++out.migrations;
        out.growth_ns+=std::chrono::duration<double,std::nano>(clock_type::now()-before).count();
        if(fill(*target,shape<A>::capacity*Load/100,level))finish(target,level);
        else if constexpr(A+Step<=First+4&&A+Step<K)grow<A+Step>(target,level+1);
        else require(false,"growth cycle exhausted measured levels");
        control_objects-=sizeof(move)+sizeof(std::optional<Target>);
    }
};

template<unsigned Arm> result trial(oa::Pool& pool,const fixture& f,bool inspect) {
    run r{f,inspect};r.start=clock_type::now();
    if constexpr(Arm==0) {
        std::optional<Small> table(std::in_place,pool,117+word(f.seed)*65537);
        r.control_objects=sizeof(table);r.out.control_objects=sizeof(table);
        if(r.fill(*table,256,0))r.finish(table,0);
        else if constexpr(K>8)r.grow<First>(table,1);
        else require(false,"K8 remains a compact list");
    }else {
        std::optional<baseline<Arm==2>> table(std::in_place);
        r.out.control_objects=sizeof(table);
        require(r.fill(*table,UINT64_MAX,0),"natural baseline growth");r.finish(table,0);
    }
    return std::move(r.out);
}
void emit(unsigned arm,int rep,const fixture& f,unsigned fanout,const result& r,const result& inventory) {
    word area=0,previous=0;
    for(const auto& s:inventory.samples) {
        area+=(s.accepted-previous)*(s.bytes.requested+s.bytes.metadata);previous=s.accepted;
    }
    const auto& last=inventory.samples.back();
    std::printf("%u,%u,%u,%u,%u,%u,%u,%u,%d,%zu,%u,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%llu,%llu,%llu,%lld,%llu,%lld,%lld,%.3f,%zu,%llu,%u\n",
        K,unsigned(Kind),W,Slots,Step,Load,arm,f.seed,rep,f.keys.size(),fanout,f.input.size(),
        r.build_ns,r.growth_ns,r.retire_ns,r.hit_ns,r.serial_ns,r.migrations,r.final_level,
        (unsigned long long)r.moved,(unsigned long long)r.scan_work,
        (unsigned long long)(last.bytes.requested+last.bytes.metadata),arm?-1LL:(long long)last.bytes.backing,
        (unsigned long long)last.bytes.metadata,arm?-1LL:(long long)inventory.coexist_logical,
        arm?-1LL:(long long)inventory.coexist_backing,double(area)/f.input.size(),
        f.keys.size()*(2*sizeof(word)+sizeof(unsigned)),(unsigned long long)r.control_objects,HT_CYCLE_ALLOCATING_ORACLE);
}
} // namespace ht::cycle

int main(int argc,char** argv) {
    using namespace ht;using namespace cycle;
    const bool checking=argc<2||std::string_view(argv[1])=="check";
    require(checking||std::string_view(argv[1])=="bench","growth cycle mode");
    const unsigned n=argc>2?std::stoul(argv[2]):K==8?256:6144;
    const unsigned reps=argc>3?std::stoul(argv[3]):3;
    const unsigned seed=argc>4?std::stoul(argv[4]):0;
    require(n&&n<=8192&&(K!=8||n<=256),"measured growth population");
    pool_owner pool;
    auto* trace_path=std::getenv("HT_CYCLE_TRACE");
    auto* trace=trace_path?std::fopen(trace_path,"w"):nullptr;
    require(!trace_path||trace,"growth memory trace output");
    if(trace)std::fprintf(trace,"K,kind,W,slots,step,load,arm,seed,fanout,accepted,keys,level,logical,backing,metadata\n");
    std::puts("K,kind,W,slots,step,load,arm,seed,rep,keys,fanout,operations,build_ns,growth_ns,retire_ns,hit_ns,serial_ns,migrations,level,moved,scan_work,logical,backing,metadata,observed_coexist_logical,observed_coexist_backing,sampled_mean_logical,read_graph_bytes,control_objects,allocating_oracle");
    for(unsigned fanout:{1u,4u}) {
        if constexpr(Kind!=container_kind::multimap)if(fanout!=1)continue;
        const auto f=make_input(n,fanout,seed);
        auto invoke=[&](unsigned arm,bool inspect) {
            switch(arm){case 0:return trial<0>(*pool.pool,f,inspect);case 1:return trial<1>(*pool.pool,f,inspect);default:return trial<2>(*pool.pool,f,inspect);}
        };
        std::array<result,3> inventories;
        for(unsigned arm=0;arm<3;++arm) {
            inventories[arm]=invoke(arm,true);
            if(arm==0&&n<=256)require(inventories[arm].migrations==0,"complete small domain remains a compact list");
            if(trace)for(const auto& s:inventories[arm].samples)std::fprintf(trace,
                "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%lld,%llu\n",
                K,unsigned(Kind),W,Slots,Step,Load,arm,seed,fanout,s.accepted,s.keys,s.level,
                (unsigned long long)(s.bytes.requested+s.bytes.metadata),arm?-1LL:(long long)s.bytes.backing,
                (unsigned long long)s.bytes.metadata);
        }
        for(unsigned rep=0;rep<(checking?1:reps);++rep)for(unsigned order=0;order<3;++order) {
            const unsigned arm=(order+rep)%3;const auto r=invoke(arm,false);const auto& inv=inventories[arm];
            require(r.migrations==inv.migrations&&r.moved==inv.moved&&r.scan_work==inv.scan_work&&r.final_level==inv.final_level,
                "timed growth replays identical transitions");
            emit(arm,rep,f,fanout,r,inv);
        }
    }
    if(trace)require(std::fclose(trace)==0,"growth trace complete");
}
