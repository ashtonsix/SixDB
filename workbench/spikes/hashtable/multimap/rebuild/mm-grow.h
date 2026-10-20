#pragma once
#include "../multimap.h"
#include "grow.h"
#include "merge.h"
#ifndef WRITE_ARCH_MERGE_BORROWED
#define WRITE_ARCH_MERGE_BORROWED 0
#endif
#if WRITE_ARCH_MERGE_PHASES
#include "merge-phases.h"
#endif

namespace write_arch {
template<class Reader,unsigned NextBucketBits> struct rebucket_reader;
template<unsigned W,unsigned b,unsigned V,unsigned E,ht::search_kernel Kernel,class Storage,unsigned NextBucketBits>
struct rebucket_reader<ht::table<W,b,V,E,Kernel,Storage>,NextBucketBits> {
    using type=ht::table<W,NextBucketBits,V,E,Kernel,Storage>;
};
// Private construction adapter. It does not make block/rank values portable
// across owners: a single source's directory coordinates are cloned exactly.
// Both owners keep independent block storage, free lists and resolution state.
struct multimap_rebuild {
    // Synchronous borrow only: both source owners and the private destination
    // remain stable until routing finishes. The reference adjustment is applied
    // once, when a cell is written or materialized. Inline bytes are never moved
    // into a temporary on the unique-primary path.
    template<class MM> struct borrowed_cell_transfer {
        struct borrowed_type {const byte* cell;word reference_delta=0;};
        static_assert(sizeof(borrowed_type)==16);
        static borrowed_type read_cell(const byte* cell){return {cell,0};}
        template<class Table> static borrowed_type read_primary(const byte* bucket,unsigned row){
            return read_cell(bucket+Table::value_offset+std::size_t(row)*sizeof(typename Table::value_type));
        }
        template<class Table> static typename Table::value_type materialize(borrowed_type value){
            typename Table::value_type result;
            std::memcpy(result.data(),value.cell,result.size());
            if(MM::is_external(ht::load<8>(value.cell)))
                ht::store<8>(result.data()+8,ht::load<8>(value.cell+8)+value.reference_delta);
            return result;
        }
        template<class Table> static const typename Table::value_type& materialize(const typename Table::value_type& value){return value;}
        template<class Table> static void write_primary(byte* bucket,unsigned row,borrowed_type value){
            auto* target=bucket+Table::value_offset+std::size_t(row)*sizeof(typename Table::value_type);
            const auto meta=ht::load<8>(value.cell);ht::store<8>(target,meta);
            if(MM::is_external(meta))ht::store<8>(target+8,ht::load<8>(value.cell+8)+value.reference_delta);
            else ht::each<MM::inline_count>([&](auto rank){
                if constexpr(MM::inline_count==1)std::memcpy(target+8,value.cell+8,sizeof(typename MM::value_type));
                else if(rank<MM::count(meta))std::memcpy(target+8+rank*sizeof(typename MM::value_type),
                    value.cell+8+rank*sizeof(typename MM::value_type),sizeof(typename MM::value_type));
            });
        }
        template<class Table> static void write_primary(byte* bucket,unsigned row,const typename Table::value_type& value){
            rebuild::full_value_transfer::template write_primary<Table>(bucket,row,value);
        }
    };
    // The existing MM cell is consecutive byte-aligned SeriesPack limbs. An
    // external bag uses only metadata and its owner-relative block/rank; inline
    // bags use metadata plus count values. The reader layout does not change.
    // This is only for initialized private destinations: untouched suffixes
    // retain initialized bytes from construction/clone, never indeterminate data.
    template<class MM> struct live_cell_transfer {
        static constexpr unsigned V=sizeof(typename MM::value_type);
        template<class Table> static auto read_primary(const byte* bucket,unsigned row) {
            static_assert(sizeof(typename Table::value_type)%8==0);
            typename Table::value_type result{};
            const auto* source=bucket+Table::value_offset+std::size_t(row)*result.size();
            const auto meta=ht::load<8>(source);
            ht::store<8>(result.data(),meta);
            if(MM::is_external(meta))std::memcpy(result.data()+8,source+8,8);
            else ht::each<MM::inline_count>([&](auto rank) {
                if constexpr(MM::inline_count==1)std::memcpy(result.data()+8,source+8,V);
                else if(rank<MM::count(meta))std::memcpy(result.data()+8+rank*V,source+8+rank*V,V);
            });
            return result;
        }
        template<class Table> static void write_primary(byte* bucket,unsigned row,const typename Table::value_type& value) {
            static_assert(sizeof(typename Table::value_type)%8==0);
            auto* target=bucket+Table::value_offset+std::size_t(row)*value.size();
            const auto meta=ht::load<8>(value.data());
            ht::store<8>(target,meta);
            if(MM::is_external(meta))std::memcpy(target+8,value.data()+8,8);
            else ht::each<MM::inline_count>([&](auto rank) {
                if constexpr(MM::inline_count==1)std::memcpy(target+8,value.data()+8,V);
                else if(rank<MM::count(meta))std::memcpy(target+8+rank*V,value.data()+8+rank*V,V);
            });
        }
    };
#if SIXDB_TUNE_ZEN5 || SIXDB_TUNE_GRANITE_RAPIDS || SIXDB_TUNE_NEOVERSE_V2
    // Native v30: substantially faster shared-domain joins, at the cost of
    // retaining sparse right blocks. Explicit false keeps left-only reuse.
    static constexpr bool reuse_right_default=true;
#else
    static constexpr bool reuse_right_default=false;
#endif
    template<bool Images,unsigned W,unsigned b,unsigned V,unsigned Inline,class Index>
    static auto grow_index(const Index& index){
        constexpr unsigned cell_bytes=8+std::max(8u,Inline*V);
        using Source=typename Index::reader_owner;
        using Selected=table<W,b+1,cell_bytes,false,false,upsert_flow::split,V,true>;
        using SelectedBase=typename Selected::reader_owner;
        constexpr bool image=Images&&!Source::bitmap&&!SelectedBase::bitmap&&
            Source::B==SelectedBase::B&&Source::T==SelectedBase::T;
        if constexpr(image){
            using Fixed=reused_table<W,b+1,cell_bytes,upsert_flow::split,V,true,
                typename rebucket_reader<Source,b+1>::type>;
            return rebuild::grow<Fixed,true,false,true>(index);
        }else return rebuild::grow<Selected>(index);
    }
    template<class Pool> static void move_buffers(Pool& from,Pool& to)noexcept {
        to.blocks_.swap(from.blocks_);
        std::swap(to.first_free_,from.first_free_);std::swap(to.assigned_bytes_,from.assigned_bytes_);
        std::swap(to.live_buffers_,from.live_buffers_);
        for(unsigned family=0;family<from.families_.size();++family)
            std::swap(to.families_[family].first,from.families_[family].first);
    }
    template<class Pool> static void rebuild_lists(Pool& target)noexcept {
        target.first_free_=Pool::none;for(auto& family:target.families_)family.first=Pool::none;
        for(std::size_t id=0;id<target.blocks_.size();++id){auto& slot=target.blocks_[id];
            if(slot.owner){auto& block=*slot.owner;block.previous=block.next=Pool::none;block.listed=false;
                if(block.returned_count||Pool::assigned(block)<target.families_[block.which].ranks)target.link(id);
            }else {slot.payload=nullptr;slot.next_free=target.first_free_;target.first_free_=id;}
        }
    }
    // Called only after the destination directory and every other fallible
    // object is prepared. Existing payload addresses survive this commit.
    template<class Pool> static void take_buffers(Pool& from,Pool& to,std::size_t offset)noexcept {
        for(std::size_t id=0;id<from.blocks_.size();++id)to.blocks_[offset+id]=std::move(from.blocks_[id]);
        to.assigned_bytes_+=from.assigned_bytes_;to.live_buffers_+=from.live_buffers_;
        from.blocks_.clear();from.assigned_bytes_=from.live_buffers_=0;from.first_free_=Pool::none;
        for(auto& family:from.families_)family.first=Pool::none;
    }
    template<class MM,class Visit> static void visit_bags(const MM& source,Visit&& visit){
        source.index_.visit([&](word key,const auto& cell){
            const auto meta=ht::load<8>(cell.data());const auto n=MM::count(meta);
            const auto* data=MM::is_external(meta)?source.buffers_.data(ht::load<8>(cell.data()+8),MM::family(meta)):cell.data()+8;
            visit(key,std::span<const byte>{data,std::size_t(n)*sizeof(typename MM::value_type)});
        });
    }
    template<unsigned V,bool Coalesced>
    static void clone_buffers(const capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>& source,
            capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>& target){
        using Pool=capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>;
        ht::require(target.blocks_.empty(),"fresh multimap destination pool");
        target.families_=source.families_;
        target.blocks_.resize(source.blocks_.size());
        for(std::size_t id=0;id<source.blocks_.size();++id){
            const auto& from=source.blocks_[id];auto& to=target.blocks_[id];to.next_free=from.next_free;
            if(!from.owner)continue;
            const auto& b=*from.owner;const auto& family=source.families_[b.which];
            auto copy=std::make_unique<typename Pool::block>(b.which,family.capacity*V*family.ranks);
            // Only the two-byte control and assigned, fully initialized
            // reservations belong to the representation. No unassigned suffix
            // or allocator/header padding is read from the source.
            std::memcpy(copy->storage.data,b.storage.data,2);
            std::memcpy(copy->storage.data+64,b.storage.data+64,std::size_t(Pool::assigned(b))*family.capacity*V);
            copy->returned=b.returned;copy->returned_count=b.returned_count;copy->live=b.live;
            copy->previous=b.previous;copy->next=b.next;copy->listed=b.listed;
            to.payload=copy->storage.data+64;to.owner=std::move(copy);
        }
        target.first_free_=source.first_free_;target.assigned_bytes_=source.assigned_bytes_;
        target.live_buffers_=source.live_buffers_;
    }

