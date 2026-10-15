#pragma once
#include "list.h"
#include "links.h"
#include "bag_buffers.h"
#include "inline_bag.h"
#include "domain_advisory.h"
#include "compact_hash.h"
#include <memory>
#ifndef HT_SMALL_COMPACT_OWNER
#define HT_SMALL_COMPACT_OWNER 1
#endif
#ifndef HT_SMALL_PACKET_SCALAR
#define HT_SMALL_PACKET_SCALAR 0
#endif
#ifndef HT_SMALL_PACKET_LOOKUP
#define HT_SMALL_PACKET_LOOKUP 0
#endif

namespace ht {
template<class Source,class Destination> class hierarchy_migration;
template<class Table> struct consolidation_access;
template<bool Stored> struct list_value_count {
    word value=0;
    void add(std::int64_t n){value+=n;}
    void subtract(word n){value-=n;}
};
template<> struct list_value_count<false> {
    void add(std::int64_t){}
    void subtract(word){}
};
// Events resolve borrowed primitive source identities synchronously. Allocation
// identity is a lifetime-qualified token, never permission to mutate a version.
// Structural outcomes and issued stores remain distinct. A consumer must drain
// this output while those allocations/owner identities remain in scope.
struct small_event {
    enum class kind { list_write, list_rebound, bag_owner_created, bag_write, bag_structure,
        primary_write, secondary_write, overflow_write, overflow_structure, owner_reset, caller_delta } type;
    const void* owner=nullptr;
    const void* storage=nullptr;
    std::size_t offset=0,size=0;
    std::uint32_t block=0;
    mutation_delta delta{};
    std::optional<link_event_kind> link_kind={};
    unsigned family=0;
    std::optional<buffer_event_kind> buffer_kind={};
};
struct no_small_effects {void before(const small_event&)const noexcept {}};
struct small_journal {
    std::span<small_event> storage;std::size_t used=0;
    std::size_t remaining()const {return storage.size()-used;}
    void before(const small_event& e)noexcept {storage[used++]=e;}
};
template<class Effects,bool WholeList=false> struct small_observer {
    Effects& effects;const void* owner;const oa::Allocation& allocation;
    template<class Source> void before(const Source&,ikea::byte_write w)noexcept {
        effects.before({small_event::kind::list_write,owner,allocation.storage,
            WholeList?0:w.offset,WholeList?allocation.size:w.size});
    }
    void operator()(const link_event& e)noexcept {
        const bool writes=e.kind==link_event_kind::before_block_write;
        small_event result{writes?small_event::kind::bag_write:small_event::kind::bag_structure,
            e.owner,e.allocation?e.allocation->storage:nullptr,0,e.bytes,e.block};
        result.link_kind=e.kind;result.family=e.family;effects.before(result);
    }
    void operator()(const buffer_event& e)noexcept {
        small_event result{e.kind==buffer_event_kind::before_payload_write?
            small_event::kind::bag_write:small_event::kind::bag_structure,
            e.owner,e.allocation?e.allocation->storage:nullptr,e.offset,e.bytes,e.id};
        result.buffer_kind=e.kind;effects.before(result);
    }
    std::size_t remaining()const {
        if constexpr(requires {effects.remaining();})return effects.remaining();else return SIZE_MAX;
    }
};

template<unsigned K,container_kind Kind,unsigned ValueBytes=(Kind==container_kind::set?0:8),bool AdmittedBagAppend=false,
         class Bags=linked_bags<(ValueBytes?ValueBytes:8),AdmittedBagAppend>,unsigned InlineBytes=8>
class small_table {
    template<class Source,class Destination> friend class hierarchy_migration;
    template<class Table> friend struct consolidation_access;
    static_assert(ValueBytes<=64 && (Kind==container_kind::set || ValueBytes>0));
    static_assert(Kind!=container_kind::multimap || Bags::bag_value_bytes==ValueBytes);
  public:
    using source_type=small_table;
    using bag_policy=inline_bag<(ValueBytes?ValueBytes:1),InlineBytes>;
    static constexpr unsigned stored_bytes=Kind==container_kind::multimap?bag_policy::payload_bytes:ValueBytes;
    static constexpr unsigned tag_bits=Kind==container_kind::multimap?bag_policy::tag_bits:1;
    using view_type=list_view<K,Kind,stored_bytes,0,tag_bits>;
    using record=typename view_type::record;
    // The rejected selector-family experiment retains its original state.
    // Compacting the selected permutation changes no stored key mapping.
    static constexpr bool compact_owner=HT_SMALL_COMPACT_OWNER && !HT_SELECTOR_HASH;
    using hash_type=std::conditional_t<compact_owner,state_experiment::compact_permutation<K,0>,
        typename view_type::hash_type>;
    using bags_type=Bags;
    static constexpr bool multimap=Kind==container_kind::multimap;
    static constexpr unsigned key_bits=K;
    static constexpr unsigned value_bytes=ValueBytes;
    static constexpr container_kind kind=Kind;
  private:
    struct empty_bags {};
    oa::Pool* pool_;
    list_owner<K,Kind,stored_bytes,tag_bits> list_;
    [[no_unique_address]] std::conditional_t<multimap,std::unique_ptr<bags_type>,empty_bags> bags_;
    // This is the integration caller's state, outside headless packed storage.
    // It is moved/captured with a version, never read from a newer owner.
    [[no_unique_address]] list_value_count<multimap || !compact_owner> values_;
    unsigned count_=0;
    hash_type hash_;
    [[gnu::always_inline]] decltype(auto) packet_hash()const {
        if constexpr(compact_owner)return hash_.expanded();
        else return (hash_);
    }
    template<class Effects,bool WholeList,class Combine=replace_bytes> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    apply(typename hash_type::parts h,const std::array<byte,ValueBytes>& value,Effects& effects,Combine combine={}) {
        const int found=list_.bind().find(h.fingerprint,h.suffix,count_);
        return apply_at<Effects,WholeList>(h,found,value,effects,combine);
    }
    // Only the bounded append/update packet may carry an admitted integer rank
    // across earlier lane writes. Allocation growth preserves ranks; no borrowed
    // binding or value record survives. Every mutation reads the current value.
    template<class Effects,bool WholeList,class Combine> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    apply_at(typename hash_type::parts h,int found,const std::array<byte,ValueBytes>& value,Effects& effects,Combine combine) {
        constexpr unsigned maximum_events=5+unsigned(HT_LIST_GROW_PRIVATE)+(multimap?1+bags_type::reservation_event_bound:0);
        if(!effects_room(effects,maximum_events))return std::unexpected(mutation_error::effects);
        const auto previous_capacity=list_.capacity();
        if(found<0 && !list_.reserve(*pool_,count_,count_+1,[&](const oa::Allocation& old)noexcept {
            effects.before({small_event::kind::list_write,this,old.storage,0,old.size});
        }))return std::unexpected(mutation_error::capacity);
        if(list_.capacity()!=previous_capacity)effects.before({small_event::kind::list_rebound,this,
            list_.allocation.storage,0,list_.allocation.size});
        auto view=list_.bind();small_observer<Effects,WholeList> observed{effects,this,list_.allocation};
        auto result=[&]() {
            if constexpr(!multimap) {
                return view.mutate_at(h,count_,found,[&](const std::optional<record>& old)->std::optional<record> {
                    record next;
                    if constexpr(Kind==container_kind::map)next.payload=old?combine(old->payload,value):value;
                    else next.payload=value;
                    next.value_delta=Kind==container_kind::map?!old:0;return next;
                },observed);
            } else {
                using reservation=typename bags_type::template reservation<decltype(observed)>;
                struct proposal:record {
                    std::optional<reservation> reserved;
                    std::array<byte,ValueBytes*(bag_policy::capacity+1)> append{};unsigned number=0;
                    void commit()noexcept {if(reserved)reserved->commit_bytes(std::span<const byte>(append.data(),std::size_t(number)*ValueBytes));}
                };
                return view.mutate_at(h,count_,found,[&](const std::optional<record>& old)->std::optional<proposal> {
                    proposal next;next.value_delta=1;
                    if constexpr(bag_policy::capacity)if(!old) {
                        std::copy(value.begin(),value.end(),next.payload.begin());return next;
                    }
                    if constexpr(bag_policy::capacity>1)if(old && !bag_policy::external(*old)) {
                        static_cast<record&>(next)=*old;next.value_delta=1;
                        if(bag_policy::append(next,value))return next;
                    }
                    if(!bags_) {
                        try {bags_=std::make_unique<bags_type>(*pool_);}catch(const std::bad_alloc&) {return {};}
                        effects.before({small_event::kind::bag_owner_created,this});
                    }
                    const auto head=old && bag_policy::external(*old)?std::uint32_t(ht::get<4>(old->payload.data())):0;
                    const auto hint=old && bag_policy::external(*old)?std::uint32_t(ht::get<4>(old->payload.data()+4)):0;
                    next.number=old && !bag_policy::external(*old)?bag_policy::capacity+1:1;
                    next.reserved=bags_->reserve_values(head,next.number,observed,hint);
                    if(!next.reserved)return {};
                    next.tag=1;put<8>(next.payload.data(),word(next.reserved->head_after())|
                        (word(next.reserved->insertion_after())<<32));
                    if(old && !bag_policy::external(*old))
                        std::memcpy(next.append.data(),old->payload.data(),(next.number-1)*ValueBytes);
                    std::memcpy(next.append.data()+(next.number-1)*ValueBytes,value.data(),ValueBytes);return next;
                },observed);
            }
        }();
        if(!result)return result;
        count_+=result->keys;values_.add(result->values);
        effects.before({small_event::kind::caller_delta,this,nullptr,0,0,0,*result});return result;
    }
  public:
    explicit small_table(oa::Pool& pool,word seed):pool_(&pool),hash_(seed) {}
    small_table(small_table&&)noexcept=default;
    small_table& operator=(small_table&&)noexcept=default;
    small_table(const small_table&)=delete;
    unsigned size()const {return count_;}
    word value_count()const {
        if constexpr(multimap || !compact_owner)return values_.value;
        else return Kind==container_kind::map?count_:0;
    }
    unsigned capacity()const {return list_.capacity();}
    template<class Effects,bool WholeList=false> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    set_bytes(word key,const std::array<byte,ValueBytes>& value,Effects& effects) {
        return apply<Effects,WholeList>(hash_.split(key),value,effects);
    }
    mutation_delta set_bytes(word key,const std::array<byte,ValueBytes>& value) {
        no_small_effects effects;auto result=set_bytes(key,value,effects);
        require(bool(result),"small byte insertion");return *result;
    }
    /// One lookup and one observed mutation. The synchronous, nonthrowing
    /// combiner sees the old and incoming opaque values only for an existing key.
    /// It must not mutate this owner; incoming bytes are disjoint from its storage.
    template<class Combine,class Effects> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    try_combine_bytes(word key,const std::array<byte,ValueBytes>& value,Combine combine,Effects& effects)
        requires(Kind==container_kind::map) {
        using bytes=std::array<byte,ValueBytes>;
        static_assert(std::is_nothrow_invocable_r_v<bytes,Combine,const bytes&,const bytes&>);
        return apply<Effects,false>(hash_.split(key),value,effects,combine);
    }
    template<class Effects,bool WholeList=false> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    set_value(word key,word value,Effects& effects) requires(ValueBytes<=8) {
        std::array<byte,ValueBytes> bytes{};if constexpr(ValueBytes)std::memcpy(bytes.data(),&value,ValueBytes);
        return set_bytes<Effects,WholeList>(key,bytes,effects);
    }
    [[gnu::always_inline]] mutation_delta set_value(word key,word value) requires(ValueBytes<=8) {
        no_small_effects effects;auto result=set_value(key,value,effects);require(bool(result),"small insertion");return *result;
    }
    template<class Consume> [[gnu::always_inline]] bool get_bytes(word key,Consume&& consume)const {
        const auto h=hash_.split(key);auto v=list_.bind();const auto slot=v.find(h.fingerprint,h.suffix,count_);
        if(slot<0)return false;
        const auto r=v.read(slot);
        if constexpr(multimap) {
            if(bag_policy::external(r))bags_->each_bytes(ht::get<4>(r.payload.data()),consume);
            else bag_policy::each_bytes(r,consume);
        }else consume(std::span<const byte,ValueBytes>(r.payload));
        return true;
    }
    template<class Consume> [[gnu::always_inline]] bool get(word key,Consume&& consume)const requires(ValueBytes<=8) {
        return get_bytes(key,[&](const auto& bytes){consume(ht::get<ValueBytes>(bytes.data()));});
    }
    template<class Visit> void each_bytes(Visit&& visit)const {
        const auto v=list_.bind();for(unsigned i=0;i<count_;++i) {
            const auto key=hash_.invert({0,ht::get<view_type::suffix_bytes>(v.suffix_at(i)),v.data[i]});
            const auto r=v.read(i);
            if constexpr(multimap) {
                auto consume=[&](auto bytes){visit(key,bytes);};
                if(bag_policy::external(r))bags_->each_bytes(ht::get<4>(r.payload.data()),consume);
                else bag_policy::each_bytes(r,consume);
            }else visit(key,std::span<const byte,ValueBytes>(r.payload));
        }
    }
    template<class Visit> void each(Visit&& visit)const requires(ValueBytes<=8) {
        each_bytes([&](word key,auto bytes){visit(key,ht::get<ValueBytes>(bytes.data()));});
    }
    template<class Visit> void each_record(Visit&& visit)const {
        auto v=list_.bind();for(unsigned i=0;i<count_;++i)
            visit(hash_.invert({0,ht::get<view_type::suffix_bytes>(v.suffix_at(i)),v.data[i]}),v.read(i));
    }
    struct scan_cursor {unsigned rank=0;};
    /// Keep this source and its interpretation immutable through the scan.
    /// A refused row stays at the cursor; accepted rows are never replayed.
    template<class Visit> record_scan_result scan_records(scan_cursor& at,unsigned budget,Visit&& visit)const {
        record_scan_result result;
        [[clang::always_inline]]result=scan_keys(at,budget,[&](auto h){return hash_.invert(h);},visit);
        return result;
    }
  private:
    template<class Key,class Visit> record_scan_result scan_keys(scan_cursor& at,unsigned budget,Key&& key,Visit&& visit)const {
        require(at.rank<=count_,"list scan cursor");
        const auto v=list_.bind();record_scan_result result{record_scan_state::yielded};
        while(at.rank<count_ && result.work<budget) {
            ++result.work;
            const auto k=key(typename hash_type::parts{0,ht::get<view_type::suffix_bytes>(v.suffix_at(at.rank)),v.data[at.rank]});
            if(!visit(k,v.read(at.rank))){result.state=record_scan_state::refused;return result;}
            ++at.rank;++result.records;
        }
        if(at.rank==count_)result.state=record_scan_state::done;
        return result;
    }
  public:
    struct transition_remainder {
        [[no_unique_address]] std::conditional_t<multimap,std::unique_ptr<bags_type>,empty_bags> bags;
        unsigned keys=0;word values=0;
    };
    // Destructive completion after every row is present in a private target.
    // Bag refs keep their owner/block/rank meaning; the target consumes this
    // remainder before becoming discoverable. A failed copy never calls this.
    transition_remainder release_for_transition()noexcept {
        transition_remainder result{std::move(bags_),count_,value_count()};
        list_={};count_=0;values_={};return result;
    }
    template<class Effects> std::expected<mutation_delta,mutation_error> erase(word key,Effects& effects) {
        const auto h=hash_.split(key);auto view=list_.bind();const auto slot=view.find(h.fingerprint,h.suffix,count_);
        if(slot<0)return mutation_delta{};
        const auto old=view.read(slot);std::size_t bag_events=0;word removed=Kind==container_kind::set?0:1;
        if constexpr(multimap)removed=bag_policy::count(old);
        if constexpr(multimap)if(bag_policy::external(old)) {
            const auto head=std::uint32_t(ht::get<4>(old.payload.data()));
            const auto info=bags_->release_info(head);removed=info.first;bag_events=info.second;
        }
        if(!effects_room(effects,4+bag_events))return std::unexpected(mutation_error::effects);
        small_observer<Effects> observer{effects,this,list_.allocation};
        if constexpr(multimap)if(bag_policy::external(old))bags_->release_values(ht::get<4>(old.payload.data()),observer);
        require(bool(view.erase(slot,count_,observer)),"preadmitted list erase");
        --count_;values_.subtract(removed);mutation_delta result{-1,-std::int64_t(removed)};
        effects.before({small_event::kind::caller_delta,this,nullptr,0,0,0,result});return result;
    }
    mutation_delta erase(word key) {no_small_effects e;auto r=erase(key,e);require(bool(r),"small erase");return *r;}
    template<unsigned N,class Effects> [[gnu::always_inline]] unsigned setN(std::span<const word,N> keys,std::span<const word,N> values,
                                                     unsigned active,Effects& effects) requires(ValueBytes<=8) {
        const auto hash=split_packet<N>(packet_hash(),keys);unsigned processed=0;bool failed=false;
        lanes<N>([&](auto i){if((active&(1u<<i))&&!failed) {
            std::array<byte,ValueBytes> bytes{};if constexpr(ValueBytes)std::memcpy(bytes.data(),&values[i],ValueBytes);
            const auto r=apply<Effects,false>({hash.bucket[i],hash.suffix[i],byte(hash.fingerprint[i])},bytes,effects);
            if(!r)failed=true;else processed|=1u<<i;
        }});return processed;
    }
  private:
    template<unsigned N> [[gnu::always_inline]] hash_packet<N> prepared_small_hashes(std::span<const word,N> keys)const {
        if constexpr(!HT_SMALL_PACKET_SCALAR)return split_packet<N>(packet_hash(),keys);
        else {
            hash_packet<N> hash{};
            lanes<N>([&](auto i){const auto h=hash_.split(keys[i]);hash.suffix[i]=h.suffix;hash.fingerprint[i]=h.fingerprint;});
            return hash;
        }
    }
    template<unsigned N,class Effects,class Combine> [[gnu::always_inline]] unsigned mutateN_bytes(std::span<const word,N> keys,
            std::span<const std::array<byte,ValueBytes>,N> values,unsigned active,Effects& effects,Combine combine) {
        static_assert(N==4||N==8);
        active&=unsigned(mask(N));if(!active)return 0;
        if constexpr(HT_SMALL_PACKET_LOOKUP) {
            const auto hash=prepared_small_hashes<N>(keys);
            auto ranks=list_.bind().template findN<N>(hash,count_,active);
            unsigned processed=0;
#pragma clang loop unroll(disable)
            for(unsigned i=0;i<N;++i)if(active&(1u<<i)) {
                const auto inserted_rank=count_;
                const auto result=apply_at<Effects,false>({0,hash.suffix[i],byte(hash.fingerprint[i])},
                    ranks[i],values[i],effects,combine);
                if(!result)break;
                processed|=1u<<i;
                // A precomputed miss can become an existing key after this
                // lane commits. Canonical key equality handles masked domains.
                if(result->keys)for(unsigned j=i+1;j<N;++j)
                    if((active&(1u<<j))&&((keys[i]^keys[j])&mask(K))==0)ranks[j]=int(inserted_rank);
            }
            return processed;
        }
        const auto hash=[&] {
            if constexpr(HT_SMALL_PACKET_SCALAR)return empty_bags{};
            else return split_packet<N>(packet_hash(),keys);
        }();
        unsigned processed=0;bool failed=false;
        // Keep allocation-capable lane bodies shared. Unrolling eight copies
        // inflated the complete Worker dispatcher; hash preparation stays N8.
#pragma clang loop unroll(disable)
        for(unsigned i=0;i<N;++i)if((active&(1u<<i))&&!failed) {
            const auto h=[&]() -> typename hash_type::parts {
                if constexpr(HT_SMALL_PACKET_SCALAR)return hash_.split(keys[i]);
                else return {hash.bucket[i],hash.suffix[i],byte(hash.fingerprint[i])};
            }();
            const auto r=apply<Effects,false>(h,values[i],effects,combine);
            if(!r)failed=true;else processed|=1u<<i;
        }return processed;
    }
  public:
    /// Ascending active-lane order; refusal commits only the preceding lanes.
    /// All N input objects stay initialized and disjoint from table storage.
    /// Effects and reduction callbacks must not reenter or mutate this owner.
    template<unsigned N,class Effects> [[gnu::always_inline]] unsigned setN_bytes(std::span<const word,N> keys,
            std::span<const std::array<byte,ValueBytes>,N> values,unsigned active,Effects& effects) {
        return mutateN_bytes<N>(keys,values,active,effects,replace_bytes{});
    }
    /// The combiner runs only for admitted existing-key lanes, in lane order.
    template<unsigned N,class Combine,class Effects> [[gnu::always_inline]] unsigned combineN_bytes(std::span<const word,N> keys,
            std::span<const std::array<byte,ValueBytes>,N> values,unsigned active,Combine combine,Effects& effects)
        requires(Kind==container_kind::map) {
        using bytes=std::array<byte,ValueBytes>;
        static_assert(std::is_nothrow_invocable_r_v<bytes,Combine,const bytes&,const bytes&>);
        return mutateN_bytes<N>(keys,values,active,effects,combine);
    }
    template<unsigned N> [[gnu::always_inline]] unsigned setN_bytes(std::span<const word,N> keys,
            std::span<const std::array<byte,ValueBytes>,N> values,unsigned active) {
        no_small_effects effects;return setN_bytes<N>(keys,values,active,effects);
    }
    template<unsigned N> [[gnu::always_inline]] unsigned setN(std::span<const word,N> keys,std::span<const word,N> values,unsigned active) {
        no_small_effects e;return setN<N>(keys,values,active,e);
    }
    template<unsigned N,class Consume> [[gnu::always_inline]] unsigned getN_bytes(std::span<const word,N> keys,
            unsigned active,Consume&& consume)const {
        if constexpr(HT_SMALL_PACKET_LOOKUP) {
            active&=unsigned(mask(N));if(!active)return 0;
            const auto hash=prepared_small_hashes<N>(keys);const auto view=list_.bind();
            const auto ranks=view.template findN<N>(hash,count_,active);unsigned found=0;
            lanes<N>([&] [[gnu::always_inline]] (auto i){if(ranks[i]>=0) {
                found|=1u<<i;const auto r=view.read(unsigned(ranks[i]));
                if constexpr(multimap) {
                    auto bytes=[&](auto value){consume(i,value);};
                    if(bag_policy::external(r))bags_->each_bytes(ht::get<4>(r.payload.data()),bytes);
                    else bag_policy::each_bytes(r,bytes);
                }else consume(i,std::span<const byte,ValueBytes>(r.payload));
            }});
            return found;
        }
        return list_.bind().template getN<N>(packet_hash(),keys,count_,active,[&] [[gnu::always_inline]] (auto i,const record& r) {
            if constexpr(multimap) {
                auto bytes=[&](auto value){consume(i,value);};
                if(bag_policy::external(r))bags_->each_bytes(ht::get<4>(r.payload.data()),bytes);
                else bag_policy::each_bytes(r,bytes);
            }else consume(i,std::span<const byte,ValueBytes>(r.payload));
        });
    }
    template<unsigned N,class Consume> [[gnu::always_inline]] unsigned getN(std::span<const word,N> keys,
            unsigned active,Consume&& consume)const requires(ValueBytes<=8) {
        return getN_bytes<N>(keys,active,[&](auto i,auto bytes){consume(i,ht::get<ValueBytes>(bytes.data()));});
    }
    template<class Effects> std::expected<mutation_delta,mutation_error> clear(Effects& effects) {
        if(!effects_room(effects,2))return std::unexpected(mutation_error::effects);
        mutation_delta delta{-int(count_),-std::int64_t(value_count())};
        effects.before({small_event::kind::owner_reset,this});
        list_=list_owner<K,Kind,stored_bytes,tag_bits>{};if constexpr(multimap)bags_.reset();count_=0;values_={};
        effects.before({small_event::kind::caller_delta,this,nullptr,0,0,0,delta});return delta;
    }
    mutation_delta clear() {no_small_effects effects;return *clear(effects);}
    memory footprint()const {
        auto m=list_.footprint();m.metadata=sizeof(*this);
        if constexpr(multimap)if(bags_) {auto b=bags_->footprint();m.requested+=b.requested;m.backing+=b.backing;m.metadata+=b.metadata;}
        return m;
    }
    dense_domain_advisory memory_advice()const {
        return advise_dense_domain<K>(count_,view_type::row_bytes,list_.allocation.size);
    }
    template<class Visit> void allocations(Visit&& visit)const {
        if(list_.allocation)visit(list_.allocation);
        if constexpr(multimap)if(bags_)bags_->allocations(visit);
    }
    // Whole-copy publication control: all bytes and owner state are private.
    // This is charged by the driver and does not prescribe future COW granularity.
    std::optional<small_table> clone()const {
        small_table result(*pool_,0);result.hash_=hash_;
        auto list=list_.clone(*pool_);if(!list)return {};
        result.list_=std::move(*list);
        if constexpr(multimap)if(bags_) {
            auto bags=bags_->clone(*pool_);if(!bags)return {};
            try {result.bags_=std::make_unique<bags_type>(std::move(*bags));}
            catch(const std::bad_alloc&) {return {};}
        }
        result.count_=count_;result.values_=values_;return result;
    }
    // Keep logical replay as an explicit attribution control, not the default
    // cost of retaining a whole private version.
    std::optional<small_table> clone_rebuild()const {
        small_table result(*pool_,0);result.hash_=hash_;
        if(count_&&!result.list_.reserve(*pool_,0,count_))return {};
        bool ok=true;each_bytes([&](word key,auto value){if(ok){
            std::array<byte,ValueBytes> bytes;std::copy(value.begin(),value.end(),bytes.begin());
            no_small_effects e;ok=bool(result.set_bytes(key,bytes,e));}});
        if(!ok)return {};return result;
    }
};
} // namespace ht
