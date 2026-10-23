#pragma once
#include "../capacity-buffers.h"
#ifndef WRITE_ARCH_POOL_ASSIGNMENT
#define WRITE_ARCH_POOL_ASSIGNMENT 0
#endif
#ifndef WRITE_ARCH_POOL_BORROWED
#define WRITE_ARCH_POOL_BORROWED 0
#endif

namespace write_arch {
// Private, exclusive-session preparation. It selects a live free rank without
// taking it and owns every detached allocation itself. Destruction never writes
// to the pool. The enclosing session must forbid intervening pool mutations;
// source identity/revision and full damage admission belong to that session.
struct guarded_pool_access {
    template<class Pool,class Visit> static void sources(const Pool& pool,const void* owner,Visit&& visit) {
        auto add=[&](const void* source,unsigned plane,const void* base,std::size_t extent){
            if(extent)visit(physical_region{source,plane,base,extent});
        };
        add(&pool,1,pool.blocks_.data(),pool.blocks_.capacity()*sizeof(typename Pool::entry));
        for(const auto& slot:pool.blocks_)if(slot.owner){
            add(slot.owner.get(),0,slot.owner.get(),sizeof(typename Pool::block));
            add(slot.owner.get(),1,slot.owner->storage.data,slot.owner->storage.size);
        }
    }
    template<class Pool> struct reservation {
        using block=typename Pool::block;
        using entry=typename Pool::entry;
        Pool* owner=nullptr;
        unsigned which=0,rank=0,assigned=0;
        std::size_t id=Pool::none;
        bool reused=false,new_slot=false;
        std::unique_ptr<block> detached;
        // If the descriptor vector needs to grow, allocate an empty destination
        // privately. Existing unique_ptrs move only after damage admission.
        std::vector<entry> directory;
        block* destination=nullptr;
        byte* payload=nullptr;
        unsigned bytes=0;
        std::span<const byte> prefix;
        reservation()=default;
        reservation(const reservation&)=delete;
        reservation& operator=(const reservation&)=delete;
        reservation(reservation&&)=default;
        reservation& operator=(reservation&&)=default;
    };

#if WRITE_ARCH_POOL_BORROWED
    // Synchronous selection of an existing free rank. No detached ownership,
    // directory allocation or destruction is needed on this branch. The same
    // exclusive grant that protects preparation protects admission and commit.
    template<class Pool> struct borrowed_reservation {
        Pool* owner;
        typename Pool::block* destination;
        std::span<const byte> prefix;
        std::size_t id;
        unsigned which,rank,assigned,bytes;
        bool reused;
        constexpr explicit operator bool()const noexcept{return true;}
        borrowed_reservation& operator*()noexcept{return *this;}
        const borrowed_reservation& operator*()const noexcept{return *this;}
        borrowed_reservation* operator->()noexcept{return this;}
        const borrowed_reservation* operator->()const noexcept{return this;}
    };
    template<unsigned V,bool Coalesced>
    [[gnu::always_inline]] static bool prepare_borrowed(
            capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>& pool,
            unsigned which,std::span<const byte> prefix,
            borrowed_reservation<capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>>& result){
        using Pool=capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>;
        ht::require(which<pool.families_.size(),"prepared pool family");
        const auto& family=pool.families_[which];
        if(family.first==Pool::none)return false;
        result.owner=&pool;result.which=which;result.id=family.first;
        ht::require(result.id<=(~word{0}>>8),"prepared block reference width");
        result.destination=pool.blocks_[result.id].owner.get();result.bytes=family.capacity*V;
        ht::require(prefix.size()%V==0&&prefix.size()<=result.bytes-V,"prepared pool payload extent");
        result.prefix=prefix;result.assigned=Pool::assigned(*result.destination);
        result.reused=result.destination->returned_count!=0;
        result.rank=result.reused?result.destination->returned[result.destination->returned_count-1]:result.assigned;
        ht::require(result.rank<family.ranks,"prepared pool rank");
        ht::require(!ht::input_overlaps(prefix,result.destination->storage.data+64+result.rank*result.bytes,result.bytes),
            "prepared source is disjoint from free reservation");
        return true;
    }
    template<class Pool,class Emit> [[gnu::always_inline]] static void damage(
            const borrowed_reservation<Pool>& plan,const void* owner,Emit&& emit){
        const auto& pool=*plan.owner;const auto& b=*plan.destination;
        auto add=[&](const void* source,unsigned plane,const void* base,std::size_t extent,std::size_t offset,std::size_t size) __attribute__((always_inline)) {
            emit(write_effect({source,plane,base,extent},offset,size));
        };
        add(&b,0,&b,sizeof(b),0,sizeof(b));add(&b,1,b.storage.data,b.storage.size,64+plan.rank*plan.bytes,plan.bytes);
        if(!plan.reused)add(&b,1,b.storage.data,b.storage.size,0,2);
        if(b.returned_count<=1&&plan.assigned+unsigned(!plan.reused)==pool.families_[plan.which].ranks){
            auto metadata=[&](std::size_t id) __attribute__((always_inline)) {
                const auto& neighbor=*pool.blocks_[id].owner;add(&neighbor,0,&neighbor,sizeof(neighbor),0,sizeof(neighbor));
            };
            if(b.previous!=Pool::none)metadata(b.previous);
            if(b.next!=Pool::none)metadata(b.next);
        }
    }
    template<class Pool,class Retire> [[gnu::always_inline]] static void directory_retirement(
            const borrowed_reservation<Pool>&,const void*,Retire&&)noexcept{}
#endif

