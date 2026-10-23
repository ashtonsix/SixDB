#pragma once
#include "../table.h"
#ifndef WRITE_ARCH_GUARDED_PRIMARY
#define WRITE_ARCH_GUARDED_PRIMARY 0
#endif
#ifndef WRITE_ARCH_GUARDED_TYPED
#define WRITE_ARCH_GUARDED_TYPED 0
#endif

namespace write_arch {
// Private selection for an exclusive, unchanged index. No key/slot pointer is
// exposed to callers. The enclosing prepared owner checks its head before any
// dereference and admits all index and payload effects before this commit.
struct guarded_index_access {
#if WRITE_ARCH_GUARDED_TYPED
    template<class Index> struct existing_primary {
        byte* cell=nullptr;
        std::size_t offset=0;
        static constexpr bool present=true,needs_capacity=false;
    };
    template<class Index> [[gnu::always_inline]] static existing_primary<Index> primary_existing(Index& index,word key){
        using Base=typename Index::reader_owner;
        const auto h=index.state().mix(key);
        auto* bucket=index.bucket(h.bucket);
        unsigned row;
#if WRITE_ARCH_MASK_FIRST_CELL && defined(__x86_64__)
        if constexpr(!Base::bitmap){
            const auto matches=Index::matching_rows(bucket,h.quotient);
            if(!matches)return {};
            row=unsigned(std::countr_zero(matches));
        }else
#endif
        {
            row=Index::locate(bucket,h.quotient);
            if(row==Index::N)return {};
        }
        const auto offset=h.bucket*Index::stride+Index::B+std::size_t(row)*sizeof(typename Index::value_type);
        return {index.state().primary.data+offset,offset};
    }
    template<class Index,class Emit> [[gnu::always_inline]] static void damage(const Index& index,
            const existing_primary<Index>& plan,const void* owner,Emit&& emit){
        const auto state=index.state();
        emit(ikea::journal::entry{&index,unsigned(ht::plane::primary),plan.offset,sizeof(typename Index::value_type)});
    }
    template<class Index,class Edit> [[gnu::always_inline]] static void commit(Index&,
            const existing_primary<Index>& plan,Edit&& edit)noexcept {
        static_assert(noexcept(edit(plan.cell)));
        edit(plan.cell);
    }
#endif
    template<class Index> struct selection {
        typename Index::hash_result hash;
        typename Index::swiss_position swiss{Index::absent,false};
        word key=0;
        unsigned row=Index::N;
        bool present=false,needs_capacity=false;
        byte* cell=nullptr;
    };
#if WRITE_ARCH_GUARDED_TYPED
    // The enclosing session has requalified this existing primary observation
    // under its current grant. This conversion performs no new lookup.
    template<class Index> [[gnu::always_inline]] static existing_primary<Index> primary_observed(
            Index& index,const selection<Index>& selected)noexcept{
        return {selected.cell,std::size_t(selected.cell-index.state().primary.data)};
    }
#endif
    template<class Index> static auto prepare(Index& index,word key,typename Index::hash_result hash){
        using Base=typename Index::reader_owner;
        selection<Index> result;result.key=key;result.hash=hash;
        auto* bucket=index.bucket(result.hash.bucket);
        result.row=Index::locate(bucket,result.hash.quotient);
        if constexpr(!Base::bitmap)if(result.row==Index::N&&bucket[Index::B-1]!=byte{0})
            result.swiss=index.resolve_swiss(key,result.hash);
        result.present=result.row!=Index::N||result.swiss.present;
        if(!result.present){
            if constexpr(Base::bitmap)result.row=unsigned(result.hash.quotient);
            else {
                const auto free=Index::vacancies(bucket);
                if(free)result.row=unsigned(std::countr_zero(free));
                if(result.row==Index::N){
                    const auto state=index.state();
                    if(!state.groups||(state.spill+state.deleted+1)*8>state.groups*16*7){
                        result.needs_capacity=true;return result;
                    }
                    if(result.swiss.slot==Index::absent)result.swiss=index.resolve_swiss(key,result.hash);
                }
            }
        }
        result.cell=result.row!=Index::N?bucket+Index::B+result.row*sizeof(typename Index::value_type):
            index.state().slots.data+result.swiss.slot*Index::tuple_bytes+Index::key_bytes;
        return result;
    }
    template<class Index> [[gnu::noinline]] static auto prepare_overflow(Index& index,word key){
        // Reuse the complete existing resolver, including duplicate search
        // behind primary holes and refusal before any capacity-changing store.
        return prepare(index,key,index.state().mix(key));
    }
#if WRITE_ARCH_GUARDED_TYPED
    template<class Index> [[gnu::always_inline]] static auto prepare_complete(Index& index,word key){
        return prepare(index,key,index.state().mix(key));
    }
#endif
    template<class Index>
#if WRITE_ARCH_GUARDED_PRIMARY
    [[gnu::always_inline]]
#endif
    static auto prepare(Index& index,word key){
#if WRITE_ARCH_GUARDED_PRIMARY
        using Base=typename Index::reader_owner;
        const auto h=index.state().mix(key);
        auto* bucket=index.bucket(h.bucket);
        selection<Index> result;result.key=key;
        // Only these two hash fields belong to a primary selection. Keep the
        // unused Swiss fields initialized, allowing the high product to move
        // into the existing overflow resolver. Prepared commits never re-route.
        result.hash={h.bucket,h.quotient,0};
        if constexpr(Base::bitmap){
            result.row=unsigned(h.quotient);result.present=Index::locate(bucket,h.quotient)!=Index::N;
            result.cell=bucket+Index::B+result.row*sizeof(typename Index::value_type);
            return result;
        }else {
#if WRITE_ARCH_MASK_FIRST_CELL && defined(__x86_64__)
            const auto matches=Index::matching_rows(bucket,h.quotient);
            if(matches){
                result.row=unsigned(std::countr_zero(matches));result.present=true;
                result.cell=bucket+Index::B+result.row*sizeof(typename Index::value_type);return result;
            }
#else
            result.row=Index::locate(bucket,h.quotient);
            if(result.row!=Index::N){
                result.present=true;result.cell=bucket+Index::B+result.row*sizeof(typename Index::value_type);return result;
            }
#endif
            const auto free=Index::vacancies(bucket);
            if(free&&bucket[Index::B-1]==byte{0}){
                result.row=unsigned(std::countr_zero(free));
                result.cell=bucket+Index::B+result.row*sizeof(typename Index::value_type);return result;
            }
            return prepare_overflow(index,key);
        }
#else
        return prepare(index,key,index.state().mix(key));
#endif
    }
    template<class Index,class Cell> static auto observed_existing(Index& index,word key,
            typename Index::hash_result hash,Cell cell)noexcept {
        selection<Index> selected;selected.hash=hash;selected.key=key;
        selected.present=true;selected.cell=const_cast<byte*>(cell.address());
        if constexpr(std::is_same_v<Cell,typename Index::template const_cell<true>>)selected.row=cell.row;
        else selected.swiss={std::size_t(cell.address()-index.state().slots.data-Index::key_bytes)/Index::tuple_bytes,true};
        return selected;
    }
    template<class Index,class Emit> [[gnu::always_inline]] static void damage(const Index& index,const selection<Index>& plan,const void* owner,Emit&& emit){
        const auto state=index.state();
        auto add=[&](ht::plane part,const byte* base,std::size_t extent,std::size_t offset,std::size_t size) __attribute__((always_inline)) {
            emit(ikea::journal::entry{&index,unsigned(part),offset,size});
        };
        if(plan.row!=Index::N){
            const auto offset=plan.hash.bucket*Index::stride;
            if(plan.present)add(ht::plane::primary,state.primary.data,state.primary.size,
                offset+Index::B+plan.row*sizeof(typename Index::value_type),sizeof(typename Index::value_type));
            else add(ht::plane::primary,state.primary.data,state.primary.size,offset,Index::stride);
        }else {
            add(ht::plane::swiss_tuples,state.slots.data,state.slots.size,
                plan.swiss.slot*Index::tuple_bytes+(plan.present?Index::key_bytes:0),
                plan.present?sizeof(typename Index::value_type):Index::tuple_bytes);
            if(!plan.present){
                add(ht::plane::swiss_controls,state.controls.data,state.controls.size,plan.swiss.slot,1);
                if(index.bucket(plan.hash.bucket)[Index::B-1]!=byte{255})
                    add(ht::plane::primary,state.primary.data,state.primary.size,plan.hash.bucket*Index::stride+Index::B-1,1);
            }
        }
        // The enclosing MM header covers counters inside the embedded index.
    }
    template<class Index,class Edit> [[gnu::always_inline]] static void commit(Index& index,const selection<Index>& plan,Edit&& edit)noexcept {
        using Base=typename Index::reader_owner;
        static_assert(noexcept(edit(plan.cell)));
        edit(plan.cell);
        if(plan.present)return;
        auto* bucket=index.bucket(plan.hash.bucket);
        if(plan.row!=Index::N){
            if constexpr(Base::bitmap)bucket[plan.row/8]|=byte(1u<<(plan.row%8));
            else {assign<Index::E>(bucket,plan.row,Index::N,plan.hash.quotient);
                bucket[Index::flags+plan.row/8]&=byte(~(1u<<(plan.row%8)));}
        }else {
            const auto state=index.state();state.deleted-=state.controls.data[plan.swiss.slot]==Index::tombstone;
            ht::store<Index::key_bytes>(state.slots.data+plan.swiss.slot*Index::tuple_bytes,plan.key);
            state.controls.data[plan.swiss.slot]=byte(plan.hash.fingerprint());++state.spill;
            if(bucket[Index::B-1]!=byte{255})bucket[Index::B-1]=byte(number(bucket[Index::B-1])+1);
        }
        ++index.state().size;
    }
};
}