    // Import one coherent worker's blocks into a disjoint ID interval. A rank
    // keeps its meaning because each whole block's reservation layout survives.
    template<unsigned V,bool Coalesced>
    static std::size_t import_buffers(const capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>& source,
            capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>& target){
        using Pool=capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>;
        const auto offset=target.blocks_.size();
        ht::require(source.blocks_.size()<=(~word{0}>>8)&&offset<=(~word{0}>>8)-source.blocks_.size(),"imported block ID range");
        target.blocks_.resize(offset+source.blocks_.size());
        for(std::size_t id=0;id<source.blocks_.size();++id){
            const auto& from=source.blocks_[id];auto& to=target.blocks_[offset+id];
            if(!from.owner)continue;
            const auto& b=*from.owner;const auto& f=source.families_[b.which];
            auto copy=std::make_unique<typename Pool::block>(b.which,f.capacity*V*f.ranks);
            std::memcpy(copy->storage.data,b.storage.data,2);
            std::memcpy(copy->storage.data+64,b.storage.data+64,std::size_t(Pool::assigned(b))*f.capacity*V);
            copy->returned=b.returned;copy->returned_count=b.returned_count;copy->live=b.live;
            to.payload=copy->storage.data+64;to.owner=std::move(copy);
        }
        target.assigned_bytes_+=source.assigned_bytes_;target.live_buffers_+=source.live_buffers_;
        rebuild_lists(target);
        return offset;
    }

