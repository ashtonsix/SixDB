#pragma once

// Canonical experimental input and actual Abseil controls, unchanged from the
// maintained-probe fixture except owner aliases, scalar fill and owner helpers.
// Source fixture SHA256 8902210030a23dae7c6f443bd6be187447fdaf7f94e03435683a70840b7eb497.
// Exact fresh-owner and maintained-reader sources are pinned in the handoff;
// the owning lead's read-profile repairs remain in those consumer sources.
// No owner kernels are copied, converted or edited. Heap owners are kept stable
// by the ordinary caller, with no Descriptor/Pool capture or retention claim.
// Original experimental input and actual Abseil controls, curated from frozen A:
// capture 4dda02876e0962278680e42df620eedf3923aa02f40df0095f51896f54316e85,
// workbench/spikes/iceberg-swiss/compare.cpp SHA256
// 489d84e9e834738ea14212d42c0d4fbafa352250ddfdd083ed584873ba07275c.
// Query limit is an explicit argument; unused cohort/long-stream/environment,
// private Candidate, heat binding, allocation tracing and PMU drivers are omitted.
// The pure short/concentrated-input factory follows staged-probe/caller.cpp,
// SHA256 ab54ad172341a9d2000c17d2f07cfc11f8bb00a1eb6cb3308e1017cb7e2f7551.
// Conditioning's dependent-line walk is from conditioning.h SHA256
// cec6653ec36637270029ea42d6deb1565e7d66c7844ff82bb40872a60322d73e;
// its unused witness timer/output is omitted. No owner kernel is copied here.

#include "../multimap.h"
#include "../loom-reduction/consumer.h"
#include "../legacy/bags.h"
#include <loom/stream.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>
#include <absl/container/internal/hashtable_debug.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef IBS_MULTI
#define IBS_MULTI 0
#endif
#ifndef IBS_BAGS
#define IBS_BAGS 0
#endif

