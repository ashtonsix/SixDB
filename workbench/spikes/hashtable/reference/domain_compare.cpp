#include "support.h"
#include "packed_key_baseline.h"
#include "single-plans.h"

#include <absl/container/internal/hashtablez_sampler.h>
#include <absl/container/internal/hashtable_debug.h>
#include <array>
#include <memory>
#include <random>
#include <string_view>
#include <unordered_map>

// A comparison of complete existing containers, not a new layout candidate.
// Equal total cardinality can mean one large table or many small ones. Report
// controls separately: the current comparator stores prepared TP state per table.
namespace {
using namespace hashtable_spike;
using Clock = std::chrono::steady_clock;

template<unsigned K> using native_key = std::conditional_t<(K <= 32), std::uint32_t, word>;
template<unsigned V> using native_value = std::conditional_t<V == 16, std::uint16_t, word>;
constexpr unsigned bound(unsigned K, unsigned V) { return K == 64 && V == 64 ? 1 : 0; }

struct Footprint {
    std::size_t requested{}, backing{}, objects{}, shared{}, catalog{}, capacity{};
    std::size_t primary{}, secondary{}, overflow{};
};

template<unsigned K, unsigned V, bool Direct> struct Candidate {
    using Table = basic_map<Direct, bound(K,V), K, V>;
    Table table;
    Candidate(oa::Pool& pool, std::size_t n, word seed) : table(pool,n,16,false,1,seed) {}
    [[gnu::always_inline]] bool get(word key, word& out) const { return table.get(key,out); }
    [[gnu::always_inline]] bool set(word key, word value) { return table.insert_or_assign(key,value); }
    [[gnu::always_inline]] bool erase(word key) { return table.erase(key); }
    void check() const { table.audit(); }
    void clear(){table.clear();}
    std::size_t size() const { return table.size(); }
    template<class F> void each(F&& f) const { table.each(std::forward<F>(f)); }
    Footprint memory() const {
        const auto m=table.memory(); const auto c=table.counts();
        return {m.payload,m.capacity,sizeof(Table),m.plans-sizeof(Table),m.catalog,0,
                c.primary,c.secondary,c.overflow};
    }
};

template<unsigned K, unsigned V, bool Exact> struct Baseline {
    using Key = std::conditional_t<Exact,exact_unsigned_key<K/8>,native_key<K>>;
    using Table = absl::flat_hash_map<Key,native_value<V>>;
    Table table;
    Baseline(oa::Pool&,std::size_t n,word) { table.reserve(n); }
    [[gnu::always_inline]] bool get(word key,word& out) const {
        const auto found=table.find(Key(key));if(found==table.end()) return false;
        out=found->second;return true;
    }
    [[gnu::always_inline]] bool set(word key,word value) { return table.insert_or_assign(Key(key),native_value<V>(value)).second; }
    [[gnu::always_inline]] bool erase(word key) { return table.erase(Key(key))!=0; }
    void check() const {}
    void clear(){table.clear();}
    std::size_t size() const { return table.size(); }
    template<class F> void each(F&& f) const {
        for(const auto& [key,value]:table) {
            if constexpr(Exact) f(key.value(),value);
            else f(word(key),value);
        }
    }
    Footprint memory() const {
        return {absl::container_internal::AllocatedByteSize(table),0,sizeof(Table),0,0,table.capacity()};
    }
    std::size_t debug_probe(word key) const {
        return absl::container_internal::GetHashtableDebugNumProbes(table,Key(key));
    }
};

// Full-key algorithm controls, not replacement primary baselines. The pinned
// Abseil custom-hasher path adds its normal per-table seed XOR. The second arm
// keeps Abseil's integer hash through that same custom path so the seed-fusion
// difference is visible instead of attributed entirely to the permutation.
constexpr word embed40(word h) {
    // This pinned Abseil uses low bits for H1 and bits57..63 for H2. Returning
    // the40-bit result directly would make every fingerprint zero. This is a
    // lossless placement of33 address bits and7 fingerprint bits, not another
    // mixer. The complete diagnostic admits capacities below2^33.
    return (h&mask(33)) | ((h>>33)<<57);
}
template<bool Strong> struct BridgeHash {
    struct Empty {};
    [[no_unique_address]] std::conditional_t<Strong,permutation<40,false>,Empty> state;
    explicit BridgeHash(word seed) : state([&] {
        if constexpr(Strong) return permutation<40,false>(seed);
        else return Empty{};
    }()) {}
    [[gnu::always_inline]] std::size_t operator()(const exact_unsigned_key<5>& key) const {
        if constexpr(Strong) return embed40(state(key.value()));
        else return absl::Hash<word>{}(key.value());
    }
};
template<bool Strong> struct HashBridge {
    using Key=exact_unsigned_key<5>;
    using Table=absl::flat_hash_map<Key,std::uint16_t,BridgeHash<Strong>>;
    Table table;
    HashBridge(oa::Pool&,std::size_t n,word seed):table(0,BridgeHash<Strong>(seed)) {table.reserve(n);check();}
    [[gnu::always_inline]] bool get(word key,word& out) const {
        const auto found=table.find(Key(key));if(found==table.end()) return false;
        out=found->second;return true;
    }
    [[gnu::always_inline]] bool set(word key,word value) {
        return table.insert_or_assign(Key(key),std::uint16_t(value)).second;
    }
    [[gnu::always_inline]] bool erase(word key) {return table.erase(Key(key))!=0;}
    void check() const {require(table.capacity()<(word{1}<<33));}
    void clear(){table.clear();}
    std::size_t size() const {return table.size();}
    template<class F> void each(F&& f) const {
        for(const auto& [key,value]:table) f(key.value(),value);
    }
    Footprint memory() const {
        return {absl::container_internal::AllocatedByteSize(table),0,sizeof(Table),0,0,table.capacity()};
    }
    std::size_t debug_probe(word key) const {
        return absl::container_internal::GetHashtableDebugNumProbes(table,Key(key));
    }
};

template<unsigned K, bool Exact> struct Baseline<K,0,Exact> {
    using Key = std::conditional_t<Exact,exact_unsigned_key<K/8>,native_key<K>>;
    using Ordinary = absl::flat_hash_set<Key>;
    Ordinary table;
    Baseline(oa::Pool&,std::size_t n,word) { table.reserve(n); }
    [[gnu::always_inline]] bool get(word key,word& out) const {
        if (!table.contains(Key(key))) return false;
        out=0; return true;
    }
    [[gnu::always_inline]] bool set(word key,word) { return table.insert(Key(key)).second; }
    [[gnu::always_inline]] bool erase(word key) { return table.erase(Key(key))!=0; }
    void check() const {}
    void clear(){table.clear();}
    std::size_t size() const { return table.size(); }
    template<class F> void each(F&& f) const {
        for (const auto& key:table) {
            if constexpr(Exact) f(key.value(),0);
            else f(word(key),0);
        }
    }
    Footprint memory() const { return {absl::container_internal::AllocatedByteSize(table),0,sizeof(Ordinary),0,0,table.capacity()}; }
};

// Owner in this executable; the material itself borrows bytes and a shared
// binding. A full new-key insertion is never timed as a successful operation.
// The growth transition is the next owner-level comparison.
template<single::plan P,bool Direct> struct Single {
    oa::Allocation storage;
    inline static const single::binding<P> prepared{};
    static const single::binding<P>& plan() {return prepared;}
    Single(oa::Pool& pool,std::size_t n,word) {
        require(n<=P.slots);const auto& prepared=plan();
        auto a=oa::allocate(pool,{.bytes=P.bytes,.alignment=16,.initialization=oa::Initialization::zero});
        require(bool(a));storage=std::move(*a);
        single::kernel<P,Direct>{reinterpret_cast<byte*>(storage.address),prepared}.initialize();
    }
    auto view() const {return single::kernel<P,Direct>{reinterpret_cast<byte*>(storage.address),plan()};}
    [[gnu::always_inline]] bool get(word key,word& out) const {return view().get(key,out);}
    bool set(word key,word value) {
        const auto result=view().set(key,value);
        require(result.result==single::status::inserted||result.result==single::status::replaced);
        return result.distinct_delta==1;
    }
    bool erase(word key) {return view().erase(key).distinct_delta==-1;}
    void check() const {require(size()<=P.slots);}
    std::size_t size() const {return view().size();}
    template<class F> void each(F&& f) const {view().each(std::forward<F>(f));}
    void clear(){view().clear();}
    Footprint memory() const {return {P.bytes,oa::capacity(storage),sizeof(*this),sizeof(single::binding<P>),0,0,size()};}
};

struct Query { word key; std::size_t table, encoded_next; };
struct Replacement { word before,after,value;std::size_t table; };
// The serial workload depends on membership and every value byte. Both set and
// map cases include a common query-source load and table-pointer lookup. This is
// application-style serial throughput, not isolated lookup instruction latency.
[[gnu::always_inline]] word fold(word x) { x^=x>>32; x^=x>>16; return x^(x>>8); }
template<unsigned V> word value_for(word key,std::size_t table,bool updated=false) {
    return (mix(key^mix(table+173)) & mask(V)) ^ (updated?mask(V):0);
}

template<unsigned K,unsigned V> struct Trace {
    std::size_t n, tables;
    bool varied;
    std::array<std::vector<word>,3> keys;
    std::array<std::vector<word>,2> values;
    std::vector<Query> hits, misses, mixed, aged;
    std::vector<Replacement> replacements;
    word key(unsigned bank,std::size_t table,std::size_t index) const {return keys[bank][(varied?table*n:0)+index];}
    Trace(std::size_t entries,std::size_t count,unsigned repetition,bool distinct=false):n(entries),tables(count),varied(distinct) {
        permutation<K,false> source(1849+repetition);
        for(unsigned bank=0;bank<3;++bank) {
            const auto length=n*(varied?tables:1);
            keys[bank].resize(length);
            for(std::size_t i=0;i<length;++i) keys[bank][i]=source(bank*length+i);
        }
        std::mt19937_64 random(38119+repetition);
        for(unsigned bank=0;bank<2;++bank) {
            values[bank].resize(n*tables);
            for(std::size_t t=0;t<tables;++t) for(std::size_t i=0;i<n;++i)
                values[bank][t*n+i]=value_for<V>(key(bank,t,i),t);
        }
        hits.reserve(n*tables); misses.reserve(n*tables);
        for(std::size_t t=0;t<tables;++t) for(std::size_t i=0;i<n;++i) {
            // Outside serial_mixed, the third word holds the expected value.
            hits.push_back({key(0,t,i),t,values[0][t*n+i]}); misses.push_back({key(2,t,i),t,0});
        }
        std::shuffle(hits.begin(),hits.end(),random); std::shuffle(misses.begin(),misses.end(),random);
        aged.reserve(hits.size());replacements.reserve(hits.size());
        for(const auto& q:hits) {
            const auto i=source.invert(q.key);const auto next=keys[1][i];
            aged.push_back({next,q.table,values[1][q.table*n+i%n]});
            replacements.push_back({q.key,next,values[1][q.table*n+i%n],q.table});
        }
        mixed=hits; mixed.insert(mixed.end(),misses.begin(),misses.end());
        std::vector<std::size_t> order(mixed.size()); std::iota(order.begin(),order.end(),0);
        std::shuffle(order.begin(),order.end(),random);
        for(std::size_t i=0;i<order.size();++i) {
            const auto at=order[i], next=order[(i+1)%order.size()];
            const bool present=at<hits.size();
            const auto& q=mixed[at];
            mixed[at].encoded_next=next ^ fold(present?value_for<V>(q.key,q.table):0) ^ word(present);
        }
        // Verify a single complete cycle independently of any table lookup.
        // Matching checksums alone would not detect a shared short-cycle bug.
        std::vector<bool> seen(mixed.size());std::size_t index=0;
        for(std::size_t i=0;i<mixed.size();++i) {
            require(!seen[index]);seen[index]=true;
            const auto& q=mixed[index];const bool present=index<hits.size();
            index=q.encoded_next ^ fold(present?value_for<V>(q.key,q.table):0) ^ word(present);
            require(index<mixed.size());
        }
        require(index==0);
    }
    std::size_t input_bytes() const {
        return 3*n*(varied?tables:1)*sizeof(word)+2*n*tables*sizeof(word)
             +(hits.size()+misses.size()+mixed.size()+aged.size())*sizeof(Query)
             +replacements.size()*sizeof(Replacement);
    }
};

struct Sample { double ns; word checksum; };
template<class F> Sample timed(std::size_t operations,F&& f) {
    const auto start=Clock::now();word sum=f();asm volatile("":"+r"(sum)::"memory");
    return {std::chrono::duration<double,std::nano>(Clock::now()-start).count()/double(operations),sum};
}
template<class Map> using Collection=std::vector<std::unique_ptr<Map>>;

template<class Map> [[gnu::noinline]] word independent(const Collection<Map>& maps,
                                                     const std::vector<Query>& queries,unsigned passes) {
    word sum=0;
    for(unsigned pass=0;pass<passes;++pass) for(const auto& q:queries) {
        word value=0;const bool found=maps[q.table]->get(q.key,value);sum+=value+word(found);
    }
    return sum;
}
template<class Map> [[gnu::noinline]] word serial(const Collection<Map>& maps,
                                                const std::vector<Query>& queries,unsigned passes) {
    std::size_t index=0;word sum=0;
    for(std::size_t i=0;i<queries.size()*passes;++i) {
        const auto& q=queries[index];word value=0;const bool found=maps[q.table]->get(q.key,value);
        sum+=value+word(found);
        index=(q.encoded_next ^ fold(value) ^ word(found)) & (queries.size()-1);
        asm volatile("":"+r"(index):"r"(found),"r"(value));
    }
    return sum^index;
}

template<unsigned K,unsigned V,class Map>
void verify(const Collection<Map>& maps,const Trace<K,V>& input,unsigned bank,bool updated=false) {
    for(std::size_t t=0;t<maps.size();++t) {
        const auto& map=*maps[t]; map.check();require(map.size()==input.n);
        for(unsigned b=0;b<3;++b) for(std::size_t i=0;i<input.n;++i) {
            const auto key=input.key(b,t,i);
            word out=~word{}; const bool found=map.get(key,out);
            require(found==(b==bank));require(out==(found?value_for<V>(key,t,updated):~word{}));
        }
        std::unordered_map<word,word> visited;
        map.each([&](word k,word v) { require(visited.emplace(k,v).second); });
        require(visited.size()==input.n);
        for(std::size_t i=0;i<input.n;++i) {const auto key=input.key(bank,t,i);require(visited.contains(key)&&visited.at(key)==value_for<V>(key,t,updated));}
    }
}

template<unsigned K,unsigned V,class Map>
std::vector<word> run(const Trace<K,V>& input,unsigned repetition,unsigned passes,
                      const char* label,bool output,std::FILE* probe_output=nullptr) {
    pool_owner owner; Collection<Map> maps;
    const auto total=input.n*input.tables;
    std::vector<word> checksums;
    auto probes=[&](const char* phase,unsigned live_bank) {
        if constexpr(requires(const Map& m) {m.debug_probe(word{});}) {
            if(!probe_output) return;
            for(unsigned bank:{live_bank,2u}) {
                std::vector<std::size_t> histogram;
                for(std::size_t t=0;t<maps.size();++t) for(std::size_t i=0;i<input.n;++i) {
                    const auto p=maps[t]->debug_probe(input.key(bank,t,i));
                    if(p>=histogram.size()) histogram.resize(p+1);
                    ++histogram[p];
                }
                require(std::accumulate(histogram.begin(),histogram.end(),std::size_t{})==total);
                for(std::size_t p=0;p<histogram.size();++p) if(histogram[p])
                    std::fprintf(probe_output,"%s,%s,%s,%u,%zu,%zu\n",label,phase,
                        bank==live_bank?"hit":"miss",repetition,p,histogram[p]);
            }
        }
    };
    auto emit=[&](const char* operation,Sample s) {
        checksums.push_back(s.checksum); if(!output) return;
        Footprint m{};
        for(const auto& map:maps) {
            const auto v=map->memory();m.requested+=v.requested;m.backing+=v.backing;
            m.objects+=v.objects;m.shared=std::max(m.shared,v.shared);m.catalog+=v.catalog;
            m.capacity+=v.capacity;m.primary+=v.primary;m.secondary+=v.secondary;m.overflow+=v.overflow;
        }
        const auto pool=oa::statistics(*owner.pool);
        std::printf("%u,%u,%zu,%zu,%s,%s,%u,%.6f,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%s\n",
            K,V,input.n,input.tables,label,operation,repetition,s.ns,(unsigned long long)s.checksum,
            m.requested,m.backing,m.objects,m.shared,m.catalog,m.capacity,m.primary,m.secondary,m.overflow,
            maps.capacity()*sizeof(std::unique_ptr<Map>),input.input_bytes(),std::size_t(pool.mapped_bytes),std::size_t(pool.metadata_bytes),input.varied?"distinct":"shared");
        std::fflush(stdout);
    };
    const auto built=timed(total,[&] {
        maps.reserve(input.tables);word inserted=0;
        for(std::size_t t=0;t<input.tables;++t) {
            maps.emplace_back(std::make_unique<Map>(*owner.pool,input.n,mix(t+773+repetition*127)));
            for(std::size_t i=0;i<input.n;++i)
                inserted+=maps.back()->set(input.key(0,t,i),input.values[0][t*input.n+i]);
        }
        return inserted;
    });
    require(built.checksum==total);verify(maps,input,0);emit("construct_fill",built);
    probes("fresh",0);
    auto read=[&](const char* label,std::size_t operations,auto&& work) {
        const auto warm=work();asm volatile(""::"r"(warm):"memory");
        emit(label,timed(operations,work));
    };
    read("independent_hit",total*passes,[&] {return independent(maps,input.hits,passes);});
    read("independent_miss",total*passes,[&] {return independent(maps,input.misses,passes);});
    read("serial_mixed",input.mixed.size()*passes,[&] {return serial(maps,input.mixed,passes);});
    emit(V?"changed_value_update":"duplicate_insert",timed(total,[&] {
        word inserted=0;for(const auto& q:input.hits) inserted+=maps[q.table]->set(q.key,q.encoded_next^mask(V));
        return inserted;
    }));
    require(checksums.back()==0);verify(maps,input,0,true);
    emit("shuffled_erase_insert_pair",timed(total,[&] {
        word changes=0;
        for(const auto& request:input.replacements) {
            changes+=maps[request.table]->erase(request.before);
            changes+=maps[request.table]->set(request.after,request.value);
        }
        return changes;
    }));
    require(checksums.back()==2*total);verify(maps,input,1);
    probes("aged",1);
    read("post_churn_hit",total*passes,[&] {return independent(maps,input.aged,passes);});
    read("iterate",total*passes,[&] {
        word sum=0;
        for(unsigned p=0;p<passes;++p) for(const auto& map:maps)
            map->each([&](word k,word v) {sum+=k+v;});
        return sum;
    });
    emit("clear_per_removed_entry",timed(total,[&] {for(auto& map:maps)map->clear();return word(total);}));
    for(const auto& map:maps) {
        require(map->size()==0);map->each([](word,word){require(false);});
        for(auto key:input.keys[1]){word value=12345;require(!map->get(key,value)&&value==12345);}
    }
    emit("destroy",timed(total,[&] {maps.clear();return word(total);}));
    return checksums;
}

template<unsigned K,unsigned V>
void compare(std::size_t n,std::size_t tables,unsigned repetitions,unsigned passes,bool output) {
    require(n&&tables&&std::has_single_bit(n*tables)&&passes&&repetitions);
    require(K==64 || n*3<=mask(K));
    for(unsigned r=0;r<repetitions;++r) {
        Trace<K,V> input(n,tables,r);std::vector<word> expected;
        for(unsigned step=0;step<4;++step) {
            std::vector<word> got;
            switch((step+r)%4) {
            case 0: got=run<K,V,Candidate<K,V,false>>(input,r,passes,"composed",output);break;
            case 1: got=run<K,V,Candidate<K,V,true>>(input,r,passes,"same_storage_direct",output);break;
            case 2: got=run<K,V,Baseline<K,V,false>>(input,r,passes,"abseil_native_key",output);break;
            case 3: got=run<K,V,Baseline<K,V,true>>(input,r,passes,"abseil_exact_key",output);break;
            }
            if(step==0) expected=got; else require(got==expected);
        }
    }
}

template<unsigned K,unsigned V,single::plan Rows,single::plan Planes>
void compare_single(std::size_t n,std::size_t tables,unsigned repetitions,unsigned passes,bool output,bool varied=false) {
    require(n&&n<=Rows.slots&&Rows.slots==Planes.slots&&std::has_single_bit(n*tables)&&repetitions&&passes);
    // Preparation is once per schema, separate from per-table construction.
    (void)Single<Rows,false>::plan();(void)Single<Planes,false>::plan();
    (void)Single<Rows,true>::plan();(void)Single<Planes,true>::plan();
    for(unsigned r=0;r<repetitions;++r) {
        Trace<K,V> input(n,tables,r,varied);std::vector<word> expected;
        for(unsigned step=0;step<6;++step) {
            std::vector<word> got;
            switch((step+r)%6) {
            case 0:got=run<K,V,Single<Rows,false>>(input,r,passes,"single_rows_composed",output);break;
            case 1:got=run<K,V,Single<Rows,true>>(input,r,passes,"single_rows_direct",output);break;
            case 2:got=run<K,V,Single<Planes,false>>(input,r,passes,"single_planes_composed",output);break;
            case 3:got=run<K,V,Single<Planes,true>>(input,r,passes,"single_planes_direct",output);break;
            case 4:got=run<K,V,Baseline<K,V,false>>(input,r,passes,"abseil_native_key",output);break;
            case 5:got=run<K,V,Baseline<K,V,true>>(input,r,passes,"abseil_exact_key",output);break;
            }
            if(step==0)expected=got;else require(got==expected);
        }
    }
}
void single_panel(unsigned k,unsigned slots,std::size_t n,std::size_t tables,unsigned reps,unsigned passes,bool output,bool varied=false) {
#define SINGLE(K,V,N) compare_single<K,V,single::k##K##v##V##_rows##N,single::k##K##v##V##_planes##N>(n,tables,reps,passes,output,varied)
    require(slots==16||slots==32);
    switch(k) {
    case 24:if(slots==16){SINGLE(24,0,16);}else{SINGLE(24,0,32);}break;
    case 40:if(slots==16){SINGLE(40,16,16);}else{SINGLE(40,16,32);}break;
    case 64:if(slots==16){SINGLE(64,64,16);}else{SINGLE(64,64,32);}break;
    default:std::abort();
    }
#undef SINGLE
}

void compare_hash_bridge(std::size_t n,unsigned repetitions,unsigned passes,bool output) {
    require(n&&std::has_single_bit(n)&&n*3<=mask(40)&&repetitions&&passes);
    for(unsigned r=0;r<repetitions;++r) {
        Trace<40,16> input(n,1,r);std::vector<word> expected;
        for(unsigned step=0;step<5;++step) {
            std::vector<word> got;
            switch((step+r)%5) {
            case 0:got=run<40,16,Candidate<40,16,false>>(input,r,passes,"composed",output);break;
            case 1:got=run<40,16,Candidate<40,16,true>>(input,r,passes,"same_storage_direct",output);break;
            case 2:got=run<40,16,Baseline<40,16,true>>(input,r,passes,"abseil_exact_default",output,output?stderr:nullptr);break;
            case 3:got=run<40,16,HashBridge<false>>(input,r,passes,"abseil_exact_custom_absl_hash",output,output?stderr:nullptr);break;
            case 4:got=run<40,16,HashBridge<true>>(input,r,passes,"abseil_exact_embedded_permutation",output,output?stderr:nullptr);break;
            }
            if(step==0) expected=got;else require(got==expected);
        }
    }
}
} // namespace