    // Exclusive consuming worker merge. Both non-null sources must have no
    // surviving borrows. Before the no-fail ownership commit, source blocks and
    // metadata remain untouched: duplicate bags use fresh reservations or deferred
    // fitting reuse of an input reservation.
    // Unique bags keep their original payload allocation. A failure preserves
    // both input owners; success clears both unique_ptrs.
    // KeepBuckets is an explicit caller capacity choice, useful for shared key
    // domains. It preserves the original encoding and primary geometry; excess
    // union keys still spill normally. It does not estimate distinct overlap or
    // promise that the caller's preferred load bound remains satisfied.
    template<bool Images=true,bool ReuseFitting=true,bool BulkJoin=true,bool KeepBuckets=false,bool ReuseRight=ReuseFitting&&reuse_right_default,bool LiveCells=false,unsigned W,unsigned b,unsigned V,unsigned Inline,bool Coalesced,class Index,bool Cached>
    static auto merge_consuming(
            std::unique_ptr<multimap<W,b,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,Index,Cached>>& left,
            std::unique_ptr<multimap<W,b,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,Index,Cached>>& right){
#if WRITE_ARCH_MERGE_PHASES
        merge_diagnostic::phases phases;
#endif
        using MM=multimap<W,b,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,Index,Cached>;
        using Pool=typename MM::buffer_owner;
        ht::require(left&&right&&left.get()!=right.get(),"distinct consuming merge sources");
        ht::require(rebuild::access::get(rebuild::reader_owner(left->index_)).mix.seed==
            rebuild::access::get(rebuild::reader_owner(right->index_)).mix.seed,"same-seed consuming merge");
        const auto offset=left->buffers_.blocks_.size(),right_size=right->buffers_.blocks_.size();
        ht::require(right_size<=(~word{0}>>8)&&offset<=(~word{0}>>8)-right_size,"merged block ID range");
        const auto imported=offset+right_size;
        Pool pool;pool.blocks_.resize(imported);
        std::vector<word> released;const auto limit=std::min(left->size(),right->size());
        ht::require(limit<=released.max_size()/2,"merged release coordinates");released.reserve(limit*2);
        // Fitting duplicates keep the left reservation. Defer writes until
        // all fallible index/root preparation has completed. External right
        // payloads stay stable; short inline right values are snapshotted here.
        struct no_shift {};
        struct deferred_append {byte* destination;const byte* source;std::size_t bytes;ht::bytes<std::max(8u,Inline*V)> inline_values;
            [[no_unique_address]] std::conditional_t<ReuseRight,std::size_t,no_shift> right_bytes_to_move;};
        std::vector<deferred_append> appends;
        if constexpr(ReuseFitting||ReuseRight)appends.reserve(limit);
#if WRITE_ARCH_MERGE_PHASES
        phases.end(0); // directory and deferred-plan reservation
#endif
        auto index=[&]{
            if constexpr(KeepBuckets)return std::make_unique<Index>(rebuild::reader_owner(left->index_).clone());
            else return grow_index<Images,W,b,V,Inline>(left->index_);
        }();
#if WRITE_ARCH_MERGE_PHASES
        phases.end(1); // left index clone or image growth, including allocation
#endif
        using Next=multimap<W,b+!KeepBuckets,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,typename decltype(index)::element_type,Cached>;
        auto remap=[&](auto cell){if(MM::is_external(ht::load<8>(cell.data()))){const auto old=ht::load<8>(cell.data()+8);
                ht::store<8>(cell.data()+8,(((old>>8)+offset)<<8)|(old&255));}return cell;};
        auto resolve=[&](word ref,unsigned which)->const byte* {
            const auto id=ref>>8;
            if(id<offset)return left->buffers_.data(ref,which);
            if(id<imported)return right->buffers_.data(((id-offset)<<8)|(ref&255),which);
            return pool.data(ref,which);
        };
        auto join=[&](const auto& a,const auto& z){
            const auto am=ht::load<8>(a.data()),zm=ht::load<8>(z.data());
            const unsigned an=MM::count(am),zn=MM::count(zm),total=an+zn;
            ht::require(total<=Pool::max_values,"merged bag maximum");
            const bool ae=MM::is_external(am),ze=MM::is_external(zm);
            const auto ar=ae?ht::load<8>(a.data()+8):0,zr=ze?ht::load<8>(z.data()+8):0;
            const auto* ad=ae?resolve(ar,MM::family(am)):a.data()+8;
            const auto* zd=ze?resolve(zr,MM::family(zm)):z.data()+8;
            auto result=a;
            if constexpr(Inline)if(total<=Inline){
                std::memcpy(result.data()+8+std::size_t(an)*V,zd,std::size_t(zn)*V);
                ht::store<8>(result.data(),total);return result;
            }
            if constexpr(ReuseFitting)if(ae&&total<=left->buffers_.capacity(MM::family(am))){
                auto& append=appends.emplace_back();append.destination=const_cast<byte*>(ad)+std::size_t(an)*V;
                append.bytes=std::size_t(zn)*V;append.source=ze?zd:nullptr;
                if constexpr(ReuseRight)append.right_bytes_to_move=0;
                if(!ze)std::memcpy(append.inline_values.data(),zd,append.bytes);
                ht::store<8>(result.data(),MM::metadata(total,MM::family(am)));
                if(ze)released.push_back(zr);return result;
            }
            // A larger right reservation can hold left-then-right too. Its
            // original values move with memmove only after all fallible work;
            // inline left values must survive the reducer's temporary cell.
            if constexpr(ReuseRight)if(ze&&total<=right->buffers_.capacity(MM::family(zm))){
                auto& append=appends.emplace_back();append.destination=const_cast<byte*>(zd);
                append.bytes=std::size_t(an)*V;append.source=ae?ad:nullptr;
                append.right_bytes_to_move=std::size_t(zn)*V;
                if(!ae)std::memcpy(append.inline_values.data(),ad,append.bytes);
                ht::store<8>(result.data()+8,zr);ht::store<8>(result.data(),MM::metadata(total,MM::family(zm)));
                if(ae)released.push_back(ar);return result;
            }
            const auto which=Pool::class_for(total);
            const auto next=[&]{
                if constexpr(BulkJoin)return pool.create_join(which,{ad,std::size_t(an)*V},{zd,std::size_t(zn)*V});
                else {ht::bytes<V> first;std::memcpy(first.data(),zd,V);
                    const auto ref=pool.create(which,{ad,std::size_t(an)*V},first);
                    if(zn>1)std::memcpy(pool.data(ref,which)+std::size_t(an+1)*V,zd+V,std::size_t(zn-1)*V);
                    return ref;}
            }();
            ht::store<8>(result.data()+8,next);ht::store<8>(result.data(),MM::metadata(total,which));
            if(ae)released.push_back(ar);if(ze)released.push_back(zr);return result;
        };
        if constexpr(WRITE_ARCH_MERGE_BORROWED&&!LiveCells){
            using Transfer=borrowed_cell_transfer<MM>;
            using Target=typename decltype(index)::element_type::reader_owner;
            auto borrowed_remap=[&](typename Transfer::borrowed_type cell){cell.reference_delta=word(offset)<<8;return cell;};
            auto borrowed_join=[&](const auto& a,const auto& z){
                return join(Transfer::template materialize<Target>(a),Transfer::template materialize<Target>(z));
            };
            rebuild::merge_into<Transfer>(*index,right->index_,borrowed_join,borrowed_remap);
        }else if constexpr(LiveCells)rebuild::merge_into<live_cell_transfer<MM>>(*index,right->index_,join,remap);
        else rebuild::merge_into(*index,right->index_,join,remap);
        auto result=std::unique_ptr<Next>(new Next(typename Next::rebuilt_index_tag{},std::move(*index)));
#if WRITE_ARCH_MERGE_PHASES
        phases.end(2); // right routing, duplicate preparation and result owner
#endif
        // Every allocation/validation is complete. The first deferred copy is
        // the commit boundary and still writes a source-owned reservation.
        // Exclusive ownership covers it; future guarded integration must publish
        // source-qualified damage before this copy, not just before transfer.
        // Do not add a fallible step before clearing the sources.
        if constexpr(ReuseFitting||ReuseRight)for(const auto& append:appends){
            if constexpr(ReuseRight)if(append.right_bytes_to_move)
                std::memmove(append.destination+append.bytes,append.destination,append.right_bytes_to_move);
            std::memcpy(append.destination,append.source?append.source:append.inline_values.data(),append.bytes);
        }
#if WRITE_ARCH_MERGE_PHASES
        phases.end(3); // no-fail deferred payload writes
#endif
        take_buffers(left->buffers_,pool,0);take_buffers(right->buffers_,pool,offset);rebuild_lists(pool);
        for(auto ref:released)pool.release(ref);
        move_buffers(pool,result->buffers_);left.reset();right.reset();
#if WRITE_ARCH_MERGE_PHASES
        phases.end(4); // ownership, free lists, releases and source destruction
#endif
        return result;
    }

