#pragma once
#include "small.h"
#include "record_table.h"
#ifndef HT_HIERARCHY_PACKET_UNROLL
#define HT_HIERARCHY_PACKET_UNROLL 0
#endif
#ifndef HT_EXTERNAL_BAG_PATCH
#define HT_EXTERNAL_BAG_PATCH 0
#endif
#ifndef HT_BAG_SPILL_PATCH
#define HT_BAG_SPILL_PATCH 0
#endif
#ifndef HT_PROMOTION_UNIQUE
#define HT_PROMOTION_UNIQUE 1
#endif
static_assert(HT_EXTERNAL_BAG_PATCH==0 || HT_EXTERNAL_BAG_PATCH==1);
static_assert(HT_BAG_SPILL_PATCH==0 || HT_BAG_SPILL_PATCH==1);
static_assert(!HT_BAG_SPILL_PATCH || HT_EXTERNAL_BAG_PATCH);

namespace ht {
template<class Source,class Destination> class hierarchy_migration;
template<class Effects,bool Bag=false> struct promoted_observer {
    Effects& effects;
    void bucket(const void* owner,const oa::Allocation& allocation,std::size_t at,std::size_t bytes,bool secondary)noexcept {
        effects.before({secondary?small_event::kind::secondary_write:small_event::kind::primary_write,
            owner,allocation.storage,at,bytes});
    }
    void operator()(const link_event& e)noexcept {
        const bool write=e.kind==link_event_kind::before_block_write;
        small_event out{Bag?(write?small_event::kind::bag_write:small_event::kind::bag_structure):
            (write?small_event::kind::overflow_write:small_event::kind::overflow_structure),
            e.owner,e.allocation?e.allocation->storage:nullptr,0,e.bytes,e.block};
        out.link_kind=e.kind;out.family=e.family;effects.before(out);
    }
    void operator()(const buffer_event& e)noexcept requires(Bag) {
        small_event out{e.kind==buffer_event_kind::before_payload_write?
            small_event::kind::bag_write:small_event::kind::bag_structure,
            e.owner,e.allocation?e.allocation->storage:nullptr,e.offset,e.bytes,e.id};
        out.buffer_kind=e.kind;effects.before(out);
    }
    std::size_t remaining()const {if constexpr(requires{effects.remaining();})return effects.remaining();else return SIZE_MAX;}
};
// The transition's final owner, including representation-local MM bag refs.
// Fixed A/B and byte units are controls; promotion does not choose a threshold
// or duplicate exact-cardinality policy inside the headless index.
template<unsigned K,container_kind Kind,unsigned ValueBytes=(Kind==container_kind::set?0:8),
         unsigned A=6,unsigned B=2,unsigned P=128,unsigned Q=128,
         record_format Format=record_format::exact,bool Direct=false,
         record_access Access=record_access::exact,bool AdmittedBagAppend=false,
         class Bags=linked_bags<(ValueBytes?ValueBytes:8),AdmittedBagAppend>,unsigned InlineBytes=8> class promoted_table {
    template<class Source,class Destination> friend class hierarchy_migration;
    template<class Table> friend struct consolidation_access;
    using source_type=small_table<K,Kind,ValueBytes,AdmittedBagAppend,Bags,InlineBytes>;
    using bag_policy=typename source_type::bag_policy;
    using index_type=record_table<K,Kind,source_type::stored_bytes,A,B,P,Q,Format,Direct,Access,source_type::tag_bits>;
    using remainder=typename source_type::transition_remainder;
    static constexpr bool multimap=Kind==container_kind::multimap;
    using bags_type=typename source_type::bags_type;
    oa::Pool* pool_;word primary_seed_,secondary_seed_;
    index_type index_;remainder owner_;
    promoted_table(oa::Pool& pool,word a,word b):pool_(&pool),primary_seed_(a),secondary_seed_(b),index_(pool,a,b) {
        // A concatenated live prefix can have a different tail width from one
        // value or the padded index payload. Admit it before the first spill,
        // even when no bag owner has yet been constructed.
        if constexpr(HT_BAG_SPILL_PATCH && multimap && bag_policy::capacity)
            admit_identity<(bag_policy::capacity*ValueBytes)%8>();
    }
  public:
    using record=typename index_type::record;
    promoted_table(promoted_table&&)noexcept=default;
    promoted_table& operator=(promoted_table&&)noexcept=default;
    static std::optional<promoted_table> promote(source_type& source,oa::Pool& pool,word a,word b) {
        try {
            promoted_table result(pool,a,b);bool ok=true;
            // A list has one record per key and the destination is empty.
            // Retain the redundant-search path only as an attribution control.
            source.each_record([&](word key,const record& r){if(ok) {
                if constexpr(HT_PROMOTION_UNIQUE)ok=result.index_.insert_unique_record(key,r);
                else ok=result.index_.insert_record(key,r);
            }});
            if(!ok)return {};
            result.owner_=source.release_for_transition();return result;
        }catch(const std::bad_alloc&) {return {};}
    }
    unsigned size()const{return owner_.keys;}word value_count()const{return owner_.values;}
    template<class Consume> [[gnu::always_inline]] void consume_record_bytes(const record& r,Consume&& consume)const {
        if constexpr(multimap) {
            if(bag_policy::external(r))owner_.bags->each_bytes(ht::get<4>(r.payload.data()),consume);
            else bag_policy::each_bytes(r,consume);
        }else consume(std::span<const byte,ValueBytes>(r.payload));
    }
    template<class Consume> [[gnu::always_inline]] void consume_record(const record& r,Consume&& consume)const requires(ValueBytes<=8) {
        consume_record_bytes(r,[&](auto bytes){consume(ht::get<ValueBytes>(bytes.data()));});
    }
    template<class Consume> [[gnu::always_inline]] bool get_bytes(word key,Consume&& consume)const {
        return index_.get_record(key,[&] [[gnu::always_inline]] (const record& r){consume_record_bytes(r,consume);});
    }
    template<class Consume> [[gnu::always_inline]] bool get(word key,Consume&& consume)const requires(ValueBytes<=8) {
        return get_bytes(key,[&](auto bytes){consume(ht::get<ValueBytes>(bytes.data()));});
    }
    // Same operation/consumer as get_bytes, scheduled at true allocation
    // dependencies. Every emitted span is a borrowed physical read window.
    class reader {
        friend class promoted_table;
        typename index_type::reader index_;
        const bags_type* bags_;
        explicit reader(const promoted_table& t):index_(t.index_.bind_read()),bags_(nullptr) {
            if constexpr(multimap)bags_=t.owner_.bags.get();
        }
      public:
        enum class stage:byte {index,bag_directory,bag_payload};
        struct multimap_probe {
            typename index_type::reader::probe index;
            const byte* payload=nullptr;std::size_t count=0;std::uint32_t link=0;
            stage next=stage::index;
        };
        using probe=std::conditional_t<multimap,multimap_probe,typename index_type::reader::probe>;
        static bool cold_path(const probe& p) {
            if constexpr(multimap)
                return p.next==stage::bag_directory || (p.next==stage::index && index_type::reader::cold_path(p.index));
            else return index_type::reader::cold_path(p);
        }
        template<class Emit> [[gnu::always_inline]] void prepare_read(probe& p,word key,Emit&& emit)const {
            if constexpr(multimap){p.next=stage::index;index_.prepare_read(p.index,key,emit);}
            else index_.prepare_read(p,key,emit);
        }
        template<class Consume> [[gnu::always_inline]] bool get_bytes(word key,Consume&& consume)const {
            return index_.get_record(key,[&] [[gnu::always_inline]] (const record& r) {
                if constexpr(multimap) {
                    if(bag_policy::external(r))bags_->each_bytes(ht::get<4>(r.payload.data()),consume);
                    else bag_policy::each_bytes(r,consume);
                }else consume(std::span<const byte,ValueBytes>(r.payload));
            });
        }
        template<bool Resolve=true,class Consume,class Emit> [[gnu::always_inline]] bool advance_read(
                probe& p,word key,bool& found,Consume&& consume,Emit&& emit)const {
            if constexpr(!multimap) {
                return index_.template advance_read<Resolve>(p,key,found,[&] [[gnu::always_inline]] (const record& r) {
                    consume(std::span<const byte,ValueBytes>(r.payload));
                },emit);
            }else {
              if(p.next==stage::index) {
                const bool done=index_.template advance_read<Resolve>(p.index,key,found,[&] [[gnu::always_inline]] (const record& r) {
                    if(bag_policy::external(r)) {
                        p.link=ht::get<4>(r.payload.data());p.next=stage::bag_directory;
                        const auto w=bags_->directory_window(p.link);emit(w.data(),w.size());
                    }else bag_policy::each_bytes(r,consume);
                },emit);
                return done && p.next==stage::index;
              }
                if(p.next==stage::bag_directory) {
                    if constexpr(Resolve) {
                        const auto [w,count]=bags_->resolve_values(p.link);p.payload=w.data();p.count=count;
                        p.next=stage::bag_payload;emit(w.data(),w.size());return false;
                    } else __builtin_unreachable();
                }
                p.link=bags_type::consume_values(p.payload,p.count,consume);
                if(!p.link){found=true;return true;}
                p.next=stage::bag_directory;const auto w=bags_->directory_window(p.link);
                emit(w.data(),w.size());return false;
            }
        }
    };
    [[gnu::always_inline]] reader bind_read()const {return reader(*this);}
    template<class Visit> void each_bytes(Visit&& visit)const {
        index_.each_record([&](word key,const record& r){consume_record_bytes(r,[&](auto bytes){visit(key,bytes);});});
    }
    template<class Visit> void each(Visit&& visit)const requires(ValueBytes<=8) {
        each_bytes([&](word key,auto bytes){visit(key,ht::get<ValueBytes>(bytes.data()));});
    }
  private:
    template<unsigned N,class Visit> [[gnu::always_inline]] static void packet_lanes(Visit&& visit) {
#if HT_HIERARCHY_PACKET_UNROLL
        lanes<N>(visit);
#else
        // Hashing is explicit SIMD. Keep the complete scalar lane operation
        // shared: unrolling bag preparation and all tiers multiplies hot code.
#pragma clang loop unroll(disable)
        for(unsigned lane=0;lane<N;++lane)visit(lane);
#endif
    }
    template<class Mutate,class Effects,class Combine=replace_bytes> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    set_using(Mutate&& mutate,const std::array<byte,ValueBytes>& value,Effects& effects,Combine combine={}) {
        if(!effects_room(effects,index_type::mutation_event_bound+1+(multimap?1+bags_type::reservation_event_bound:0)))
            return std::unexpected(mutation_error::effects);
        promoted_observer<Effects> observed{effects};promoted_observer<Effects,true> observer{effects};
        // An inline append only needs its new value and tag. Keep map replacement
        // on the fused whole-value path: the native partial-write control loses
        // on x86 maps. Spill and absent-key admission still prepare infallibly.
        auto existing=[&] [[gnu::always_inline]] (auto& edit)noexcept->existing_record_result {
            if constexpr(multimap && (bag_policy::capacity>1 || HT_EXTERNAL_BAG_PATCH)) {
                const auto tag=edit.tag();
                if constexpr(HT_EXTERNAL_BAG_PATCH)if(tag==1) {
                    // Bag preparation may relocate its own blocks/directory,
                    // never this independently owned key index. Only the
                    // descriptor is live in an external row; its inactive tail
                    // and discriminator do not need another read or write.
                    auto descriptor=edit.template prefix<8>();
                    const auto head=std::uint32_t(ht::get<4>(descriptor.data()));
                    const auto hint=std::uint32_t(ht::get<4>(descriptor.data()+4));
                    auto reserved=owner_.bags->reserve_values(head,1,observer,hint);
                    if(!reserved)return std::unexpected(mutation_error::preparation);
                    const word next=word(reserved->head_after())|(word(reserved->insertion_after())<<32);
                    const bool changed=next!=ht::get<8>(descriptor.data());
                    reserved->commit_bytes(std::span<const byte,ValueBytes>(value));
                    if(changed) {
                        ht::put<8>(descriptor.data(),next);
                        edit.template patch<8>(0,std::span<const byte,8>(descriptor));
                    }
                    return mutation_delta{0,1};
                }
                if constexpr(bag_policy::capacity>1)if(tag!=1) {
                    const unsigned count=tag<2?1:tag;
                    if(count<bag_policy::capacity) {
                        edit.template patch<ValueBytes>(count*ValueBytes,std::span<const byte,ValueBytes>(value),byte(count+1));
                        return mutation_delta{0,1};
                    }
                }
                if constexpr(HT_BAG_SPILL_PATCH && bag_policy::capacity) {
                    // The remaining existing state is full inline. Copy only
                    // its live values before fallible bag preparation. No bag
                    // operation can move the separately owned index.
                    constexpr unsigned used=bag_policy::capacity*ValueBytes;
                    const auto inline_values=edit.template prefix<used>();
                    if(!owner_.bags)try {
                        owner_.bags=std::make_unique<bags_type>(*pool_);
                        effects.before({small_event::kind::bag_owner_created,this});
                    }catch(const std::bad_alloc&){return std::unexpected(mutation_error::preparation);}
                    auto reserved=owner_.bags->reserve_values(0,bag_policy::capacity+1,observer);
                    if(!reserved)return std::unexpected(mutation_error::preparation);
                    std::array<byte,8> descriptor;
                    ht::put<8>(descriptor.data(),word(reserved->head_after())|(word(reserved->insertion_after())<<32));
                    reserved->commit_bytes(std::span<const byte,used>(inline_values),std::span<const byte,ValueBytes>(value));
                    edit.template patch<8>(0,std::span<const byte,8>(descriptor),byte(1));
                    return mutation_delta{0,1};
                }
            }
            return {};
        };
        auto r=[&] [[gnu::always_inline]] {
            if constexpr(!multimap)return mutate([&] [[gnu::always_inline]] (const std::optional<record>& old)->std::optional<record>{
                record next;
                if constexpr(Kind==container_kind::map)next.payload=old?combine(old->payload,value):value;
                else std::copy(value.begin(),value.end(),next.payload.begin());
                next.value_delta=Kind==container_kind::map?!old:0;return next;
            },observed,existing);
            else {
                using reservation=typename bags_type::template reservation<decltype(observer)>;
                struct proposal:record {
                    std::optional<reservation> reserved;std::array<byte,ValueBytes*(bag_policy::capacity+1)> values{};unsigned count=0;
                    void commit()noexcept {if(reserved)reserved->commit_bytes(std::span<const byte>(values.data(),std::size_t(count)*ValueBytes));}
                };
                return mutate([&] [[gnu::always_inline]] (const std::optional<record>& old)->std::optional<proposal>{
                    proposal next;next.value_delta=1;
                    if constexpr(bag_policy::capacity)if(!old){std::copy(value.begin(),value.end(),next.payload.begin());return next;}
                    if constexpr(bag_policy::capacity>1)if(old && !bag_policy::external(*old)) {
                        static_cast<record&>(next)=*old;next.value_delta=1;
                        if(bag_policy::append(next,value))return next;
                    }
                    if(!owner_.bags)try{owner_.bags=std::make_unique<bags_type>(*pool_);
                        effects.before({small_event::kind::bag_owner_created,this});
                    }catch(const std::bad_alloc&){return {};}
                    const auto head=old && bag_policy::external(*old)?std::uint32_t(ht::get<4>(old->payload.data())):0;
                    const auto hint=old && bag_policy::external(*old)?std::uint32_t(ht::get<4>(old->payload.data()+4)):0;
                    next.count=old && !bag_policy::external(*old)?bag_policy::capacity+1:1;next.reserved=owner_.bags->reserve_values(head,next.count,observer,hint);
                    if(!next.reserved)return {};
                    next.tag=1;ht::put<8>(next.payload.data(),word(next.reserved->head_after())|
                        (word(next.reserved->insertion_after())<<32));
                    if(old && !bag_policy::external(*old))
                        std::memcpy(next.values.data(),old->payload.data(),(next.count-1)*ValueBytes);
                    std::memcpy(next.values.data()+(next.count-1)*ValueBytes,value.data(),ValueBytes);return next;
                },observed,existing);
            }
        }();
        if(r){owner_.keys+=r->keys;owner_.values+=r->values;
            effects.before({small_event::kind::caller_delta,this,nullptr,0,0,0,*r});}return r;
    }
  public:
    /// The initialized value stays valid and disjoint from table-owned storage
    /// throughout preparation and commit, including bag relocation.
    template<class Effects> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    try_set_bytes(word key,const std::array<byte,ValueBytes>& value,Effects& effects) {
        return set_using([&] [[gnu::always_inline]] (auto&& prepare,auto& observed,auto&& existing) {
            return index_.mutate(key,prepare,observed,existing);
        },value,effects);
    }
    /// Same private-owner and input lifetime as try_set_bytes. Combines in the
    /// located bucket without a caller-side lookup followed by another search.
    template<class Combine,class Effects> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    try_combine_bytes(word key,const std::array<byte,ValueBytes>& value,Combine combine,Effects& effects)
        requires(Kind==container_kind::map) {
        using bytes=std::array<byte,ValueBytes>;
        static_assert(std::is_nothrow_invocable_r_v<bytes,Combine,const bytes&,const bytes&>);
        return set_using([&] [[gnu::always_inline]] (auto&& prepare,auto& observed,auto&& existing) {
            return index_.mutate(key,prepare,observed,existing);
        },value,effects,combine);
    }
  private:
    template<unsigned N,class Effects,class Combine> [[gnu::always_inline]] unsigned mutateN_bytes(std::span<const word,N> keys,
            std::span<const std::array<byte,ValueBytes>,N> values,unsigned active,Effects& effects,Combine combine) {
        active&=unsigned(mask(N));if(!active)return 0;
        return index_.template with_packet<N>(keys,[&] [[gnu::always_inline]] (auto& packet) {
            unsigned processed=0;bool failed=false;
            packet_lanes<N>([&] [[gnu::always_inline]] (auto lane) {
                if(!failed && (active&(1u<<lane))) {
                    auto r=set_using([&] [[gnu::always_inline]] (auto&& prepare,auto& observed,auto&& existing) {
                        return packet.mutate(lane,prepare,observed,existing);
                    },values[lane],effects,combine);
                    if(r)processed|=1u<<lane;else failed=true;
                }
            });return processed;
        });
    }
  public:
    /// Ascending active-lane order, including repeated keys. A refusal commits
    /// only the preceding active lanes; caller deltas/damage remain per mutation.
    /// All N key/value objects are initialized and disjoint from table storage.
    template<unsigned N,class Effects> [[gnu::always_inline]] unsigned setN_bytes(std::span<const word,N> keys,
            std::span<const std::array<byte,ValueBytes>,N> values,unsigned active,Effects& effects) {
        return mutateN_bytes<N>(keys,values,active,effects,replace_bytes{});
    }
    /// Same admission and effects as setN_bytes. Duplicate keys observe the
    /// previous accepted lane's value; no mutable lookup state crosses lanes.
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
    template<unsigned N,class Consume> [[gnu::always_inline]] unsigned getN_bytes(std::span<const word,N> keys,
            unsigned active,Consume&& consume)const {
        active&=unsigned(mask(N));if(!active)return 0;
        return index_.template with_packet<N>(keys,[&] [[gnu::always_inline]] (auto& packet) {
            unsigned found=0;
            packet_lanes<N>([&] [[gnu::always_inline]] (auto lane) {
                if(active&(1u<<lane))if(packet.get(lane,[&] [[gnu::always_inline]] (const record& r) {
                    consume_record_bytes(r,[&] [[gnu::always_inline]] (auto bytes){consume(lane,bytes);});
                }))found|=1u<<lane;
            });return found;
        });
    }
    template<class Effects> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    try_set(word key,word value,Effects& effects) requires(ValueBytes<=8) {
        std::array<byte,ValueBytes> bytes{};if constexpr(ValueBytes)std::memcpy(bytes.data(),&value,ValueBytes);
        return try_set_bytes(key,bytes,effects);
    }
    [[gnu::always_inline]] std::expected<mutation_delta,mutation_error> try_set_bytes(word key,const std::array<byte,ValueBytes>& value) {
        no_small_effects effects;return try_set_bytes(key,value,effects);
    }
    [[gnu::always_inline]] mutation_delta set_bytes(word key,const std::array<byte,ValueBytes>& value) {
        auto r=try_set_bytes(key,value);require(bool(r),"promoted byte insertion");return *r;
    }
    std::expected<mutation_delta,mutation_error> try_set(word key,word value) requires(ValueBytes<=8) {
        no_small_effects effects;return try_set(key,value,effects);
    }
    mutation_delta set_value(word key,word value) requires(ValueBytes<=8) {auto r=try_set(key,value);require(bool(r),"promoted set");return *r;}
    template<class Effects> std::expected<mutation_delta,mutation_error> erase(word key,Effects& effects) {
        word values=Kind==container_kind::set?0:1;
        promoted_observer<Effects> observed{effects};promoted_observer<Effects,true> observer{effects};
        auto old=index_.erase(key,observed,[&](const record& old) {
            std::size_t needed=index_type::erase_event_bound+1;
            if constexpr(multimap)if(bag_policy::external(old))needed+=owner_.bags->release_event_bound(ht::get<4>(old.payload.data()));
            if(!effects_room(effects,needed))return false;
            if constexpr(multimap)values=bag_policy::external(old)?
                owner_.bags->release_values(ht::get<4>(old.payload.data()),observer):bag_policy::count(old);
            return true;
        });
        if(!old)return std::unexpected(old.error());if(!*old)return mutation_delta{};
        --owner_.keys;owner_.values-=values;mutation_delta result{-1,-std::int64_t(values)};
        effects.before({small_event::kind::caller_delta,this,nullptr,0,0,0,result});return result;
    }
    mutation_delta erase(word key) {no_small_effects effects;auto r=erase(key,effects);require(bool(r),"promoted erase");return *r;}
    template<class Visit> void read_storage(Visit&& visit)const {
        visit(std::span<const byte>(reinterpret_cast<const byte*>(this),sizeof(*this)));
        index_.read_storage(visit);
        if constexpr(multimap)if(owner_.bags) {
            visit(owner_.bags->directory_storage());
            owner_.bags->allocations([&](const oa::Allocation& a){visit(std::span<const byte>(reinterpret_cast<const byte*>(a.address),a.size));});
        }
    }
    memory footprint()const {
        auto m=index_.footprint();m.metadata+=sizeof(*this)-sizeof(index_);
        if constexpr(multimap)if(owner_.bags){auto b=owner_.bags->footprint();m.requested+=b.requested;m.backing+=b.backing;m.metadata+=b.metadata;}
        return m;
    }
    dense_domain_advisory memory_advice()const {
        return advise_dense_domain<K>(owner_.keys,source_type::view_type::row_bytes,index_.footprint().requested);
    }
    std::optional<promoted_table> clone()const {
        try {
            promoted_table copy(*pool_,primary_seed_,secondary_seed_);bool ok=true;
            each_bytes([&](word k,auto value){if(ok){std::array<byte,ValueBytes> bytes;
                std::copy(value.begin(),value.end(),bytes.begin());ok=bool(copy.try_set_bytes(k,bytes));}});if(!ok)return {};return copy;
        }catch(const std::bad_alloc&){return {};}
    }
    auto population()const{return index_.population();}
    void validate()const{index_.validate();if constexpr(multimap)if(owner_.bags)owner_.bags->validate();}
};
} // namespace ht
