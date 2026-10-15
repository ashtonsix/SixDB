#pragma once
#include "promoted.h"

namespace ht {
struct consolidation_key {word bucket=0,suffix=0,decoded=0;byte fingerprint=0;bool encoded=false;};
// Spike-only composition access. Keep the experiment outside the ordinary
// single-value mutation bodies until complete callers qualify the new boundary.
template<class Table> struct consolidation_access {
    using small=typename Table::source_type;
    using policy=typename small::bag_policy;
    using bags=typename small::bags_type;
    using record=typename Table::record;
    static constexpr auto kind=small::kind;
    static constexpr unsigned width=small::value_bytes;
    static constexpr bool hierarchy=requires(Table& t){t.index_;};
    template<class T> static auto cursor_type(int)->typename T::index_type::scan_cursor;
    template<class T> static auto cursor_type(...)->typename T::scan_cursor;
    using record_cursor=decltype(cursor_type<Table>(0));
    // This is an operation-local fact, not a persisted key or cross-owner ref.
    // Encoded facts require the matching source/destination mapping admitted
    // once before transfer; neither owner may change its interpretation.
    using merge_key=consolidation_key;
    template<class Other> static bool same_mapping(const Table& a,const Other& b) {
        using other_small=typename Other::source_type;
        constexpr bool other_hierarchy=requires(Other& t){t.index_;};
        if constexpr(small::key_bits!=other_small::key_bits || kind!=other_small::kind || width!=other_small::value_bytes)
            return false;
        else if constexpr(hierarchy && other_hierarchy) {
            // Byte units, inline budgets and secondary geometry may differ.
            // Secondary source rows are decoded through their own mapping.
            if constexpr(Table::index_type::primary_bits!=Other::index_type::primary_bits)return false;
            else return a.index_.hp_==b.index_.hp_;
        }else if constexpr(!hierarchy && !other_hierarchy)return a.hash_==b.hash_;
        else return false;
    }
    static auto population(const Table& t) requires(hierarchy) {return t.index_.population();}
    static word logical_key(const Table& t,const merge_key& k) {
        if(!k.encoded)return k.decoded;
        if constexpr(hierarchy)return t.index_.hp_.invert({k.bucket,k.suffix,k.fingerprint});
        else return t.hash_.invert({k.bucket,k.suffix,k.fingerprint});
    }
    template<bool Encoded=false,class F> static record_scan_result scan(const Table& t,record_cursor& c,unsigned budget,F&& f) {
        record_scan_result result;
        if constexpr(Encoded) {
            auto primary=[](auto h){return merge_key{h.bucket,h.suffix,0,h.fingerprint,true};};
            if constexpr(hierarchy) {
                auto secondary=[&](auto h){return merge_key{.decoded=t.index_.hq_.invert(h)};};
                [[clang::always_inline]]result=t.index_.scan_keys(c,budget,primary,secondary,
                    [](word key){return merge_key{.decoded=key};},f);
            }else {[[clang::always_inline]]result=t.scan_keys(c,budget,primary,f);}
        }else {
            if constexpr(hierarchy){[[clang::always_inline]]result=t.index_.scan(c,budget,f);}
            else {[[clang::always_inline]]result=t.scan_records(c,budget,f);}
        }
        return result;
    }
    template<class Key> static auto small_hash(const Table& t,const Key& key) requires(!hierarchy) {
        if constexpr(std::is_same_v<Key,merge_key>) {
            require(key.encoded,"matching small-list key facts");
            return typename Table::hash_type::parts{key.bucket,key.suffix,key.fingerprint};
        }else return t.hash_.split(key);
    }
    template<class Key,class Prepare,class Effects,class Existing>
    [[gnu::always_inline]] static auto mutate(Table& t,const Key& key,Prepare&& prepare,
            Effects& effects,Existing&& existing) requires(hierarchy) {
        if constexpr(std::is_same_v<Key,merge_key>) {
            auto& index=t.index_;
            using hash=std::remove_cvref_t<decltype(index.hp_)>;
            using secondary_hash=std::remove_cvref_t<decltype(index.hq_)>;
            const auto h=key.encoded?typename hash::parts{key.bucket,key.suffix,key.fingerprint}:index.hp_.split(key.decoded);
            std::optional<word> decoded;
            if(!key.encoded)decoded=key.decoded;
            std::optional<typename secondary_hash::parts> secondary;
            auto full=[&] [[gnu::always_inline]] {if(!decoded)decoded=index.hp_.invert(h);return *decoded;};
            // Normalize the key facts, then use one mutation body. Separate
            // encoded/general bodies duplicated cold admission and hid tiny
            // forwarding lambdas behind per-record calls in the first control.
            return index.mutate_hashed(full,h,[&] [[gnu::always_inline]] {
                if(!secondary)secondary=index.hq_.split(full());return *secondary;
            },prepare,effects,existing);
        }else return t.index_.mutate(key,prepare,effects,existing);
    }
    static auto& bag_owner(Table& t) {
        if constexpr(hierarchy)return t.owner_.bags;
        else return t.bags_;
    }
    static bag_value_window window(const Table& t,std::uint32_t ref) {
        if constexpr(hierarchy)return t.owner_.bags->value_window(ref);
        else return t.bags_->value_window(ref);
    }

    /// One bounded group, one lookup, one reservation. Inputs remain immutable,
    /// disjoint from destination storage, and live through the synchronous call.
    /// Failure commits no logical values; preparation may retain extra capacity.
    template<class Key,class Effects> static std::expected<mutation_delta,mutation_error>
    append(Table& t,const Key& key,std::span<const byte> values,Effects& effects) requires(kind==container_kind::multimap) {
        require(values.size()%width==0,"whole values in merge group");
        const unsigned number=values.size()/width;
        require(number && number<=64,"bounded merge group");
        auto execute=[&] [[gnu::always_inline]] (auto& observer,auto&& mutate) {
            using reservation=typename bags::template reservation<std::remove_cvref_t<decltype(observer)>>;
            struct proposal:record {
                std::optional<reservation> reserved;
                std::array<byte,policy::payload_bytes> prefix{};
                unsigned prefix_bytes=0;
                std::span<const byte> incoming;
                void commit()noexcept {
                    if(reserved)reserved->commit_ranges({prefix.data(),prefix_bytes},incoming);
                }
            };
            auto prepare=[&] [[gnu::always_inline]] (const std::optional<record>& old)->std::optional<proposal> {
                proposal next;next.value_delta=number;next.incoming=values;
                const bool external=old && policy::external(*old);
                const unsigned previous=old && !external?policy::count(*old):0;
                if constexpr(policy::capacity)if(!external && previous+number<=policy::capacity) {
                    if(old)next.payload=old->payload;
                    copy_plain_bytes(next.payload.data()+previous*width,values);
                    next.tag=previous+number==1?0:byte(previous+number);return next;
                }
                auto& owner=bag_owner(t);
                if(!owner)try {
                    owner=std::make_unique<bags>(*t.pool_);
                    effects.before({small_event::kind::bag_owner_created,&t});
                }catch(const std::bad_alloc&){return {};}
                const auto head=external?std::uint32_t(get<4>(old->payload.data())):0;
                const auto hint=external?std::uint32_t(get<4>(old->payload.data()+4)):0;
                // Preserve old inline bytes before reservation can relocate bags.
                next.prefix_bytes=previous*width;
                if(previous)copy_plain_bytes(next.prefix.data(),{old->payload.data(),next.prefix_bytes});
                next.reserved=owner->reserve_values(head,previous+number,observer,hint);
                if(!next.reserved)return {};
                next.tag=1;put<8>(next.payload.data(),word(next.reserved->head_after())|
                    (word(next.reserved->insertion_after())<<32));return next;
            };
            auto existing=[&] [[gnu::always_inline]] (auto& edit)noexcept->existing_record_result {
                if(edit.tag()!=1)return {};
                auto descriptor=edit.template prefix<8>();
                auto reserved=bag_owner(t)->reserve_values(std::uint32_t(get<4>(descriptor.data())),number,
                    observer,std::uint32_t(get<4>(descriptor.data()+4)));
                if(!reserved)return std::unexpected(mutation_error::preparation);
                const word next=word(reserved->head_after())|(word(reserved->insertion_after())<<32);
                reserved->commit_ranges({},values);
                if(next!=get<8>(descriptor.data())) {
                    put<8>(descriptor.data(),next);edit.template patch<8>(0,std::span<const byte,8>(descriptor));
                }
                return mutation_delta{0,number};
            };
            return mutate(prepare,existing);
        };
        std::expected<mutation_delta,mutation_error> result;
        if constexpr(hierarchy) {
            if(!effects_room(effects,Table::index_type::mutation_event_bound+2+bags::reservation_event_bound))
                return std::unexpected(mutation_error::effects);
            promoted_observer<Effects> observed{effects};promoted_observer<Effects,true> observer{effects};
            result=execute(observer,[&] [[gnu::always_inline]] (auto&& prepare,auto&& existing) {
                return mutate(t,key,prepare,observed,existing);
            });
            if(result){t.owner_.keys+=result->keys;t.owner_.values+=result->values;}
        }else {
            if(!effects_room(effects,6+unsigned(HT_LIST_GROW_PRIVATE)+bags::reservation_event_bound))
                return std::unexpected(mutation_error::effects);
            const auto h=small_hash(t,key);const auto found=t.list_.bind().find(h.fingerprint,h.suffix,t.count_);
            const auto before=t.list_.capacity();
            if(found<0 && !t.list_.reserve(*t.pool_,t.count_,t.count_+1,[&](const oa::Allocation& old)noexcept {
                effects.before({small_event::kind::list_write,&t,old.storage,0,old.size});
            }))return std::unexpected(mutation_error::capacity);
            if(before!=t.list_.capacity())effects.before({small_event::kind::list_rebound,&t,
                t.list_.allocation.storage,0,t.list_.allocation.size});
            auto view=t.list_.bind();small_observer<Effects> observer{effects,&t,t.list_.allocation};
            result=execute(observer,[&] [[gnu::always_inline]] (auto&& prepare,auto&&) {
                return view.mutate_at(h,t.count_,found,prepare,observer);
            });
            if(result){t.count_+=result->keys;t.values_.add(result->values);}
        }
        if(result)effects.before({small_event::kind::caller_delta,&t,nullptr,0,0,0,*result});
        return result;
    }
    template<class Key,class Combine,class Effects> [[gnu::always_inline]] static bool accept(Table& t,const Key& key,std::span<const byte> bytes,
            Combine combine,Effects& effects) {
        if constexpr(kind==container_kind::multimap)return bool(append(t,key,bytes,effects));
        else {
            require(bytes.size()==width,"one set/map record per group");
            std::array<byte,width> value;copy_plain_bytes(value.data(),bytes);
            if constexpr(std::is_same_v<Key,merge_key>) {
                if constexpr(hierarchy)return bool(t.set_using([&] [[gnu::always_inline]] (auto&& prepare,auto& observed,auto&& existing) {
                    return mutate(t,key,prepare,observed,existing);
                },value,effects,combine));
                else return bool(t.template apply<Effects,false>(small_hash(t,key),value,effects,combine));
            }else if constexpr(kind==container_kind::map)return bool(t.try_combine_bytes(key,value,combine,effects));
            else if constexpr(hierarchy)return bool(t.try_set_bytes(key,value,effects));
            else return bool(t.set_bytes(key,value,effects));
        }
    }
};

struct value_scan_result {
    record_scan_state state=record_scan_state::yielded;
    unsigned work=0,records=0,values=0;
};
/// A partial bag owns one decoded record, never a payload pointer. The source and its
/// interpretation stay immutable and retained by the caller through completion.
/// Callback spans expire on return. False means no part of that group committed.
template<class Table,bool Encoded=false> class value_cursor {
    using access=consolidation_access<Table>;
    static constexpr unsigned W=access::width;
    typename access::record_cursor records_{};
    using key_type=std::conditional_t<Encoded,typename access::merge_key,word>;
    struct bag_state {
        typename access::record row{};key_type key{};
        std::uint32_t link=0;unsigned offset=0;
        bool active=false,source_done=false;
    };
    struct empty_state {};
    [[no_unique_address]] std::conditional_t<access::kind==container_kind::multimap,bag_state,empty_state> pending_;
  public:
    template<class Consume> value_scan_result step(const Table& source,unsigned budget,unsigned group,Consume&& consume) {
        require(group && group<=64,"bounded value-group size");
        if constexpr(access::kind!=container_kind::multimap) {
            // A set/map row commits atomically inside the ready callback. It
            // needs no copied pending row or second consume stage across yield.
            // One attempted index record is one work unit (headers separate).
            record_scan_result r;
            [[clang::always_inline]]r=access::template scan<Encoded>(source,records_,budget,
                [&](auto key,const auto& row) {return consume(key,std::span<const byte>(row.payload));});
            return {r.state,r.work,r.records,access::kind==container_kind::map?r.records:0};
        }else {
            auto& s=pending_;value_scan_result out;
            for(;;) {
                if(!s.active && s.source_done){out.state=record_scan_state::done;return out;}
                if(out.work==budget)return out;
                if(!s.active) {
                    // A partial bag keeps owned row bytes across yielded steps.
                    auto r=access::template scan<Encoded>(source,records_,1,[&](auto key,const auto& row) {
                        s.row=row;s.key=key;s.offset=0;s.active=true;s.link=0;
                        if(access::policy::external(s.row))s.link=std::uint32_t(get<4>(s.row.payload.data()));
                        return true;
                    });
                    out.work+=r.work;s.source_done=r.state==record_scan_state::done;
                    continue;
                }
                const auto window=s.link?access::window(source,s.link):
                    bag_value_window{{s.row.payload.data(),std::size_t(access::policy::count(s.row))*W},0};
                const unsigned count=window.bytes.size()/W;
                require(s.offset<=count,"retained value cursor offset");
                if(s.offset==count) {
                    ++out.work;s.link=window.next;s.offset=0;
                    if(!s.link){s.active=false;++out.records;}continue;
                }
                const auto n=std::min({count-s.offset,group,budget-out.work});out.work+=n;
                if(!consume(s.key,window.bytes.subspan(std::size_t(s.offset)*W,std::size_t(n)*W))) {
                    out.state=record_scan_state::refused;return out;
                }
                s.offset+=n;out.values+=n;
                if(s.offset==count) {
                    s.link=window.next;s.offset=0;
                    if(!s.link){s.active=false;++out.records;}
                }
            }
        }
    }
};
} // namespace ht