    template<unsigned V,bool Coalesced>
    static auto prepare(capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>& pool,unsigned which,std::span<const byte> prefix) {
        using Pool=capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>;
        reservation<Pool> result;result.owner=&pool;result.which=which;
        ht::require(which<pool.families_.size(),"prepared pool family");
        const auto& family=pool.families_[which];result.bytes=family.capacity*V;
        ht::require(prefix.size()%V==0&&prefix.size()<=result.bytes-V,"prepared pool payload extent");result.prefix=prefix;
        result.id=family.first;
        if(result.id==Pool::none){
            result.detached=std::make_unique<typename Pool::block>(which,result.bytes*family.ranks);
            auto source=pool.view(*result.detached);ikea::no_effects effects;
            stream::initialize_unchecked(source,effects);
            result.id=pool.first_free_==Pool::none?pool.blocks_.size():pool.first_free_;
            result.new_slot=result.id==pool.blocks_.size();
            if(result.new_slot&&pool.blocks_.size()==pool.blocks_.capacity()){
                ht::require(pool.blocks_.size()<pool.blocks_.max_size(),"pool descriptor capacity");
                const auto current=std::max(std::size_t(1),pool.blocks_.size());
                const auto next=current>pool.blocks_.max_size()/2?pool.blocks_.max_size():current*2;
                result.directory.reserve(next);result.directory.resize(pool.blocks_.size()+1);
            }
            result.destination=result.detached.get();
        }else result.destination=pool.blocks_[result.id].owner.get();
        ht::require(result.id<=(~word{0}>>8),"prepared block reference width");
        result.assigned=Pool::assigned(*result.destination);
        result.reused=result.destination->returned_count!=0;
        result.rank=result.reused?result.destination->returned[result.destination->returned_count-1]:result.assigned;
        ht::require(result.rank<family.ranks,"prepared pool rank");
        result.payload=result.destination->storage.data+64+result.rank*result.bytes;
        ht::require(!ht::input_overlaps(prefix,result.payload,result.bytes),"prepared source is disjoint from free reservation");
        return result;
    }