    // Left then right concatenation, preserving duplicates. The new index has
    // twice the bucket count; both source pools and indexes remain unchanged.
    template<bool Routed=true,unsigned W,unsigned b,unsigned V,unsigned Inline,bool Coalesced,class Index,bool Cached>
    static auto merge(const multimap<W,b,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,Index,Cached>& left,
            const multimap<W,b,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,Index,Cached>& right){
        static_assert(W>b);
        using MM=multimap<W,b,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,Index,Cached>;
        using NextIndex=table<W,b+1,8+std::max(8u,Inline*V),false,false,upsert_flow::split,V,true>;
        using Next=multimap<W,b+1,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,NextIndex,Cached>;
        const auto left_state=rebuild::access::get(rebuild::reader_owner(left.index_));
        ht::require(left_state.mix.seed==rebuild::access::get(rebuild::reader_owner(right.index_)).mix.seed,"same-seed multimap merge");
        typename MM::buffer_owner pool;clone_buffers(left.buffers_,pool);
        const auto offset=import_buffers(right.buffers_,pool);
        auto index=rebuild::grow<typename Next::index_type>(left.index_);
        auto remap=[&](auto cell){if(MM::is_external(ht::load<8>(cell.data()))){
                const auto old=ht::load<8>(cell.data()+8);
                ht::store<8>(cell.data()+8,(((old>>8)+offset)<<8)|(old&255));}
            return cell;};
        auto join=[&](const auto& a,const auto& z){
            const auto am=ht::load<8>(a.data()),zm=ht::load<8>(z.data());
            const unsigned an=MM::count(am),zn=MM::count(zm),total=an+zn;
            ht::require(total<=MM::buffer_owner::max_values,"merged bag maximum");
            const bool ae=MM::is_external(am),ze=MM::is_external(zm);
            const auto ar=ae?ht::load<8>(a.data()+8):0,zr=ze?ht::load<8>(z.data()+8):0;
            const auto* ad=ae?pool.data(ar,MM::family(am)):a.data()+8;
            const auto* zd=ze?pool.data(zr,MM::family(zm)):z.data()+8;
            auto result=a;
            if constexpr(Inline)if(total<=Inline){
                std::memcpy(result.data()+8+std::size_t(an)*V,zd,std::size_t(zn)*V);
                ht::store<8>(result.data(),total);return result;
            }
            if(ae&&total<=pool.capacity(MM::family(am))){
                std::memcpy(pool.data(ar,MM::family(am))+std::size_t(an)*V,zd,std::size_t(zn)*V);
                ht::store<8>(result.data(),MM::metadata(total,MM::family(am)));
            }else {
                const auto which=MM::buffer_owner::class_for(total);
                ht::bytes<V> first;std::memcpy(first.data(),zd,V);
                const auto next=pool.create(which,{ad,std::size_t(an)*V},first);
                // One contiguous copy for the rest of the right bag; the
                // already assigned reservation is fully initialized and private.
                if(zn>1)std::memcpy(pool.data(next,which)+std::size_t(an+1)*V,zd+V,std::size_t(zn-1)*V);
                ht::store<8>(result.data()+8,next);ht::store<8>(result.data(),MM::metadata(total,which));
                if(ae)pool.release(ar);
            }
            if(ze)pool.release(zr);
            return result;
        };
        if constexpr(Routed)rebuild::merge_into(*index,right.index_,join,remap);
        else {
            auto& target=rebuild::reader_owner(*index);
            right.index_.visit([&](word key,const auto& cell){
                const auto value=remap(cell);typename Next::index_type::value_type previous{};
                const auto place=ht::author::table_access::prepare_write(target,key,previous);
                ht::author::table_access::insert_prepared(target,key,place,place.present?join(previous,value):value);
            });
        }
        auto result=std::unique_ptr<Next>(new Next(typename Next::rebuilt_index_tag{},std::move(*index)));
        move_buffers(pool,result->buffers_);return result;
    }

