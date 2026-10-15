#include "promoted.h"
#include "small_baseline.h"
#include "benchmark_oracle.h"
#include <absl/container/internal/hashtable_debug.h>
#include <map>
#include <numeric>
#include <random>
#include <string>
#ifndef HT_VALUE_BYTES
#define HT_VALUE_BYTES 2
#endif
#ifndef HT_VALUE_KEY
#define HT_VALUE_KEY 40
#endif
#ifndef HT_GEOMETRY_KIND
#define HT_GEOMETRY_KIND 1
#endif
#ifndef HT_VALUE_DIRECT
#define HT_VALUE_DIRECT 0
#endif
#ifndef HT_OCCUPANCY_STUDY
#define HT_OCCUPANCY_STUDY 0
#endif
#ifndef HT_VALUE_ROWS
#define HT_VALUE_ROWS 0
#endif
namespace ht {
constexpr unsigned W=HT_VALUE_BYTES,K=HT_VALUE_KEY;
#if defined(HT_GEOMETRY_POLICIES)
// Keep cardinality independent of the physical hierarchy. K8 belongs to the
// whole-table list study; these controls start at 384 distinct keys.
static_assert(K>=16 && (HT_GEOMETRY_KIND==0 || HT_GEOMETRY_KIND==1 || (HT_OCCUPANCY_STUDY && HT_GEOMETRY_KIND==2)));
static_assert((HT_GEOMETRY_KIND==0)==(W==0));
#ifdef HT_BAG_POLICIES
#error Geometry and bag-policy controls are separate panels
#endif
struct geometry_shape {int bucket_shift;unsigned unit,secondary_shift=4,secondary_unit=0,slots=0,secondary_slots=0,quantum=8;};
#ifdef HT_CAPACITY_FAMILIES
// Capacity and bucket-count families, with byte extents solved for each domain,
// value/tag width and growth level. Eight-byte and cache-line padding are paired
// controls, not caller-visible options. The first two are historical geometries.
constexpr std::array geometry_shapes{
    geometry_shape{0,128},geometry_shape{0,192},
    geometry_shape{0,0,2,0,7,3,8},geometry_shape{-1,0,2,0,14,5,8},
    geometry_shape{-2,0,2,0,28,8,8},
    geometry_shape{0,0,2,0,7,3,64},geometry_shape{-1,0,2,0,14,5,64},
    geometry_shape{-2,0,2,0,28,8,64}};
#elif defined(HT_COUPLED_GEOMETRY)
// Same complete histories, jointly varying both tiers instead of freezing a
// 1:16 secondary ratio and matching units. The first two arms retain the crowded
// and roomier controls. None of these choices is an interface option.
constexpr std::array geometry_shapes{
    geometry_shape{0,128},geometry_shape{0,192},
    geometry_shape{0,168,2,96},geometry_shape{0,168,3,128},
    geometry_shape{0,160,2,96},geometry_shape{0,176,1,64},
    geometry_shape{0,168,2,64},geometry_shape{0,168,1,64},
    geometry_shape{0,160,2,128},geometry_shape{0,192,3,64}};
#else
constexpr std::array geometry_shapes{
    geometry_shape{0,128},geometry_shape{-1,128},geometry_shape{-2,128},geometry_shape{1,128},
    geometry_shape{0,96},geometry_shape{-1,96},geometry_shape{0,192},geometry_shape{-1,192},
    geometry_shape{0,64},geometry_shape{1,64},geometry_shape{-2,96},geometry_shape{1,96},
    geometry_shape{-2,192},geometry_shape{1,192},geometry_shape{-1,64},geometry_shape{-2,64}};
#endif
constexpr unsigned baseline_arm=geometry_shapes.size(),arm_count=baseline_arm+2;
#elif defined(HT_BAG_POLICIES)
// Coupled physical controls. These are benchmark arms, not interface presets.
struct bag_shape {unsigned budget,unit,block_slots;bool buffers=false;int bucket_shift=0;bool vector_prefix=false;};
#ifdef HT_VECTOR_BAG_POLICIES
// Each triple isolates external storage at the same inline/bucket geometry.
// W32/W64 use enough buckets to avoid a word-sized map defining the layout.
constexpr unsigned vector_budget=W<=8?16u:64u;
constexpr unsigned vector_unit=W<=8?192u:320u;
constexpr int vector_shift=W<=8?0:1;
constexpr std::array shapes{
    bag_shape{vector_budget,vector_unit,16,false,vector_shift},
    bag_shape{vector_budget,vector_unit,0,true,vector_shift},
    bag_shape{vector_budget,vector_unit,16,false,vector_shift,true},
    bag_shape{vector_budget,vector_unit,256,false,vector_shift},
    bag_shape{vector_budget,vector_unit,256,false,vector_shift,true},
    bag_shape{32,W<=8?256u:192u,16,false,W<=8?0:2},
    bag_shape{32,W<=8?256u:192u,0,true,W<=8?0:2},
    bag_shape{32,W<=8?256u:192u,16,false,W<=8?0:2,true}};
#elif defined(HT_WIDE_BAG_POLICIES)
// Coupled controls keep useful capacity as records widen. Compare identical
// histories; no geometry is a growth policy or selected platform preset.
constexpr std::array shapes{
    bag_shape{8,128,16}, bag_shape{16,192,16}, bag_shape{32,192,16,false,1},
    bag_shape{64,96,16,false,3}, bag_shape{64,128,16,false,3},
    bag_shape{64,160,16,false,2}, bag_shape{64,192,16,false,2},
    bag_shape{64,256,16,false,2}, bag_shape{64,320,16,false,1},
    bag_shape{64,320,256,false,1}, bag_shape{64,320,0,true,1}};
#else
constexpr std::array shapes{
    bag_shape{8,128,16}, bag_shape{16,192,16}, bag_shape{24,256,16},
    bag_shape{32,256,16}, bag_shape{32,320,16},
    bag_shape{16,192,64}, bag_shape{16,192,256}, bag_shape{16,192,0,true}};
#endif
constexpr unsigned baseline_arm=shapes.size(),arm_count=baseline_arm+2;
inline unsigned row_fanout(unsigned i,unsigned uniform) {
    if(uniform)return uniform;
    // Deterministic mixed fanout: 75% singleton, 15% pair, 6% four,
    // 3% sixteen and 1% sixty-four in expectation. Every key is updated once.
    const auto x=mix(i+317)%100;
    return x<75?1:x<90?2:x<96?4:x<99?16:64;
}
#else
constexpr unsigned baseline_arm=3,arm_count=5;
#endif
using value_bytes=std::array<byte,W>;
using scalar=std::conditional_t<W==1,std::uint8_t,std::conditional_t<W==2,std::uint16_t,
    std::conditional_t<W==4,std::uint32_t,std::conditional_t<W==8,word,value_bytes>>>>;
static_assert(W==0 || sizeof(scalar)==W);
inline value_bytes value(unsigned i,unsigned j,unsigned fanout,unsigned step) {
    value_bytes result{};
    auto part=[&](unsigned n){value_bytes out;for(unsigned b=0;b<W;++b)out[b]=byte(mix(word(i)+(word(n)<<32)+719*b));return out;};
    if(j+1<fanout)return part(j);
    // Wide values vary in every byte while their complete XOR digest names the
    // next dependent rank. Narrow values still contain varied rank deltas.
    result=part(j);word correction=step;
    for(unsigned b=0;b<W;++b)correction^=word(result[b])<<((b%8)*8);
    for(unsigned n=0;n<j;++n){auto x=part(n);for(unsigned b=0;b<W;++b)correction^=word(x[b])<<((b%8)*8);}
    for(unsigned b=0;b<std::min(W,8u);++b)result[b]^=byte(correction>>(b*8));return result;
}
inline word digest(std::span<const byte,W> bytes) {
    word result=0;lanes<W/8>([&](auto i){word v;std::memcpy(&v,bytes.data()+8*i,8);result^=v;});
    if constexpr(W%8){word v=0;std::memcpy(&v,bytes.data()+W/8*8,W%8);result^=v;}return result;
}
inline word key(unsigned i){
    const auto x=word(i)*0xd6e8feb86659fd93ULL;
    // Match full-count definitions in both the query producer and permutation
    // for this encoding comparison. A producer MOVB/BZHI can otherwise inherit
    // the previous selector result and confound an unrelated layout change.
    if constexpr(HT_OCCUPANCY_STUDY)return hash_mask<K>(x);
    else return x&mask(K);
}
template<container_kind Kind,bool Exact> struct baseline {
    using key_type=std::conditional_t<Exact,hashtable_spike::exact_unsigned_key<K/8>,typename abseil<K>::key_type>;
    using bag=absl::InlinedVector<scalar,1>;
    using mapped=std::conditional_t<Kind==container_kind::multimap,bag,scalar>;
    using table=std::conditional_t<Kind==container_kind::set,absl::flat_hash_set<key_type>,absl::flat_hash_map<key_type,mapped>>;
    table storage;
    void reserve(unsigned n){storage.reserve(n);}
    [[gnu::always_inline]] void set_bytes(word k,const value_bytes& bytes) {
        const auto v=std::bit_cast<scalar>(bytes);
        if constexpr(Kind==container_kind::set){[[clang::always_inline]] storage.insert(key_type(k));}
        else if constexpr(Kind==container_kind::map){[[clang::always_inline]] storage.insert_or_assign(key_type(k),v);}
        else {std::pair<typename table::iterator,bool> at;[[clang::always_inline]] at=storage.try_emplace(key_type(k));at.first->second.push_back(v);}
    }
    template<class F> [[gnu::always_inline]] bool get_bytes(word k,F&& f)const {
        typename table::iterator at;[[clang::always_inline]] at=const_cast<table&>(storage).find(key_type(k));
        if(at==storage.end())return false;
        if constexpr(Kind==container_kind::set){const value_bytes empty{};f(std::span<const byte,W>(empty));}
        else if constexpr(Kind==container_kind::map)f(std::span<const byte,W>(reinterpret_cast<const byte*>(&at->second),W));
        else for(const auto& v:at->second)f(std::span<const byte,W>(reinterpret_cast<const byte*>(&v),W));return true;
    }
    void erase(word k){typename table::iterator at;[[clang::always_inline]] at=storage.find(key_type(k));if(at!=storage.end())storage.erase(at);}
    template<class F> void each_bytes(F&& f)const {
        if constexpr(Kind==container_kind::set)for(const auto& k:storage) {
            const word decoded=[&]{if constexpr(Exact)return k.value();else return word(k);}();f(decoded,value_bytes{});
        }else for(const auto& [k,v]:storage){const word decoded=[&]{if constexpr(Exact)return k.value();else return word(k);}();
            if constexpr(Kind==container_kind::map){auto b=std::bit_cast<value_bytes>(v);f(decoded,b);}
            else for(auto x:v){auto b=std::bit_cast<value_bytes>(x);f(decoded,b);}}
    }
    memory footprint()const {
        word bytes=absl::container_internal::AllocatedByteSize(storage);
        if constexpr(Kind==container_kind::multimap)for(const auto& [k,v]:storage){auto p=reinterpret_cast<std::uintptr_t>(v.data()),o=reinterpret_cast<std::uintptr_t>(&v);
            if(p<o||p>=o+sizeof(v))bytes+=v.capacity()*W;}
        return {bytes,0,sizeof(*this)};
    }
};
template<class Body> auto contents(const Body& body) {
    std::map<word,std::vector<value_bytes>> result;body.each_bytes([&](word k,auto bytes){value_bytes v;std::copy(bytes.begin(),bytes.end(),v.begin());result[k].push_back(v);});
    for(auto& [k,v]:result)std::sort(v.begin(),v.end());return result;
}
template<class F> double timed(F&& f){auto before=clock_type::now();f();return std::chrono::duration<double,std::nano>(clock_type::now()-before).count();}
template<container_kind Kind,unsigned ReferenceA,unsigned Arm> void trial(oa::Pool& pool,unsigned rep,unsigned fanout,unsigned requested_n=0,word seed=41) {
#ifdef HT_GEOMETRY_POLICIES
    constexpr auto geometry=geometry_shapes[Arm<baseline_arm?Arm:0];
    constexpr unsigned A=unsigned(int(ReferenceA)+geometry.bucket_shift),B=A-geometry.secondary_shift;
#ifdef HT_COUPLED_GEOMETRY
#ifdef HT_CAPACITY_FAMILIES
    // A wider-than-budget value is external; an unused 16-byte payload would
    // buy no inline capacity for W32/W64. Keep the eight-byte descriptor there.
    constexpr unsigned Budget=Kind==container_kind::multimap && W<=16?16:8;
#else
    constexpr unsigned Budget=Kind==container_kind::multimap?16:8;
#endif
    using Bags=linked_bags<(W?W:8),true,256>;
#else
    constexpr unsigned Budget=8;
    using Bags=linked_bags<(W?W:8),true>;
#endif
    using GeometrySource=small_table<K,Kind,W,true,Bags,Budget>;
    using GeometryIndex=record_table<K,Kind,GeometrySource::stored_bytes,A,B,128,128,
        record_format::exact,false,record_access::exact,GeometrySource::tag_bits>;
    constexpr unsigned P=geometry.slots?GeometryIndex::unit_for_slots(geometry.slots,false,geometry.quantum):geometry.unit;
    constexpr unsigned Q=geometry.secondary_slots?GeometryIndex::unit_for_slots(geometry.secondary_slots,true,geometry.quantum):
        geometry.secondary_unit?geometry.secondary_unit:P;
#else
#ifdef HT_BAG_POLICIES
    constexpr auto shape=shapes[Arm<baseline_arm?Arm:0];
    constexpr unsigned A=unsigned(int(ReferenceA)+shape.bucket_shift),Budget=shape.budget,P=shape.unit,B=A-4;
    using Bags=std::conditional_t<shape.buffers,buffer_bags<W>,
        std::conditional_t<shape.vector_prefix,pooled_vector_bags<W,true,shape.block_slots?shape.block_slots:16>,
            linked_bags<W,true,shape.block_slots?shape.block_slots:16>>>;
#else
    constexpr unsigned A=ReferenceA,Budget=Arm==0?8:16,P=Arm==2?192:128,B=A-4;
    using Bags=linked_bags<W,true>;
#endif
#endif
#ifndef HT_GEOMETRY_POLICIES
    constexpr unsigned Q=P;
#endif
    constexpr auto Format=HT_VALUE_ROWS?record_format::rows:record_format::exact;
    using Candidate=promoted_table<K,Kind,W,A,B,P,Q,Format,HT_VALUE_DIRECT,record_access::both,true,Bags,Budget>;
    using Body=std::conditional_t<(Arm<baseline_arm),Candidate,baseline<Kind,Arm==baseline_arm+1>>;
#ifdef HT_CAPACITY_FAMILIES
    const unsigned n=requested_n,rounds=std::max(8u,65536u/n);
#else
    constexpr unsigned n=6u<<ReferenceA,rounds=std::max(8u,65536u/n);
#endif
    std::vector<unsigned> order(n);std::iota(order.begin(),order.end(),0);std::mt19937 random(91);std::shuffle(order.begin(),order.end(),random);
    std::vector<unsigned> next(n+(Kind==container_kind::set));for(unsigned j=0;j<n;++j)next[order[j]]=order[(j+1)%n];
#ifdef HT_BAG_POLICIES
    std::vector<unsigned> offsets(n+1);
    for(unsigned i=0;i<n;++i)offsets[i+1]=offsets[i]+row_fanout(i,fanout);
    std::vector<value_bytes> input(offsets.back()),updates(n);
#else
    std::vector<value_bytes> input(std::size_t(n)*fanout),updates(n);
#endif
    std::map<word,std::vector<value_bytes>> expected;
    for(unsigned i=0;i<n;++i){
#ifdef HT_BAG_POLICIES
        const unsigned count=offsets[i+1]-offsets[i],begin=offsets[i];
#else
        const unsigned count=fanout;const auto begin=std::size_t(i)*fanout;
#endif
        for(unsigned j=0;j<count;++j){auto v=value(i,j,count,(next[i]+n-i)%n);input[begin+j]=v;expected[key(i)].push_back(v);}
        updates[i]=value(i,0,2,0);std::sort(expected[key(i)].begin(),expected[key(i)].end());}
    // Allocate every independent phase model before constructing either owner.
    // Keeping an untimed allocating validator between phases biases malloc/free.
    auto updated_expected=expected;
    for(unsigned i=0;i<n;++i){auto& v=updated_expected.at(key(i));
        if constexpr(Kind!=container_kind::multimap)v={updates[i]};else v.push_back(updates[i]);}
    auto erased_expected=updated_expected;
    for(unsigned i=0;i<n;i+=2)erased_expected.erase(key(i));
    benchmark_oracle<value_bytes> built_oracle(expected),updated_oracle(updated_expected),erased_oracle(erased_expected);
#if HT_OCCUPANCY_STUDY
    std::vector<unsigned> live,removed;for(auto i:order)(i&1?live:removed).push_back(i);
    auto reused_expected=erased_expected;for(auto i:removed)reused_expected[key(i)]={updates[i]};
    benchmark_oracle<value_bytes> reused_oracle(reused_expected);
#endif
    std::optional<Body> body;
#ifdef HT_VECTOR_BAG_POLICIES
    // Untimed Pool snapshots explain reservation/growth policy. These totals
    // include idle inventory and are neither per-container RSS nor copy bytes.
    std::array<oa::Statistics,6> pool_states;
#endif
    auto construct=timed([&]{if constexpr(Arm<baseline_arm){small_table<K,Kind,W,true,Bags,Budget> source(pool,19);body=Candidate::promote(source,pool,seed,seed+32);require(bool(body),"value benchmark construct");}
        else {body.emplace();body->reserve(n);}});
#ifdef HT_VECTOR_BAG_POLICIES
    pool_states[0]=oa::statistics(pool);
#endif
#if HT_OCCUPANCY_STUDY
    const auto constructed=body->footprint();
#endif
#ifdef HT_BAG_POLICIES
    auto build=timed([&]{for(auto i:order)for(unsigned j=offsets[i];j<offsets[i+1];++j)body->set_bytes(key(i),input[j]);});
#else
    auto build=timed([&]{for(auto i:order)for(unsigned j=0;j<fanout;++j)body->set_bytes(key(i),input[std::size_t(i)*fanout+j]);});
#endif
    auto initial=body->footprint();std::array<word,3> population{};if constexpr(Arm<baseline_arm)population=body->population();
#ifdef HT_VECTOR_BAG_POLICIES
    pool_states[1]=oa::statistics(pool);
#endif
    built_oracle.check_bytes(*body);
    word expected_sum=0;
    for(unsigned i=0;i<2*n;++i){
        built_oracle.check_key(key(i),[&](auto&& emit){return body->get_bytes(key(i),[&](auto bytes){
            value_bytes v;std::copy(bytes.begin(),bytes.end(),v.begin());emit(v);
        });});
        if(i<n){++expected_sum;word step=0;for(const auto& v:expected.at(key(i))){expected_sum+=digest(v);step^=digest(v);}
            if constexpr(Kind!=container_kind::set)require(step==(next[i]+n-i)%n,"dependent graph edge independently checked");}}
    expected_sum*=rounds;
    auto reads=[&](bool misses){
        word reduced=0;
        for(unsigned r=0;r<rounds;++r)for(auto i:order)
            reduced+=body->get_bytes(key(i+(misses?n:0)),[&] [[gnu::always_inline]] (auto bytes){reduced+=digest(bytes);});
        return reduced;
    };
    auto dependent=[&]{word reduced=0;unsigned rank=order[0];for(unsigned i=0;i<rounds*n;++i){word step=0;
        const bool found=body->get_bytes(key(rank),[&] [[gnu::always_inline]] (auto bytes){auto v=digest(bytes);step^=v;reduced+=v;});reduced+=found;
        // Local AArch64 inspection shows constant hit results synthesized on
        // predicted branches. This set traversal is ordered, not qualified
        // serial latency. Maps retain the actual value-dependent cycle.
        if constexpr(Kind==container_kind::set)rank=next[rank+unsigned(!found)];
        else {
#ifdef HT_CAPACITY_FAMILIES
            // The checked graph stores a step in [0,n). Keep this one-wrap
            // operation cheap when population is a runtime workload input.
            const auto advanced=rank+step;rank=advanced<n?advanced:advanced-n;
#else
            rank=(rank+step)%n;
#endif
        }}return reduced;};
    require(reads(false)==expected_sum&&reads(true)==0&&dependent()==expected_sum,"value benchmark reduction oracle");
    word sum=0,result=0;auto hit=timed([&]{result=reads(false);});require(result==expected_sum,"timed byte hit");sum+=result;
    auto miss=timed([&]{result=reads(true);});require(result==0,"timed byte miss");
    auto serial=timed([&]{result=dependent();});require(result==expected_sum,"timed byte serial");sum+=result;
    auto update=timed([&]{for(auto i:order)body->set_bytes(key(i),updates[i]);});auto updated=body->footprint();
#ifdef HT_VECTOR_BAG_POLICIES
    pool_states[2]=oa::statistics(pool);
#endif
    updated_oracle.check_bytes(*body);
    auto erase=timed([&]{for(unsigned i=0;i<n;i+=2)body->erase(key(i));});
    erased_oracle.check_bytes(*body);const auto erased=body->footprint();auto after=erased;
#ifdef HT_VECTOR_BAG_POLICIES
    pool_states[3]=oa::statistics(pool);
#endif
#if HT_OCCUPANCY_STUDY
    auto check_points=[&](auto& oracle) {
        for(unsigned i=0;i<2*n;++i)oracle.check_key(key(i),[&](auto&& emit) {
            return body->get_bytes(key(i),[&](auto bytes){value_bytes v;std::copy(bytes.begin(),bytes.end(),v.begin());emit(v);});
        });
    };
    check_points(erased_oracle);
    auto aged=[&](const auto& queries){word reduced=0;
        for(unsigned r=0;r<rounds;++r)for(auto i:queries)
            reduced+=body->get_bytes(key(i),[&] [[gnu::always_inline]] (auto bytes){reduced+=digest(bytes);});
        return reduced;};
    auto expected_reduction=[&](const auto& phase){word out=phase.size();for(const auto& [k,values]:phase)for(const auto& v:values)out+=digest(v);return out*rounds;};
    const word aged_sum=expected_reduction(erased_expected);
    auto aged_hit=timed([&]{result=aged(live);});require(result==aged_sum,"aged hit oracle");sum+=result;
    auto aged_miss=timed([&]{result=aged(removed);});require(result==0,"aged deleted-key miss oracle");
    auto reuse=timed([&]{for(auto i:removed)body->set_bytes(key(i),updates[i]);});
#ifdef HT_VECTOR_BAG_POLICIES
    pool_states[4]=oa::statistics(pool);
#endif
    reused_oracle.check_bytes(*body);after=body->footprint();
    check_points(reused_oracle);
    const word reused_sum=expected_reduction(reused_expected);
    auto reused_hit=timed([&]{result=aged(order);});require(result==reused_sum,"reused hit oracle");sum+=result;
#endif
    auto release=timed([&]{body.reset();});
#ifdef HT_VECTOR_BAG_POLICIES
    pool_states[5]=oa::statistics(pool);
#endif
    asm volatile(""::"r"(sum):"memory");
    auto emit=[&](const char* phase,double ns,unsigned operations,memory m){std::printf("%u,%u,%u,%u,%u,%u,%u,%u,%s,%u,%.3f,%llu,%llu,%llu,%u,%u,%llu,%llu,%llu",
        Arm,K,W,unsigned(Kind),A,n,rep,fanout,phase,operations,ns,(unsigned long long)m.requested,(unsigned long long)m.backing,(unsigned long long)m.metadata,
        Kind==container_kind::multimap&&Arm<baseline_arm?Budget/(W?W:1):0,Kind==container_kind::multimap&&Arm>=baseline_arm?unsigned(typename baseline<Kind,false>::bag{}.capacity()):0,
        (unsigned long long)population[0],(unsigned long long)population[1],(unsigned long long)population[2]);
#if defined(HT_GEOMETRY_POLICIES) || defined(HT_WIDE_BAG_POLICIES) || defined(HT_VECTOR_BAG_POLICIES)
        using Source=small_table<K,Kind,W,true,Bags,Budget>;
        using Index=record_table<K,Kind,Source::stored_bytes,A,B,P,Q,Format,false,record_access::exact,Source::tag_bits>;
        std::printf(",%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu",ReferenceA,Arm<baseline_arm?B:0,Arm<baseline_arm?P:0,
            Arm<baseline_arm?Index::primary.shape.slots:0,Arm<baseline_arm?Index::secondary.shape.slots:0,
            (unsigned long long)(Arm<baseline_arm?Index::primary.used_bytes:0),
            (unsigned long long)(Arm<baseline_arm?Index::primary.padding_bytes:0),
            (unsigned long long)(Arm<baseline_arm?Index::secondary.used_bytes:0),
            (unsigned long long)(Arm<baseline_arm?Index::secondary.padding_bytes:0));
#endif
#ifdef HT_COUPLED_GEOMETRY
        std::printf(",%u",Arm<baseline_arm?Q:0);
#endif
#ifdef HT_CAPACITY_FAMILIES
        std::printf(",%llu",(unsigned long long)seed);
#endif
#ifdef HT_VECTOR_BAG_POLICIES
        const std::string_view at=phase;
        const auto& ps=pool_states[at=="construct"?0:at=="update"?2:
            (at=="erase"||at=="aged_hit"||at=="aged_miss")?3:
            (at=="reuse"||at=="reused_hit")?4:(at=="release"||at=="lifecycle")?5:1];
        std::printf(",%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu",unsigned(HT_LINK_RESERVE_BLOCK),
            (unsigned long long)ps.allocations,(unsigned long long)ps.growths,(unsigned long long)ps.relocations,
            (unsigned long long)ps.mapped_bytes,(unsigned long long)ps.reserved_bytes,
            (unsigned long long)ps.metadata_bytes,(unsigned long long)ps.idle_bytes);
#endif
        std::putchar('\n');};
    emit("construct",construct,1,
#if HT_OCCUPANCY_STUDY
        constructed
#else
        initial
#endif
        );emit("build",build,n,initial);emit("hit",hit,rounds*n,initial);emit("miss",miss,rounds*n,initial);
    emit(Kind==container_kind::set?"ordered":"serial",serial,rounds*n,initial);emit("update",update,n,updated);emit("erase",erase,(n+1)/2,erased);
#if HT_OCCUPANCY_STUDY
    emit("aged_hit",aged_hit,rounds*(n/2),erased);emit("aged_miss",aged_miss,rounds*((n+1)/2),erased);
    emit("reuse",reuse,(n+1)/2,after);emit("reused_hit",reused_hit,rounds*n,after);
#endif
    emit("release",release,1,after);
    emit("lifecycle",construct+build+update+erase+release
#if HT_OCCUPANCY_STUDY
        +reuse
#endif
        ,n,after);
}
}
int main(int argc,char** argv){using namespace ht;
#ifdef HT_CAPACITY_FAMILIES
    require(argc==8,"capacity bench kind arm fanout large reps population seed");
    const unsigned requested_n=std::stoul(argv[6]);const word seed=std::stoull(argv[7]);
    require(requested_n>=2 && word(requested_n)*2<=(word(1)<<std::min(K,32u)),"capacity population fits key domain, queries and miss keys");
#else
    require(argc==6,"value bench kind arm fanout large reps");
#endif
    auto kind=std::stoul(argv[1]),arm=std::stoul(argv[2]),fanout=std::stoul(argv[3]),large=std::stoul(argv[4]),reps=std::stoul(argv[5]);
    require(kind<=2&&arm<arm_count&&fanout<=128&&(kind==2||fanout==1)&&large<2&&reps>0,"value bench arguments");
#if defined(HT_GEOMETRY_POLICIES)
    require(kind==HT_GEOMETRY_KIND,"geometry panel kind");
#elif defined(HT_BAG_POLICIES)
    require(kind==2,"coupled bag policy panel uses multimaps");
#else
    require(kind>0 && fanout>0,"uniform value fanout and map/multimap kind");
#endif
    std::printf("arm,K,value_bytes,kind,A,n,rep,fanout,phase,operations,ns,requested,owned_capacity,metadata,inline_values,abseil_inline_capacity,build_primary_rows,build_secondary_rows,build_overflow_rows");
#ifdef HT_GEOMETRY_POLICIES
    std::printf(",reference_A,B,bucket_bytes,primary_slots,secondary_slots,primary_used,primary_padding,secondary_used,secondary_padding");
#ifdef HT_COUPLED_GEOMETRY
    std::printf(",secondary_bucket_bytes");
#endif
#ifdef HT_CAPACITY_FAMILIES
    std::printf(",placement_seed");
#endif
    std::putchar('\n');
    for(unsigned rep=0;rep<reps;++rep){pool_owner pool;lanes<arm_count>([&](auto a){if(a!=arm)return;
#ifdef HT_CAPACITY_FAMILIES
        if(large)trial<container_kind(HT_GEOMETRY_KIND),10,a>(*pool.pool,rep,fanout,requested_n,seed);
        else trial<container_kind(HT_GEOMETRY_KIND),6,a>(*pool.pool,rep,fanout,requested_n,seed);
#else
        if(large)trial<container_kind(HT_GEOMETRY_KIND),10,a>(*pool.pool,rep,fanout);
        else trial<container_kind(HT_GEOMETRY_KIND),6,a>(*pool.pool,rep,fanout);
#endif
    });}
#else
#if defined(HT_WIDE_BAG_POLICIES) || defined(HT_VECTOR_BAG_POLICIES)
    std::printf(",reference_A,B,bucket_bytes,primary_slots,secondary_slots,primary_used,primary_padding,secondary_used,secondary_padding");
#endif
#ifdef HT_VECTOR_BAG_POLICIES
    std::printf(",reserve_block,pool_allocations,pool_growths,pool_relocations,pool_mapped_bytes,pool_reserved_bytes,pool_metadata_bytes,pool_idle_bytes");
#endif
    std::putchar('\n');
    for(unsigned rep=0;rep<reps;++rep){pool_owner pool;lanes<2>([&](auto c){if(c+1!=kind)return;
#ifdef HT_BAG_POLICIES
        if constexpr(c+1==2)
#endif
        lanes<arm_count>([&](auto a){if(a!=arm)return;
        if(large)trial<container_kind(c+1),10,a>(*pool.pool,rep,fanout);else trial<container_kind(c+1),6,a>(*pool.pool,rep,fanout);});});}
#endif
}