    // Effects identify the actual pool directory or heap block and its plane. Whole metadata
    // objects are conservative physical envelopes; detached allocations are
    // unpublished construction and are carried by the pending operation.
    template<class Pool,class Emit> [[gnu::always_inline]] static void damage(const reservation<Pool>& plan,const void* owner,Emit&& emit) {
        const auto& pool=*plan.owner;
        auto add=[&](const void* source,unsigned plane,const void* base,std::size_t extent,std::size_t offset,std::size_t size) __attribute__((always_inline)) {
            emit(write_effect({source,plane,base,extent},offset,size));
        };
        auto metadata=[&](std::size_t id) __attribute__((always_inline)) {const auto& b=*pool.blocks_[id].owner;add(&b,0,&b,sizeof(b),0,sizeof(b));};
        // The outer operation covers its enclosing owner header separately;
        // that header contains families_, counts and the vector descriptor.
        if(plan.detached){
            if(!plan.directory.empty()){
                if(!pool.blocks_.empty())add(&pool,1,pool.blocks_.data(),pool.blocks_.size()*sizeof(typename Pool::entry),0,pool.blocks_.size()*sizeof(typename Pool::entry));
            }else add(&pool,1,pool.blocks_.data(),pool.blocks_.capacity()*sizeof(typename Pool::entry),plan.id*sizeof(typename Pool::entry),sizeof(typename Pool::entry));
            if(pool.families_[plan.which].first!=Pool::none)metadata(pool.families_[plan.which].first);
        }else {
            const auto& b=*plan.destination;
            add(&b,0,&b,sizeof(b),0,sizeof(b));
            add(&b,1,b.storage.data,b.storage.size,64+plan.rank*plan.bytes,plan.bytes);
            if(!plan.reused)add(&b,1,b.storage.data,b.storage.size,0,2);
            // A full block is unlinked after assigning this reservation.
            if(b.returned_count<=1&&plan.assigned+unsigned(!plan.reused)==pool.families_[plan.which].ranks){
                if(b.previous!=Pool::none)metadata(b.previous);
                if(b.next!=Pool::none)metadata(b.next);
            }
        }
    }

    template<class Pool,class Retire> [[gnu::always_inline]] static void directory_retirement(const reservation<Pool>& plan,const void* owner,Retire&& retire){
        const auto& pool=*plan.owner;
        if(!plan.directory.empty()&&!pool.blocks_.empty())
            retire(physical_region{&pool,1,pool.blocks_.data(),pool.blocks_.capacity()*sizeof(typename Pool::entry)});
    }
    template<unsigned V,bool Coalesced,class Emit>
    [[gnu::always_inline]] static void append_damage(const capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>& pool,
            word reference,unsigned count,const void* owner,Emit&& emit){
        const auto& b=*pool.blocks_[reference>>8].owner;
        const auto offset=64+(std::size_t(reference&255)*pool.families_[b.which].capacity+count)*V;
        emit(ikea::journal::entry{&b,1,offset,V});
    }

    // Standalone release, or a joined append into a DIFFERENT family. A same-
    // block reservation changes the last-reference decision and must not use
    // this discovery. A last-reference release destroys two physical sources. Their
    // initialized representation is preserved before destruction, and the
    // session must resolve/retain effect identity before calling release().
    // This is not permission to free storage borrowed by an old reader.
    template<unsigned V,bool Coalesced,class Emit,class Retire>
    [[gnu::always_inline]] static void release_damage(const capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>& pool,
            word reference,const void* owner,Emit&& emit,Retire&& retire){
        using Pool=capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>;
        const auto id=std::size_t(reference>>8);const auto& slot=pool.blocks_[id];const auto& b=*slot.owner;
        auto source=[&](const void* stable,unsigned plane,const void* base,std::size_t extent) __attribute__((always_inline)) {return physical_region{stable,plane,base,extent};};
        auto add=[&](const void* stable,unsigned plane,const void* base,std::size_t extent,std::size_t offset,std::size_t size) __attribute__((always_inline)) {if(size)emit(write_effect(source(stable,plane,base,extent),offset,size));};
        auto metadata=[&](std::size_t next) __attribute__((always_inline)) {const auto& target=*pool.blocks_[next].owner;add(&target,0,&target,sizeof(target),0,sizeof(target));};
        add(&b,0,&b,sizeof(b),0,sizeof(b));
        if(b.live==1){
            add(&pool,1,pool.blocks_.data(),pool.blocks_.capacity()*sizeof(typename Pool::entry),id*sizeof(typename Pool::entry),sizeof(typename Pool::entry));
            if(b.listed){if(b.previous!=Pool::none)metadata(b.previous);if(b.next!=Pool::none)metadata(b.next);}
            add(&b,1,b.storage.data,b.storage.size,0,2);
            const auto assigned_bytes=std::size_t(Pool::assigned(b))*pool.families_[b.which].capacity*V;
            add(&b,1,b.storage.data,b.storage.size,64,assigned_bytes);
            retire(source(&b,0,&b,sizeof(b)));retire(source(&b,1,b.storage.data,b.storage.size));
        }else if(!b.listed&&pool.families_[b.which].first!=Pool::none)
            metadata(pool.families_[b.which].first);
    }

