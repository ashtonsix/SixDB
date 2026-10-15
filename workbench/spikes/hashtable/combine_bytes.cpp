#include "migration.h"
#include "small_baseline.h"
#include <absl/container/internal/hashtable_debug.h>
#include <map>
#include <numeric>
#include <random>
#include <string>

#ifndef HT_COMBINE_KEY
#define HT_COMBINE_KEY 40
#endif
#ifndef HT_COMBINE_WIDTH
#define HT_COMBINE_WIDTH 16
#endif

namespace ht::combine_test {
constexpr unsigned K=HT_COMBINE_KEY,W=HT_COMBINE_WIDTH;
using value=std::array<byte,W>;
using Small=small_table<K,container_kind::map,W>;
using Body=promoted_table<K,container_kind::map,W,4,2,192,96>;
using Bigger=promoted_table<K,container_kind::map,W,6,4,192,96>;
using Crowded=promoted_table<K,container_kind::map,W,1,0,96,96>;
// Widths must shape capacity from the first comparison. Keep deliberately
// crowded geometry in behavior checks; don't make a 16-bucket word-map control
// the timed default for 64-byte values. This is still a diagnostic capacity,
// not the format solver's final selection or a list-promotion threshold.
constexpr unsigned bench_a=std::min(K-1,W<=3?4u:W<=8?5u:W<=16?6u:W<=32?7u:8u);
using BenchBody=promoted_table<K,container_kind::map,W,bench_a,bench_a-2,192,96>;
word key(unsigned i){return word(i)*0xd6e8feb86659fd93ULL&mask(K);}
value input(unsigned i,unsigned salt) {
    value out;for(unsigned j=0;j<W;++j)out[j]=byte(mix(word(i)*97+salt*101+word(j)*7919));return out;
}
// A caller-owned associative combiner, not an implicit map merge policy.
// Full 16-byte regions use explicit SIMD; the bounded tail uses exact loads.
struct xor_value {
    [[gnu::always_inline]] value operator()(const value& a,const value& b)const noexcept {
        value out;
        lanes<W/16>([&] [[gnu::always_inline]] (auto lane) {
            constexpr unsigned at=decltype(lane)::value*16;
#if defined(__aarch64__)
            vst1q_u8(out.data()+at,veorq_u8(vld1q_u8(a.data()+at),vld1q_u8(b.data()+at)));
#else
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out.data()+at),_mm_xor_si128(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(a.data()+at)),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(b.data()+at))));
#endif
        });
        constexpr unsigned at=W/16*16;
        if constexpr(W-at>=8)put<8>(out.data()+at,get<8>(a.data()+at)^get<8>(b.data()+at));
        constexpr unsigned tail=at+(W-at>=8?8:0);
        lanes<W-tail>([&] [[gnu::always_inline]] (auto i){out[tail+i]=a[tail+i]^b[tail+i];});
        return out;
    }
};
template<class Table> auto contents(const Table& t) {
    std::map<word,value> out;
    t.each_bytes([&](word k,auto bytes){value v;std::copy(bytes.begin(),bytes.end(),v.begin());
        require(out.emplace(k,v).second,"combine unique keys");});return out;
}
template<class Table> void history(Table& table,unsigned n) {
    std::map<word,value> expected;
    std::array<small_event,32> events;
    unsigned calls=0;
    auto combine=[&](const value& old,const value& incoming)noexcept {++calls;return xor_value{}(old,incoming);};
    for(unsigned pass=0;pass<4;++pass)for(unsigned i=0;i<n;++i) {
        const auto k=key((i*73)%n);const auto v=input(i,pass);
        const bool exists=expected.contains(k);const auto prior=expected[k];
        small_journal journal{events};auto delta=table.try_combine_bytes(k,v,combine,journal);
        require(bool(delta)&&delta->keys==int(!exists)&&delta->values==int(!exists),"combine occupancy delta");
        require(journal.used && events[journal.used-1].type==small_event::kind::caller_delta,
            "combine publishes caller delta");
        bool write=false;for(unsigned j=0;j<journal.used;++j)write|=events[j].size!=0;
        require(write,"combine publishes byte damage");
        for(unsigned j=0;j<W;++j)expected[k][j]=exists?byte(prior[j]^v[j]):v[j];
    }
    require(calls==3*n && contents(table)==expected,"independent complete-byte combine oracle");
    auto retained=table.clone();require(bool(retained),"combine retained owner");
    const auto old=expected;small_journal refused{{}};
    auto result=table.try_combine_bytes(key(0),input(0,9),combine,refused);
    require(!result && result.error()==mutation_error::effects && calls==3*n && contents(table)==expected,
        "refusal precedes combiner and stores");
    no_small_effects effects;
    for(unsigned i=0;i<n;i+=2) {table.erase(key(i));expected.erase(key(i));}
    for(unsigned i=0;i<n;i+=2) {
        auto v=input(i,17);require(bool(table.try_combine_bytes(key(i),v,combine,effects)),"combine after erase");
        expected[key(i)]=v;
    }
    require(contents(table)==expected&&contents(*retained)==old,"swap erase/reinsert preserves retained values");
}
// XOR alone cannot detect reordered duplicate reductions. This deliberately
// noncommutative caller checks every byte and the exact combiner call count.
template<class Table> void packet_history(Table& table) {
    auto expected=contents(table);const auto old=expected;
    auto retained=table.clone();require(bool(retained),"packet retained owner");
    struct prefix_effects {
        unsigned left,writes=0,commits=0;mutation_delta delta{};
        std::size_t remaining()const{return left?SIZE_MAX:0;}
        void before(const small_event& e)noexcept {
            writes+=e.size!=0;
            if(e.type==small_event::kind::caller_delta) {
                require(left&&writes,"admitted packet damage precedes its delta");
                --left;++commits;writes=0;delta.keys+=e.delta.keys;delta.values+=e.delta.values;
            }
        }
    };
    for(unsigned limit=0;limit<=5;++limit) {
        std::array<word,8> keys;std::array<value,8> values;
        for(unsigned i=0;i<8;++i){keys[i]=key(i%3?0:255);values[i]=input(i,limit+51);}
        constexpr unsigned active=0xad; // sparse, repeated and initially absent
        unsigned calls=0;
        auto combine=[&](const value& a,const value& b)noexcept {
            ++calls;value out;
            for(unsigned j=0;j<W;++j)out[j]=byte(3u*a[j]+b[j]+j+1);return out;
        };
        unsigned want=0,left=limit,want_calls=0,new_keys=0;
        auto remember=[&](unsigned mask) {
            for(unsigned i=0;i<8;++i)if(mask&(1u<<i)) {
                auto [at,inserted]=expected.emplace(keys[i],values[i]);
                if(inserted)++new_keys;
                else {++want_calls;for(unsigned j=0;j<W;++j)
                    at->second[j]=byte(3u*at->second[j]+values[i][j]+j+1);}
            }
        };
        for(unsigned i=0;i<8;++i)if((active&(1u<<i))&&left){want|=1u<<i;--left;}
        prefix_effects effects{limit};
        const auto done=table.template combineN_bytes<8>(keys,values,active|0xff00,combine,effects);
        remember(want);
        require(done==want&&calls==want_calls&&contents(table)==expected,"ordered packet prefix and full-byte reduction");
        require(effects.commits==std::popcount(want)&&effects.delta.keys==new_keys&&effects.delta.values==new_keys,
            "packet publishes exactly accepted occupancy changes");
        const auto before=calls;
        require(!table.template combineN_bytes<8>(keys,values,0,combine,effects)&&calls==before,
            "empty packet never invokes reducer");
        no_small_effects rest;const auto retry=active&~done;
        require(table.template combineN_bytes<8>(keys,values,retry,combine,rest)==retry,"packet resumes remaining lanes");
        remember(retry);require(calls==want_calls&&contents(table)==expected,"retry reduces exactly once");
    }
    require(contents(*retained)==old,"packet does not change retained owner");
}
void check(oa::Pool& pool) {
    for(unsigned initial:{0u,1u,3u,7u,15u,31u,63u,127u,240u})
    for(unsigned active:{0u,0xadu,255u})for(unsigned budget:{0u,16u,128u}) {
        Small table(pool,19);std::map<word,value> expected;
        for(unsigned i=0;i<initial;++i){auto v=input(i,31);table.set_bytes(key(i),v);expected[key(i)]=v;}
        const auto old=expected;auto retained=table.clone();require(bool(retained),"growth packet retained owner");
        std::array<word,8> keys;std::array<value,8> values;
        constexpr std::array<unsigned,8> offsets{0,0,1,2,0,3,1,0};
        for(unsigned i=0;i<8;++i){keys[i]=key(initial+offsets[i]);values[i]=input(i,37);}
        if(initial)keys[2]=key(0); // Existing rank survives later relocation.
        if constexpr(K<64)keys[7]|=word(1)<<K;
        unsigned calls=0,want_calls=0;
        auto reduce=[&](const value& a,const value& b)noexcept {++calls;value out;
            for(unsigned j=0;j<W;++j)out[j]=byte(3u*a[j]+b[j]+j+1);return out;};
        auto remember=[&](unsigned lanes) {
            for(unsigned i=0;i<8;++i)if(lanes&(1u<<i)) {
                auto [at,inserted]=expected.emplace(keys[i]&mask(K),values[i]);
                if(!inserted){++want_calls;for(unsigned j=0;j<W;++j)at->second[j]=byte(3u*at->second[j]+values[i][j]+j+1);}
            }
        };
        std::array<small_event,128> events;small_journal effects{std::span<small_event>(events.data(),budget)};
        const auto done=table.combineN_bytes<8>(keys,values,active,reduce,effects);
        require(!(done&~active),"growth packet inactive lanes");
        bool refused=false;
        for(unsigned i=0;i<8;++i)if(active&(1u<<i)) {
            if(done&(1u<<i))require(!refused,"growth packet ordered prefix");else refused=true;
        }
        remember(done);require(calls==want_calls&&contents(table)==expected,"growth packet exact committed values");
        no_small_effects rest;const auto retry=active&~done;
        require(table.combineN_bytes<8>(keys,values,retry,reduce,rest)==retry,"growth packet retry");
        remember(retry);require(calls==want_calls&&contents(table)==expected&&contents(*retained)==old,
            "growth preserves existing ranks, new duplicate ranks, retained owner and exactly-once reduction");
        std::array<value,8> read{};unsigned wanted=0;
        const auto found=table.getN_bytes<8>(keys,active,[&](unsigned lane,auto bytes){std::copy(bytes.begin(),bytes.end(),read[lane].begin());});
        for(unsigned i=0;i<8;++i)if(active&(1u<<i))if(auto at=expected.find(keys[i]&mask(K));at!=expected.end()) {
            wanted|=1u<<i;require(read[i]==at->second,"shared lookup complete-byte lane oracle");
        }
        require(found==wanted,"shared lookup active membership");
    }
    Small list(pool,19);history(list,127);
    packet_history(list);
    auto body=Body::promote(list,pool,31,97);require(bool(body),"combine promotion");
    const auto expected=contents(*body);
    auto migration=hierarchy_migration<Body,Bigger>::prepare(body,719,331);
    require(bool(migration),"combine migration preparation");
    while(!migration->ready())require(migration->step(7).state!=record_scan_state::refused,"bounded combine migration");
    auto bigger=migration->finish();require(contents(bigger)==expected,"combined bytes survive reseeding");
    Small empty(pool,33);auto crowded=Crowded::promote(empty,pool,51,73);require(bool(crowded),"crowded combine owner");
    history(*crowded,251);require(crowded->population()[2]>0,"combine exercises linked key overflow");
    packet_history(*crowded);
}