int main(int argc,char** argv) {
    // Sampling affects a small fraction of tables; a many-table comparison
    // must not accidentally measure sampled and unsampled collections differently.
    absl::container_internal::SetHashtablezEnabled(false);
    if(argc==2&&std::string_view(argv[1])=="check") {
        static_assert(sizeof(std::size_t)==8);
        // Verify the adapter against this library's real bit consumers, not
        // merely against another spelling of the embedding expression.
        for(unsigned bit=0;bit<40;++bit) {
            const word encoded=embed40(word{1}<<bit);
            require((absl::container_internal::H1(encoded)&mask(33))==(bit<33?(word{1}<<bit):0));
            require(absl::container_internal::H2(encoded)==(bit>=33?(word{1}<<(bit-33)):0));
        }
        for(auto k:{24u,40u,64u}) for(auto slots:{16u,32u})
            for(bool varied:{false,true})single_panel(k,slots,slots,4,6,2,false,varied);
        for(auto n:{16u,256u}) {
            compare<24,0>(n,4,4,2,false);compare<40,16>(n,4,4,2,false);compare<64,64>(n,4,4,2,false);
            compare_hash_bridge(n,5,2,false);
        }
        std::puts("domain checks: common traces, full values, unchanged misses, iteration, mutation, tiers and collection lifetime passed");
        return 0;
    }
    require(argc==6||argc==8);
    const unsigned reps=std::stoul(argv[4]),passes=std::stoul(argv[5]);
    const auto n=std::stoull(argv[2]),tables=std::stoull(argv[3]);
    std::puts("key_bits,value_bits,entries_per_table,tables,implementation,operation,rep,ns_per_operation,checksum,requested_bytes,backing_bytes,table_object_bytes,shared_controls_bytes,dynamic_catalog_bytes,abseil_capacity,primary_keys,secondary_keys,overflow_keys,table_pointer_bytes,input_bytes,pool_mapped_bytes,pool_metadata_bytes,key_sets");
    if(std::string_view(argv[1])=="single"||std::string_view(argv[1])=="single-varied") {
        require(argc==8);single_panel(std::stoul(argv[6]),std::stoul(argv[7]),n,tables,reps,passes,true,std::string_view(argv[1])=="single-varied");return 0;
    }
    require(argc==6);
    if(std::string_view(argv[1])=="bridge") {
        require(tables==1);
        std::fputs("implementation,phase,query,rep,debug_probes,keys\n",stderr);
        compare_hash_bridge(n,reps,passes,true);return 0;
    }
    const unsigned which=std::stoul(argv[1]);
    switch(which) {
    case 24:compare<24,0>(n,tables,reps,passes,true);break;
    case 40:compare<40,16>(n,tables,reps,passes,true);break;
    case 64:compare<64,64>(n,tables,reps,passes,true);break;
    default:std::abort();
    }
}