    // Requires unchanged exclusive owner, live unchanged prefix, snapshotted
    // incoming value, and successful admission of all index/pool/retirement
    // effects. The session must consume this head once and resolve effects
    // before its old directory is destroyed. No allocation occurs here. The complete
    // reservation is initialized before its assignment count is published.
    template<unsigned V,bool Coalesced
#if WRITE_ARCH_POOL_BORROWED
        ,class Plan
#endif
    >
    static word commit(
#if WRITE_ARCH_POOL_BORROWED
            Plan& plan,
#else
            reservation<capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>>& plan,
#endif
            const ht::bytes<V>& incoming)noexcept {
        using Pool=capacity_buffers<V,buffer_representation::streampack_capacity,Coalesced>;
#if WRITE_ARCH_POOL_BORROWED
        static_assert(std::is_same_v<Plan,reservation<Pool>>||std::is_same_v<Plan,borrowed_reservation<Pool>>,
            "commit value width and coalescing must match the selected pool");
#endif
        auto& pool=*plan.owner;auto& family=pool.families_[plan.which];
        auto& b=*plan.destination;
        const auto prefix=plan.prefix;const auto initialized=prefix.size()+V;
#if WRITE_ARCH_POOL_ASSIGNMENT
        // Preparation established this placement; the exclusive session keeps
        // it valid through admission. Snapshot the real source before stores so
        // the assignment sees exactly the initialized bytes, with no second copy.
        const auto source=pool.view(b);const auto format=source.format();
        const unsigned assigned=plan.assigned,bytes=plan.bytes,offset=plan.rank*bytes;
        auto* payload=reinterpret_cast<byte*>(source.windows().payload.data())+offset;
#endif
        // Extent and destination disjointness were checked during preparation;
        // source liveness/stability is the enclosing session's witness.
        ikea::no_effects effects;
#if WRITE_ARCH_POOL_ASSIGNMENT
        if(prefix.size())std::memmove(payload,prefix.data(),prefix.size());
        tuple_value<V,Coalesced>(payload+prefix.size(),incoming,effects);
#else
        if(prefix.size())std::memmove(plan.payload,prefix.data(),prefix.size());
        tuple_value<V,Coalesced>(plan.payload+prefix.size(),incoming,effects);
#endif
        if(!plan.reused){
#if WRITE_ARCH_POOL_ASSIGNMENT
            if(initialized<bytes)std::memset(payload+initialized,0,bytes-initialized);
            Pool::assign_initialized(source,format,assigned,offset,bytes,effects);
#else
            if(initialized<plan.bytes)std::memset(plan.payload+initialized,0,plan.bytes-initialized);
            const auto source=pool.view(b);const auto format=source.format();
            const ht::bag_write::cursor cursor{&source,plan.assigned,plan.rank*plan.bytes};
            const ht::bag_write::append_access<std::uint16_t,std::uint32_t> access{&cursor,&format};
            stream::author::append_fixed_run_unchecked(access,
                {reinterpret_cast<const stream::byte*>(plan.payload),plan.bytes},1,effects);
#endif
        }else --b.returned_count;
        ++b.live;++pool.live_buffers_;
#if WRITE_ARCH_POOL_BORROWED
        static_assert(WRITE_ARCH_POOL_ASSIGNMENT,"borrowed reservation uses the shared source assignment");
        if constexpr(std::is_same_v<Plan,reservation<Pool>>)
#endif
        if(plan.detached){
            if(!plan.directory.empty()){
                for(std::size_t i=0;i<pool.blocks_.size();++i)plan.directory[i]=std::move(pool.blocks_[i]);
                pool.blocks_.swap(plan.directory);
            }else if(plan.new_slot)pool.blocks_.emplace_back();
            else pool.first_free_=pool.blocks_[plan.id].next_free;
            auto& slot=pool.blocks_[plan.id];slot.next_free=Pool::none;
            slot.owner=std::move(plan.detached);slot.payload=slot.owner->storage.data+64;
            pool.assigned_bytes_+=slot.owner->storage.capacity();
            pool.link(plan.id);
        }
        if(!b.returned_count&&Pool::assigned(b)==family.ranks)pool.unlink(plan.id);
        return (word(plan.id)<<8)|plan.rank;
    }
};
}