    template<bool Images=false,unsigned W,unsigned b,unsigned V,unsigned Inline,bool Coalesced,class Index,bool Cached>
    static auto grow(const multimap<W,b,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,Index,Cached>& source){
        static_assert(W>b);
        auto index=grow_index<Images,W,b,V,Inline>(source.index_);
        using Next=multimap<W,b+1,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,typename decltype(index)::element_type,Cached>;
        auto result=std::unique_ptr<Next>(new Next(typename Next::rebuilt_index_tag{},std::move(*index)));
        clone_buffers(source.buffers_,result->buffers_);
        return result;
    }

    // Exclusive consuming growth requires a non-null source and invalidates
    // every old borrow (unique_ptr ownership alone does not prove this). All
    // fallible index/root construction completes before pool ownership moves;
    // failure preserves the source. Payloads and block/rank coordinates stay put.
    template<bool Images=false,unsigned W,unsigned b,unsigned V,unsigned Inline,bool Coalesced,class Index,bool Cached>
    static auto grow_consuming(std::unique_ptr<multimap<W,b,V,false,Inline,
            buffer_representation::streampack_capacity,false,Coalesced,Index,Cached>>& source){
        static_assert(W>b);
        auto index=grow_index<Images,W,b,V,Inline>(source->index_);
        using Next=multimap<W,b+1,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,typename decltype(index)::element_type,Cached>;
        auto result=std::unique_ptr<Next>(new Next(typename Next::rebuilt_index_tag{},std::move(*index)));
        move_buffers(source->buffers_,result->buffers_);
        source.reset();return result;
    }

    template<bool EnumerateValues,unsigned W,unsigned b,unsigned V,unsigned Inline,bool Coalesced,class Index,bool Cached>
    static auto enumerate(const multimap<W,b,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,Index,Cached>& source){
        using NextIndex=table<W,b+1,8+std::max(8u,Inline*V),false,false,upsert_flow::split,V,true>;
        using Next=multimap<W,b+1,V,false,Inline,buffer_representation::streampack_capacity,false,Coalesced,NextIndex,Cached>;
        const auto seed=rebuild::access::get(rebuild::reader_owner(source.index_)).mix.seed;
        auto result=std::make_unique<Next>(seed);
        if constexpr(EnumerateValues){
            visit_bags(source,[&](word key,std::span<const byte> bag){
                for(std::size_t at=0;at<bag.size();at+=V){ht::bytes<V> value;std::memcpy(value.data(),bag.data()+at,V);result->insert(key,value);}
            });
        }else {
            source.index_.visit([&](word key,const auto& cell){result->index_.insert(key,cell);});
            clone_buffers(source.buffers_,result->buffers_);
        }
        return result;
    }
};
}
