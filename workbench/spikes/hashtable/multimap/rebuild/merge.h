#pragma once
#include "grow.h"
#include <functional>

namespace write_arch::rebuild {
struct full_value_transfer {
    template<class Table> static auto read_primary(const byte* p,unsigned row) {
        return access::primary_value<Table>(p,row);
    }
    template<class Table> static void write_primary(byte* p,unsigned row,const typename Table::value_type& value) {
        access::write_value<Table>(p,row,value);
    }
};
// Private unpublished destination, same seed/domain and at least as many bucket
// bits as the coherent source. Quotients route directly even across growth.
// On failure the caller must discard the destination. Reduce/transform must
// preserve source coherence and must not reenter or invalidate either backing.
// Owner-relative payloads require an ownership-aware transform outside this leaf.
// Transfer may omit inactive value bytes only in an already fully initialized
// destination. Its read still returns a fully initialized logical value object.
template<class Transfer=full_value_transfer,class Destination,class SourceOwner,class Reduce,class Transform=std::identity>
void merge_into(Destination& output,const SourceOwner& input,Reduce&& reduce,Transform&& transform={}) {
    auto& destination=reader_owner(output);const auto& original=reader_owner(input);
    using Target=std::remove_cvref_t<decltype(destination)>;
    using Source=std::remove_cvref_t<decltype(original)>;
    static_assert(std::is_same_v<typename Target::storage_policy,ht::heap_storage_policy>);
    static_assert(std::is_same_v<typename Source::value_type,typename Target::value_type>);
    static_assert((Source::logical_bucket_count<<Source::K)==(Target::logical_bucket_count<<Target::K));
    static_assert(Source::K>=Target::K);
    constexpr unsigned delta=Source::K-Target::K;
    const auto from=access::get(original);auto to=access::get(destination);
    ht::require(to.mix.seed==from.mix.seed,"same-seed merge");
    ht::require(to.primary.data!=from.primary.data,"distinct merge backing");
    auto hashed=[&](word key,const typename Target::value_type& value){
        typename Target::value_type previous{};const auto place=ht::author::table_access::prepare_write(destination,key,previous);
        const auto next=place.present?reduce(previous,value):value;
        ht::author::table_access::insert_prepared(destination,key,place,next);
    };
    for(std::size_t bucket=0;bucket<Source::bucket_count;++bucket){
        const auto* source=from.primary.data+bucket*Source::stride;
        for(unsigned row=0;row<Source::N;++row)if(live_row<Source>(source,row)){
            const word q=[&]{if constexpr(Source::bitmap)return word(row);
                else return ht::point<Source::encoding>(source,row,Source::N);}();
            const word target_bucket=[&]{
                const word prefix=[](word bucket){if constexpr(delta==64)return word{0};else return bucket<<delta;}(bucket);
                if constexpr(Target::K==64)return prefix;else return prefix|(q>>Target::K);
            }();
            const word reduced=q&ht::mask<Target::K>;
            auto* target=to.primary.data+target_bucket*Target::stride;
            const auto value=transform(Transfer::template read_primary<Source>(source,row));
            if constexpr(Target::bitmap){
                const auto target_row=unsigned(reduced);const bool present=live_row<Target>(target,target_row);
                if constexpr(requires{typename Transfer::borrowed_type;}){
                    if(present)Transfer::template write_primary<Target>(target,target_row,
                        reduce(Transfer::template read_primary<Target>(target,target_row),value));
                    else {occupy<Target>(target,target_row,reduced);Transfer::template write_primary<Target>(target,target_row,value);++to.size;}
                }else {
                    const auto next=present?reduce(Transfer::template read_primary<Target>(target,target_row),value):value;
                    to.size+=!present;occupy<Target>(target,target_row,reduced);Transfer::template write_primary<Target>(target,target_row,next);
                }
            }else {
                const auto found=access::primary_find<Target>(target,reduced);
                if(found!=Target::N){Transfer::template write_primary<Target>(target,found,reduce(Transfer::template read_primary<Target>(target,found),value));continue;}
                const auto free=ht::load<Target::T>(target+Target::flags_offset);
                if(free&&!target[Target::overflow_offset]){
                    const auto next_row=unsigned(std::countr_zero(free));occupy<Target>(target,next_row,reduced);
                    Transfer::template write_primary<Target>(target,next_row,value);++to.size;
                }else {
                    if constexpr(requires{typename Transfer::borrowed_type;})
                        hashed(from.mix.recover(bucket,q),Transfer::template materialize<Target>(value));
                    else hashed(from.mix.recover(bucket,q),value);
                }
            }
        }
    }
    for(std::size_t slot=0;slot<from.groups*16;++slot)if(ht::detail::occupied_control(from.controls.data[slot])){
        const auto* tuple=from.slots.data+slot*Source::tuple_bytes;
        if constexpr(requires{typename Transfer::borrowed_type;})
            hashed(ht::load<Source::key_bytes>(tuple),Transfer::template materialize<Target>(transform(Transfer::read_cell(tuple+Source::key_bytes))));
        else {
            typename Source::value_type value{};
            if constexpr(value.size())std::memcpy(value.data(),tuple+Source::key_bytes,value.size());
            hashed(ht::load<Source::key_bytes>(tuple),transform(value));
        }
    }
}

// Source-preserving same-geometry wrapper. The concrete owner type survives;
// exceptions discard only owned construction state, not callback side effects.
template<class Owner,class Reduce,class Transform=std::identity>
auto merge(const Owner& left,const Owner& right,Reduce&& reduce,Transform&& transform={}) {
    auto result=std::make_unique<Owner>(reader_owner(left).clone());
    merge_into(*result,right,std::forward<Reduce>(reduce),std::forward<Transform>(transform));
    return result;
}
}