namespace study {
using byte=std::byte;
using ikea::hashtable::bytes;
using ikea::hashtable::each;
using ikea::hashtable::load;
using ikea::hashtable::mask;
using ikea::hashtable::permutation;
using ikea::hashtable::require;
using ikea::hashtable::word;

// These geometry/data definitions identify the experiment; no IBS layout or
// runtime tuning flag is forwarded into the maintained Ikea implementation.
inline constexpr bool Multi=IBS_MULTI, Bags=IBS_BAGS;
inline constexpr unsigned W=IBS_W,b=IBS_B,V=IBS_V,E=IBS_E?IBS_E:W-b;
inline constexpr unsigned independent_width=32;
static_assert(W>=1&&W<=64&&b<=W);
inline constexpr unsigned K=W-b;
static_assert(K==30&&E==30,"this composition fixes logical quotient K30");
inline constexpr unsigned packed_stored_bits=30,whole_stored_bits=32;
inline constexpr unsigned packed_header_bytes=64,whole_header_bytes=96;
inline constexpr auto maintained_kernel=write_arch::read_profile<K,V,Multi>().kernel;
// Set/map use the class instantiation measured for shared-store writes, with
// fully initialized rows. MM keeps pooled StreamPack capacity, coalesced whole
// value writes and the owner's default inline count (two V8 / one V64).
template<bool WholeBytes> using FreshOwner=std::conditional_t<Multi,
    write_arch::multimap<W,b,V,WholeBytes>,
    write_arch::table<W,b,V,WholeBytes,false,write_arch::upsert_flow::shared_stores>>;
using Packed=FreshOwner<false>;
using Whole=FreshOwner<true>;
using MaintainedHeap=std::conditional_t<Multi,
    ikea::hashtable::multimap<W,b,V,30,maintained_kernel,ikea::hashtable::heap_storage_policy>,
    ikea::hashtable::table<W,b,V,30,maintained_kernel,ikea::hashtable::heap_storage_policy>>;
// Geometry-only compatibility name for the preserved pure input factory.
using Candidate=Packed;
static_assert(Packed::N==16&&Whole::N==16&&MaintainedHeap::N==16);
using Value=bytes<V>;
using Key=std::conditional_t<(W<=8),std::uint8_t,std::conditional_t<(W<=16),std::uint16_t,std::conditional_t<(W<=32),std::uint32_t,word>>>;
using Bag=absl::InlinedVector<Value,1>;
using Abseil=std::conditional_t<V==0,absl::flat_hash_set<Key>,absl::flat_hash_map<Key,std::conditional_t<Multi,Bag,Value>>>;
// 0 preserves every procedure, 1 changes only the candidate's independent
// reduction, 2 applies the same loop choice to actual Abseil as a strong control.
using ReductionOwner=std::conditional_t<WRITE_ARCH_VALUEWISE_REDUCTION==2,Abseil,Packed>;

inline word digest(const Value& v) {
    word x=0;each<(V+7)/8>([&](auto c){constexpr unsigned width=std::min(8u,V-c*8);x^=load<width>(v.data()+c*8);});return x;
}
inline word random_word(word x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
// A permutation of the W-bit domain independent of the candidate's multiplier.
inline word key(word rank,word seed) {
    word x=(rank^seed)&mask<W>;
    x^=x>>std::max(1u,W/2);x=x*0xbf58476d1ce4e5b9ULL&mask<W>;
    x^=x>>std::max(1u,W/3);x=x*0x94d049bb133111ebULL&mask<W>;
    return x^(x>>std::max(1u,W/2));
}
struct result {word sum=0,hits=0,terminal=0;bool operator==(const result&)const=default;};

struct input {
    std::size_t n,requests;word seed;unsigned miss;
    std::vector<word> keys,request_keys;
    std::vector<std::uint32_t> next,order;
    std::vector<std::array<std::uint32_t,2>> narrow_edges;
  private:
    static std::size_t domain(std::size_t population,unsigned misses) {
        require(population&&population<(word{1}<<32)/(misses?2:1),"graph rank width");
        const auto count=population*(misses?2:1);
        if constexpr(W<64)require(count<=word{1}<<W,"graph fits domain");
        return count;
    }
  public:
    // Zero limit retains the whole population. make_input supplies the bounded
    // request count and preserves frozen A's balanced shuffle/reconnection.
    input(std::size_t n_,unsigned miss_,word seed_,std::size_t query_limit=0)
        :n(n_),requests(domain(n_,miss_)),seed(seed_),miss(miss_),keys(requests),next(requests),order(requests) {
        std::iota(order.begin(),order.end(),0);std::mt19937_64 rng(seed);std::shuffle(order.begin(),order.end(),rng);
        for(std::size_t i=0;i<requests;++i){next[order[i]]=order[(i+1)%requests];keys[i]=key(i,seed);}
        if(query_limit){require(query_limit>=32,"query sample must cover all lanes");
            if(order.size()>query_limit){
                if(miss){
                    const std::size_t wanted_hits=query_limit/2,wanted_misses=query_limit-wanted_hits;
                    std::size_t hits=0,misses=0,selected=0;
                    for(auto rank:order){
                        auto& count=rank<n?hits:misses;
                        if(count<(rank<n?wanted_hits:wanted_misses)){
                            ++count;order[selected++]=rank;
                            if(selected==query_limit)break;
                        }
                    }
                    require(selected==query_limit,"balanced query sample fits domain");
                    order.resize(query_limit);std::shuffle(order.begin(),order.end(),rng);
                }else order.resize(query_limit);
            }
            requests=order.size();
            for(std::size_t i=0;i<requests;++i)next[order[i]]=order[(i+1)%requests];
        }
        request_keys.reserve(order.size());for(auto rank:order)request_keys.push_back(keys[rank]);
        if constexpr(V<4){narrow_edges.resize(keys.size());for(std::size_t i=0;i<keys.size();++i){
            narrow_edges[i]={unsigned(rng()%keys.size()),unsigned(rng()%keys.size())};
            word edge=0;if(i<n)for(unsigned j=0;j<fanout(i);++j)edge^=digest(value(i,j));
            narrow_edges[i][i<n]=next[i]^unsigned(edge);
        }}
    }
    unsigned fanout(std::size_t i)const {
        if constexpr(!Multi||!Bags)return 1;
        const auto x=random_word(i+91)%100;return x<75?1:x<90?2:x<96?4:x<99?16:64;
    }
    Value raw_value(std::size_t i,unsigned entry)const {
        Value v{};for(unsigned j=0;j<V;++j)v[j]=byte(random_word(seed+i+j*0x9e3779b97f4a7c15ULL+word(entry)*0x243f6a8885a308d3ULL));return v;
    }
    Value value(std::size_t i,unsigned entry=0)const {
        Value v=raw_value(i,entry);
        if constexpr(V>=4)if(entry+1==fanout(i)){
            const word successor=V>=8&&!miss?keys[next[i]]:next[i];word delta=successor;
            for(unsigned j=0;j<=entry;++j)delta^=digest(raw_value(i,j));
            for(unsigned j=0;j<std::min(V,8u);++j)v[j]^=byte(delta>>(j*8));}
        return v;
    }
    result expected()const {result r;for(auto i:order)if(i<n){++r.hits;for(unsigned j=0;j<fanout(i);++j)r.sum+=digest(value(i,j));}return r;}
    result independent_expected()const{return expected();}
};

inline std::unique_ptr<input> make_input(std::size_t n,std::size_t count,word seed,unsigned miss,bool concentrated=false) {
    require(n&&n<(word{1}<<32)/(miss?2:1)&&count<=n*(miss?2:1),"query count fits original input domain");
    auto in=std::make_unique<input>(n,miss,seed,std::max<std::size_t>(32,count));
    if(count<32) {
        std::vector<unsigned> selected;
        unsigned hits=0,misses=0;
        for(auto rank:in->order) {
            auto& used=rank<n?hits:misses;
            const auto wanted=miss?(rank<n?count/2:count-count/2):count;
            if(used<wanted){selected.push_back(rank);++used;}
        }
        require(selected.size()==count,"short input count");
        in->order=std::move(selected);in->requests=count;in->request_keys.clear();
        for(std::size_t i=0;i<count;++i){in->next[in->order[i]]=in->order[(i+1)%count];in->request_keys.push_back(in->keys[in->order[i]]);}
    }
    if(concentrated) {
        const permutation<W,W-b> mix{random_word(seed)};
        for(std::size_t i=0;i<in->keys.size();++i)in->keys[i]=mix.recover(0,i);
        in->request_keys.clear();for(auto rank:in->order)in->request_keys.push_back(in->keys[rank]);
    }
    require(in->request_keys.size()==count&&in->requests==count,"actual issued input count");
    return in;
}

template<class Table> struct abseil_reader_base {
    const Table* table;
    struct staged_probe {word key;};
    template<class Consume> [[gnu::always_inline]] bool find(word k,Consume&& consume)const {
        auto it=table->find(Key(k));if(it==table->end())return false;
        if constexpr(Multi) {
            if constexpr(V==64&&requires{typename std::remove_cvref_t<Consume>::valuewise_tag;}) {
#pragma clang loop vectorize(disable)
                for(const auto& v:it->second)consume(v);
            }else for(const auto& v:it->second)consume(v);
        }
        else if constexpr(V)consume(it->second);return true;
    }
    [[gnu::always_inline]] void prefetch(word k)const{table->prefetch(Key(k));}
    template<class Emit> void prepare_staged(staged_probe& s,word k,Emit&&)const{s.key=k;prefetch(k);}
    // Frozen A's IBS_INLINE_BAG_ADVANCE=0 body. Both owners are compiled in the
    // caller TU with the explicitly recorded inline-threshold=1000 setting.
    template<class Consume,class Emit>
    bool advance(staged_probe& s,bool& found,Consume&& consume,Emit&&)const{found=find(s.key,consume);return true;}
};
using abseil_reader=abseil_reader_base<Abseil>;

// Logical request auditing only; the caller keeps each heap owner stable.
template<class Read> struct audit_reader {
    Read read;std::unordered_map<word,unsigned>* visits;
    using staged_probe=typename Read::staged_probe;
    template<class Consume> bool find(word k,Consume&& consume)const{++(*visits)[k];return read.find(k,std::forward<Consume>(consume));}
    void prefetch(word k)const{read.prefetch(k);}
    template<class Emit> void prepare_staged(staged_probe& s,word k,Emit&& emit)const{++(*visits)[k];read.prepare_staged(s,k,emit);}
    template<class Consume,class Emit> bool advance(staged_probe& s,bool& found,Consume&& consume,Emit&& emit)const{return read.advance(s,found,consume,emit);}
};

template<class Read> void inspect(Read read,const input& in) {
    for(std::size_t i=0;i<in.keys.size();++i){unsigned count=0;const bool found=read.find(in.keys[i],[&](const auto& v){require(v==in.value(i,count),"complete value mismatch");++count;});
        require(found==(i<in.n),"membership mismatch");require(count==(V>0&&found?in.fanout(i):0),"value count mismatch");}
}
template<class T> void fill(T& target,const input& in) {
    for(std::size_t i=0;i<in.n;++i){if constexpr(!std::is_same_v<T,Abseil>){
        // Every MM append performs the actual owner's scalar lookup/write path;
        // no bag import, conversion or reference-only population shortcut.
        for(unsigned j=0;j<in.fanout(i);++j)
            require(target.insert(in.keys[i],in.value(i,j))==(j==0),"distinct owner scalar insert/append");}
        else if constexpr(V==0)require(target.insert(Key(in.keys[i])).second,"distinct Abseil insert");
        else if constexpr(Multi){auto [it,inserted]=target.try_emplace(Key(in.keys[i]));require(inserted,"distinct Abseil bag");for(unsigned j=0;j<in.fanout(i);++j)it->second.push_back(in.value(i,j));}
        else require(target.emplace(Key(in.keys[i]),in.value(i)).second,"distinct Abseil insert");}
}

// Ordinary setup/check helpers, never an extra layer around timed reads.
struct owner_profile {
    const char* label;
    unsigned logical_k,stored_k,primary_header_bytes;
};
template<class Owner> constexpr owner_profile profile() {
    if constexpr(std::is_same_v<Owner,Packed>)return {"packed",K,packed_stored_bits,packed_header_bytes};
    else if constexpr(std::is_same_v<Owner,Whole>)return {"whole",K,whole_stored_bits,whole_header_bytes};
    else if constexpr(std::is_same_v<Owner,MaintainedHeap>)return {"maintained",K,packed_stored_bits,packed_header_bytes};
    else {static_assert(std::is_same_v<Owner,Abseil>);return {"abseil",0,0,0};}
}
template<class Owner> std::size_t owner_bytes(const Owner& owner) {
    // Abseil's census omits external InlinedVector bags. It is retained as the
    // actual control's reported field, never a complete MM footprint ratio.
    if constexpr(std::is_same_v<Owner,Abseil>)return absl::container_internal::AllocatedByteSize(owner);
    else return owner.bytes_used();
}
template<class Owner> std::size_t owner_spill(const Owner& owner) {
    if constexpr(std::is_same_v<Owner,Abseil>)return 0; // Not an Abseil concept.
    else return owner.spill_size();
}
template<class Owner> std::size_t owner_capacity(const Owner& owner) {
    if constexpr(requires{owner.capacity();})return owner.capacity();
    else return Owner::bucket_count*Owner::N;
}
template<class Owner> void validate_owner(const Owner& owner) {
    if constexpr(requires{owner.validate();})owner.validate();
}
template<class Owner> bool erase_key(Owner& owner,word k) {
    if constexpr(std::is_same_v<Owner,Abseil>)return bool(owner.erase(Key(k)));
    else return owner.erase(k);
}
template<class Owner> bool insert_value(Owner& owner,word k,const Value& value) {
    if constexpr(!std::is_same_v<Owner,Abseil>)return owner.insert(k,value);
    else if constexpr(V==0)return owner.insert(Key(k)).second;
    else if constexpr(Multi){auto [it,fresh]=owner.try_emplace(Key(k));it->second.push_back(value);return fresh;}
    else return owner.insert_or_assign(Key(k),value).second;
}

template<class Read> [[gnu::noinline]] result independent(Read read,const input& in) {
    result out;
    for(auto rank:in.order)out.hits+=read.find(in.keys[rank],write_arch::reduction_study::select<Multi&&V==64,ReductionOwner,Read>([&](const auto& v) __attribute__((always_inline)){out.sum+=digest(v);}));
    return out;
}
// The accepted plain prefetch path and bounded streams share Loom's canonical
// typed loop. It is the preserved actual control, not a second hint dispatcher.
template<bool Prefetch,class Read> [[gnu::noinline]] result batch_independent(Read read,const input& in) {
    word sum=0,hits=0;
    if constexpr(Prefetch)loom::prefetch_for_each<independent_width>(std::span<const word>{in.request_keys},
        [&](const word& k)noexcept{read.prefetch(k);},
        [&](const word& k)noexcept{hits+=read.find(k,write_arch::reduction_study::select<Multi&&V==64,ReductionOwner,Read>([&](const auto& v) __attribute__((always_inline)){sum+=digest(v);}));});
    else for(auto k:in.request_keys)hits+=read.find(k,write_arch::reduction_study::select<Multi&&V==64,ReductionOwner,Read>([&](const auto& v) __attribute__((always_inline)){sum+=digest(v);}));
    return {sum,hits,0};
}

template<unsigned C,bool Prefetch,class Read> [[gnu::noinline]] result chain(Read read,const input& in) {
    if constexpr(V>=8)if(!in.miss){
        // The returned full value digest is the next KEY; no input graph load
        // intervenes between this result and its dependent table request.
        std::array<word,C> keys{};std::array<std::size_t,C> lengths{};result out;
        for(unsigned j=0;j<C;++j){keys[j]=in.keys[in.order[std::size_t(j)*in.requests/C]];lengths[j]=(std::size_t(j+1)*in.requests/C)-(std::size_t(j)*in.requests/C);if constexpr(Prefetch)read.prefetch(keys[j]);}
        for(std::size_t i=0;i<(in.requests+C-1)/C;++i)for(unsigned j=0;j<C;++j)if(i<lengths[j]){
            word edge=0;out.hits+=read.find(keys[j],[&](const auto& v) __attribute__((always_inline)){const auto d=digest(v);edge^=d;out.sum+=d;});keys[j]=edge;
            if constexpr(Prefetch)if(i+1<lengths[j])read.prefetch(edge);
        }
        for(auto k:keys)out.terminal^=k;return out;
    }
    std::array<unsigned,C> ranks{};std::array<std::size_t,C> lengths{};result out;
    for(unsigned j=0;j<C;++j){ranks[j]=in.order[std::size_t(j)*in.requests/C];lengths[j]=(std::size_t(j+1)*in.requests/C)-(std::size_t(j)*in.requests/C);if constexpr(Prefetch)read.prefetch(in.keys[ranks[j]]);}
    const auto longest=(in.requests+C-1)/C;
    for(std::size_t i=0;i<longest;++i)for(unsigned j=0;j<C;++j)if(i<lengths[j]){
        auto rank=ranks[j];word edge=0;
        const bool found=read.find(key(rank,in.seed),[&](const auto& v) __attribute__((always_inline)){const auto d=digest(v);edge^=d;out.sum+=d;});out.hits+=found;
        if constexpr(V>=4)ranks[j]=found?unsigned(edge):in.next[rank];
        else ranks[j]=in.narrow_edges[rank][found]^unsigned(edge);
        if constexpr(Prefetch)if(i+1<lengths[j])read.prefetch(key(ranks[j],in.seed));
    }
    for(auto rank:ranks)out.terminal^=rank;return out;
}

// Same shuffled dependent walk used before frozen A's timing. It also touches
// query inputs; this is an explicit conditioning protocol, not a cache-tier claim.
class conditioning {
    struct alignas(64) line {std::uintptr_t next;std::byte padding[56];};
    std::vector<line> storage_;
  public:
    explicit conditioning(std::size_t bytes):storage_((bytes+63)/64){
        if(storage_.empty())return;
        std::vector<std::size_t> order(storage_.size());std::iota(order.begin(),order.end(),0);
        std::mt19937_64 rng(0x87a14411);std::shuffle(order.begin(),order.end(),rng);
        for(std::size_t i=0;i<order.size();++i)storage_[order[i]].next=reinterpret_cast<std::uintptr_t>(&storage_[order[(i+1)%order.size()]]);
    }
    bool enabled()const{return !storage_.empty();}
    std::size_t bytes()const{return storage_.size()*64;}
    const void* data()const{return storage_.data();}
    void apply()const {
        if(!enabled())return;
        auto p=reinterpret_cast<std::uintptr_t>(storage_.data());
        for(std::size_t i=0;i<storage_.size();++i)p=*reinterpret_cast<volatile std::uintptr_t*>(p);
        asm volatile("" :: "r"(p) : "memory");
    }
};
} // namespace study