template<bool Exact> struct Baseline {
    using key_type=typename small_abseil<K,container_kind::map,Exact>::key_type;
    absl::flat_hash_map<key_type,value> storage;
    void reserve(unsigned n){storage.reserve(n);}
    [[gnu::always_inline]] void add(word k,const value& v) {
        std::pair<typename decltype(storage)::iterator,bool> result;
        [[clang::always_inline]] result=storage.try_emplace(key_type(k),v);
        if(!result.second)result.first->second=xor_value{}(result.first->second,v);
    }
    template<class Visit>void each_bytes(Visit&& visit)const {
        for(const auto& [k,v]:storage) {
            if constexpr(Exact)visit(k.value(),v);else visit(word(k),v);
        }
    }
    memory footprint()const{return {absl::container_internal::AllocatedByteSize(storage),0,sizeof(*this)};}
};
struct row {word key;value bytes;};
template<bool Fused,class Table> [[gnu::noinline]] void update(Table& table,std::span<const row> rows) {
    no_small_effects effects;
    for(const auto& r:rows) {
        if constexpr(requires{table.storage;})table.add(r.key,r.bytes);
        else if constexpr(Fused)require(bool(table.try_combine_bytes(r.key,r.bytes,xor_value{},effects)),"fused update");
        else {
            value next=r.bytes;table.get_bytes(r.key,[&] [[gnu::always_inline]] (auto bytes){
                value old;std::copy(bytes.begin(),bytes.end(),old.begin());next=xor_value{}(old,r.bytes);});
            table.set_bytes(r.key,next);
        }
    }
}
template<bool Fused,class Table> void measure(Table& t,const char* name,unsigned n,unsigned rep) {
    std::vector<row> rows;rows.reserve(n*32);
    std::map<word,value> expected;
    for(unsigned pass=0;pass<32;++pass)for(unsigned i=0;i<n;++i)rows.push_back({key(i),input(i,pass)});
    std::mt19937_64 rng(733+rep);std::shuffle(rows.begin(),rows.end(),rng);
    for(const auto& r:rows) {
        auto [at,inserted]=expected.try_emplace(r.key,r.bytes);
        if(!inserted)for(unsigned j=0;j<W;++j)at->second[j]^=r.bytes[j];
    }
    // One admitted empty-to-populated history includes first insert and all
    // later combines. Initial owner construction/reservation and destruction
    // are outside this update-path diagnostic; this is not a lifetime score.
    auto begin=clock_type::now();update<Fused>(t,rows);
    const auto ns=std::chrono::duration<double,std::nano>(clock_type::now()-begin).count()/rows.size();
    require(contents(t)==expected,"timed mutation complete-byte oracle");const auto m=t.footprint();
    std::printf("%u,%u,%s,%u,%u,%zu,%.6f,%llu,%llu,%llu\n",K,W,name,n,rep,rows.size(),ns,
        (unsigned long long)m.requested,(unsigned long long)m.backing,(unsigned long long)m.metadata);
}
void bench(oa::Pool& pool,unsigned reps) {
    std::puts("K,W,owner,keys,rep,operations,ns_per_mutation,requested,backing,metadata");
    for(unsigned n:{16u,128u,251u})for(unsigned rep=0;rep<reps;++rep)
      for(unsigned arm=0;arm<6;++arm)switch((rep+arm)%6) {
        case 0:{Small t(pool,17);measure<true>(t,"list_fused",n,rep);break;}
        case 1:{Small t(pool,17);measure<false>(t,"list_double",n,rep);break;}
        case 2:{Small s(pool,17);auto t=BenchBody::promote(s,pool,31,97);require(bool(t),"benchmark body");
            measure<true>(*t,"hierarchy_fused",n,rep);break;}
        case 3:{Small s(pool,17);auto t=BenchBody::promote(s,pool,31,97);require(bool(t),"benchmark body");
            measure<false>(*t,"hierarchy_double",n,rep);break;}
        case 4:{Baseline<false> t;t.reserve(n);measure<true>(t,"abseil",n,rep);break;}
        default:{Baseline<true> t;t.reserve(n);measure<true>(t,"abseil_exact",n,rep);break;}
      }
}
}
int main(int argc,char** argv) {
    ht::pool_owner pool;ht::combine_test::check(*pool.pool);
    if(argc>1 && std::string_view(argv[1])=="bench")
        ht::combine_test::bench(*pool.pool,argc>2?std::stoul(argv[2]):7);
    else std::puts("Fused opaque combine: complete bytes, deltas, refusal, erase, retained copy, overflow and reseed pass");
}
