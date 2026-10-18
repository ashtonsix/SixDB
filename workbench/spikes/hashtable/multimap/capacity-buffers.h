#pragma once
#include "table.h"
#include <ikea/streampack/write.h>
#include <ikea/streampack/author/write.h>
#include "legacy/stream-write.h"
#include <optional>
#ifndef WRITE_ARCH_ADMITTED_TUPLE
#if SIXDB_TUNE_ZEN5 || SIXDB_TUNE_NEOVERSE_V2
#define WRITE_ARCH_ADMITTED_TUPLE 1
#else
#define WRITE_ARCH_ADMITTED_TUPLE 0
#endif
#endif
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

namespace write_arch {
namespace stream=ikea::streampack;
namespace tuple=ikea::tuplepack;

// A full eight-byte TuplePack projection. Preparation validates its shape once;
// each private owner call supplies real bounded storage and retains a named view.
// This private leaf deliberately accepts no_coverage only. Deferred journals
// cannot borrow these short-lived views; guarded writes need operation-lived
// owner coordinates and protection published before the first payload store.
struct tuple_word {
    inline static const auto format=[] {
        std::array<tuple::code,8> codes;for(unsigned i=0;i<8;++i)codes[i]={byte(i),0,8};
        auto result=tuple::layout::make(8,codes);ht::require(bool(result),"cell tuple format");return *result;
    }();
    inline static const auto plan=[] {
        std::array<byte,8> map;for(unsigned i=0;i<8;++i)map[i]=byte(i);
        auto result=tuple::writer<8>::make(format,map);ht::require(bool(result),"cell tuple writer");
        const auto& p=result->controls();ht::require(p.word.selected==8&&p.transfer.bytes==8&&p.transfer.offset==0&&p.transfer.shift==0&&p.transfer.mask==~word{0},"cell tuple transfer");
        return *result;
    }();
    [[gnu::always_inline]] static void write(byte* p,word value,ikea::no_coverage& effects) {
        // The actual owner proves this whole eight-byte object is addressable.
        const auto size=format.bytes();__builtin_assume(size==8);
        __builtin_assume(reinterpret_cast<std::uintptr_t>(p)<=~std::uintptr_t{0}-8);
        auto bound=tuple::view::bind(format,{p,8},1,8);
        // The immutable admitted recipe and owner-proven eight-byte extent
        // establish every bind precondition. Restate that proof on the result:
        // Clang 21 misses the equivalent pointer-overflow assumption above.
        const bool admitted=bool(bound);
        if constexpr(WRITE_ARCH_ADMITTED_TUPLE)__builtin_assume(admitted);
        ht::require(admitted,"cell tuple placement");
        const auto& source=*bound;
        effects.before(source,ikea::byte_write{0,0,8});
        const auto& controls=plan.controls();
        const auto selected=controls.word.selected;
        const auto bytes=controls.transfer.bytes,offset=controls.transfer.offset,shift=controls.transfer.shift;
        const auto mask=controls.transfer.mask;
        __builtin_assume(selected==8);__builtin_assume(bytes==8);__builtin_assume(offset==0);__builtin_assume(shift==0);__builtin_assume(mask==~word{0});
        tuple::native::write_body(controls,p,value);
    }
};
// One placed sequence of whole-word TuplePack units. Callers supply their
// already-snapshotted incoming value, disjoint from this live destination.
// Bind once for the entire physical write envelope, not through eight captured
// closures with separate placement checks. This remains private no_coverage;
// a deferred effect consumer needs an operation-lived source binding.
template<unsigned V,bool Coalesced=true> [[gnu::always_inline]] void tuple_value(byte* p,const ht::bytes<V>& __restrict value,ikea::no_coverage& effects) {
    static_assert(V%8==0);
    if constexpr(!Coalesced){
        ht::each<V/8>([&](auto i){tuple_word::write(p+i*8,ht::load<8>(value.data()+i*8),effects);});
        return;
    }
    const auto size=tuple_word::format.bytes();__builtin_assume(size==8);
    __builtin_assume(reinterpret_cast<std::uintptr_t>(p)<=~std::uintptr_t{0}-V);
    auto bound=tuple::view::bind(tuple_word::format,{p,V},V/8,8);
    const bool admitted=bool(bound);
    if constexpr(WRITE_ARCH_ADMITTED_TUPLE)__builtin_assume(admitted);
    ht::require(admitted,"whole value tuple placement");const auto& source=*bound;
    effects.before(source,ikea::byte_write{0,0,V});
#if defined(__aarch64__) || defined(__AVX2__)
    if constexpr(V==64){
        // The admitted identity recipe selects every byte of all eight units.
        // Coalesce that exact physical envelope into one native packet write;
        // no padding, neighbouring tuple or old destination value is touched.
        tuple::native::store_packet(source.row_unchecked(0),tuple::native::load_packet(value.data()));
        return;
    }
#endif
    const auto& controls=tuple_word::plan.controls();
    for(unsigned i=0;i<V;i+=8){
        // Restate the constructor-admitted, immutable recipe at each native
        // store; arbitrary byte stores otherwise defeat loop-wide propagation.
        const auto selected=controls.word.selected;
        const auto bytes=controls.transfer.bytes,offset=controls.transfer.offset,shift=controls.transfer.shift;
        const auto mask=controls.transfer.mask;
        __builtin_assume(selected==8);__builtin_assume(bytes==8);__builtin_assume(offset==0);__builtin_assume(shift==0);__builtin_assume(mask==~word{0});
        tuple::native::write_body(controls,source.row_unchecked(i/8),ht::load<8>(value.data()+i));
    }
}

// StreamPack values here are fully initialized capacity buffers, NOT logical
// bags with a second U. The index cell alone owns the logical element count.
// Reusable buffers remain assigned StreamPack values. A block is deallocated
// when no cell references any of its buffers; it is not kept as retired payload.
// The second representation is a diagnostic PRIVATE prefix-initialized pool.
// It does not construct a StreamPack view and cannot use StreamPack copying,
// persistence or packing contracts. It isolates the cost of initializing slack.
enum class buffer_representation { streampack_capacity, mutable_prefix_diagnostic };
template<unsigned V,buffer_representation Representation=buffer_representation::streampack_capacity,bool Coalesced=true> class capacity_buffers {
    friend struct multimap_rebuild;
    friend struct guarded_pool_access;
    friend struct guarded_pool_test_access;
    static_assert(V==8||V==64);
    static constexpr bool stream_capacity=Representation==buffer_representation::streampack_capacity;
    static constexpr unsigned maximum=65535/V;
    static constexpr unsigned last_class=std::bit_width(maximum),classes=last_class+1;
    static constexpr std::size_t none=~std::size_t{0};
    using buffer=ht::heap_storage_policy::buffer;
    struct family {std::optional<stream::layout> format;std::size_t first=none;unsigned capacity=0,ranks=0;};
    struct block {
        buffer storage;
        std::array<byte,256> returned{};
        unsigned returned_count=0,live=0,which=0;
        std::size_t previous=none,next=none;
        bool listed=false;
        block(unsigned c,unsigned bytes):storage(64+bytes,nullptr,false),which(c){}
    };
    // Recycle descriptor IDs only after the private owner has no referencing
    // cells. Payload addresses are directly available to readers, independently
    // of cold free-list metadata. Published versions need their own protected
    // descriptor/buffer state; an allocation hold alone does not permit reuse.
    struct entry {std::unique_ptr<block> owner;byte* payload=nullptr;std::size_t next_free=none;};
    std::array<family,classes> families_;
    std::vector<entry> blocks_;
    std::size_t first_free_=none;
    std::size_t assigned_bytes_=0,live_buffers_=0;
    static void expose_prefix(byte* payload,std::size_t bytes) {
#if __has_feature(address_sanitizer)
        if constexpr(!stream_capacity)__asan_unpoison_memory_region(payload,bytes);
#else
        (void)payload;(void)bytes;
#endif
    }
    static void hide_slack(byte* payload,std::size_t used,std::size_t reserved) {
#if __has_feature(address_sanitizer)
        if constexpr(!stream_capacity)__asan_poison_memory_region(payload+used,reserved-used);
#else
        (void)payload;(void)used;(void)reserved;
#endif
    }
    stream::view view(block& b)const requires(stream_capacity) {
        auto* bytes=reinterpret_cast<stream::byte*>(b.storage.data);
        const auto capacity=families_[b.which].capacity;
        return stream::view::assume_valid(*families_[b.which].format,
            {{bytes+64,std::size_t(capacity)*V*families_[b.which].ranks},{},{},{bytes,2}});
    }
    static unsigned assigned(const block& b){return unsigned(ht::load<2>(b.storage.data));}
    // Assign one fully initialized reservation in this pool's real fixed view.
    // The cursor and input describe the same bytes. The caller admits effects
    // before entry and keeps this source and its recipe stable through the call.
    [[gnu::always_inline]] static void assign_initialized(const stream::view& source,const stream::layout& format,
            unsigned count,unsigned offset,unsigned reservation,ikea::no_coverage& effects) requires(stream_capacity) {
        const auto fixed=format.fixed(),used=format.has_used(),tombs=format.has_tombstones(),extent=format.has_extent();
        const auto control=format.control_bytes();
        __builtin_assume(fixed);__builtin_assume(!used);__builtin_assume(!tombs);__builtin_assume(!extent);__builtin_assume(control==2);
        const ht::bag_write::cursor cursor{&source,count,offset};
        const ht::bag_write::append_access<std::uint16_t,std::uint32_t> access{&cursor,&format};
        [[clang::always_inline]] stream::author::append_fixed_run_unchecked(access,
            {source.windows().payload.data()+offset,reservation},1,effects);
    }
    void link(std::size_t id) {
        auto& b=*blocks_[id].owner;auto& family=families_[b.which];
        b.previous=none;b.next=family.first;b.listed=true;
        if(b.next!=none)blocks_[b.next].owner->previous=id;family.first=id;
    }
    void unlink(std::size_t id) {
        auto& b=*blocks_[id].owner;if(!b.listed)return;
        if(b.previous==none)families_[b.which].first=b.next;else blocks_[b.previous].owner->next=b.next;
        if(b.next!=none)blocks_[b.next].owner->previous=b.previous;
        b.previous=b.next=none;b.listed=false;
    }
    [[gnu::noinline]] std::size_t new_block(unsigned which) {
        const auto& f=families_[which];
        auto next=std::make_unique<block>(which,f.capacity*V*f.ranks);
        if constexpr(stream_capacity){auto destination=view(*next);ikea::no_coverage effects;
            ht::require(bool(stream::initialize(destination,effects)),"capacity block initialize");}
        else ht::store<2>(next->storage.data,0);
        const auto id=first_free_==none?blocks_.size():first_free_;
        if(first_free_==none)blocks_.push_back({std::move(next)});
        else {auto& slot=blocks_[id];first_free_=slot.next_free;slot.next_free=none;slot.owner=std::move(next);}
        blocks_[id].payload=blocks_[id].owner->storage.data+64;
        assigned_bytes_+=blocks_[id].owner->storage.capacity();link(id);return id;
    }
  public:
    static constexpr unsigned max_values=maximum;
    capacity_buffers() {
        for(unsigned i=0;i<classes;++i){auto& f=families_[i];f.capacity=std::min(1u<<i,maximum);
            // A 256-rank logical limit is not a demand for 256 large buffers.
            // Bound each allocation to about 64 KiB, with whole reservations.
            f.ranks=std::clamp(65536u/(f.capacity*V),1u,256u);
            if constexpr(stream_capacity){
                stream::description d{.family=stream::geometry::direct,.maximum=std::uint16_t(f.capacity*V),
                    .byte_limit=f.capacity*V*f.ranks,.fixed_reservation=std::uint16_t(f.capacity*V),.full=true,.immortal=true};
                f.format=stream::layout::prepare(d).value();
                ht::require(f.format->directory_bytes()==0&&f.format->lengths_bytes()==0&&f.format->control_bytes()==2,"capacity buffer recipe");
            }
        }
    }
    capacity_buffers(const capacity_buffers&)=delete;
    static unsigned class_for(unsigned count){ht::require(count&&count<=maximum,"bag maximum");return std::min(unsigned(std::bit_width(count-1)),last_class);}
    unsigned capacity(unsigned which)const{return families_[which].capacity;}
    // Stage the actual reader directory dependency without touching the
    // separately allocated payload. No mutation may intervene in this borrow.
    template<class Emit> void prepare_data(word reference,Emit&& emit)const {
        emit(reinterpret_cast<const byte*>(&blocks_[reference>>8].payload),sizeof(byte*));
    }
    byte* data(word reference,unsigned which)const {
        return blocks_[reference>>8].payload+unsigned(reference&255)*families_[which].capacity*V;
    }
    // The owning cell supplies this exact reservation size. The source pool's
    // family/rank invariant remains the authority; this avoids rereading its
    // immutable family table after the cell has already supplied the geometry.
    byte* data_capacity(word reference,unsigned capacity)const {
        return blocks_[reference>>8].payload+unsigned(reference&255)*capacity*V;
    }
    void append_fitting_capacity(word reference,unsigned capacity,unsigned count,const ht::bytes<V>& incoming){
        auto* suffix=data_capacity(reference,capacity)+std::size_t(count)*V;
        expose_prefix(suffix,V);ikea::no_coverage effects;tuple_value<V,Coalesced>(suffix,incoming,effects);
    }
    void append_fitting(word reference,unsigned which,unsigned count,const ht::bytes<V>& incoming) {
        auto* suffix=data(reference,which)+std::size_t(count)*V;
        expose_prefix(suffix,V);ikea::no_coverage effects;tuple_value<V,Coalesced>(suffix,incoming,effects);
    }
    // All fallible allocation precedes any old-buffer or index-cell mutation.
    // Prefix may name another live buffer: block payload pointers stay stable
    // when the owner directory grows. The bounded incoming value is a snapshot.
    word create(unsigned which,std::span<const byte> prefix,const ht::bytes<V>& incoming) {
        auto& f=families_[which];const unsigned reservation=f.capacity*V;
        ht::require(prefix.size()+V<=reservation,"capacity buffer payload");
        const auto id=f.first==none?new_block(which):f.first;auto& b=*blocks_[id].owner;
        // Capture the real placement before payload writes; the prefix-only
        // diagnostic deliberately has no StreamPack source at all.
        const auto source=[&]{if constexpr(stream_capacity)return view(b);else return 0;}();
        const auto count=assigned(b);const bool reuse=b.returned_count!=0;
        const auto rank=reuse?unsigned(b.returned[--b.returned_count]):count;
        // rank<256 and reservation<=65535: this physical offset fits unsigned.
        const unsigned offset=rank*reservation;
        auto* payload=b.storage.data+64+offset;
        expose_prefix(payload,prefix.size()+V);
        if(prefix.size())std::memmove(payload,prefix.data(),prefix.size());
        ikea::no_coverage effects;tuple_value<V,Coalesced>(payload+prefix.size(),incoming,effects);
        if(!reuse){if constexpr(stream_capacity){
            const auto initialized=prefix.size()+V;
            if(initialized<reservation)std::memset(payload+initialized,0,reservation-initialized);
            // Assign the real, already initialized capacity buffer. The authored
            // self-copy is legal and can disappear; source identity stays real.
            // Snapshot the admitted recipe and cursor across payload writes.
            // The adapter still identifies this original, real source view.
            const auto format=source.format();
            assign_initialized(source,format,count,offset,reservation,effects);
          }else{
            // This pool's assigned count describes reservations; the owning
            // cell's count alone describes their initialized/exposed prefix.
            ht::store<2>(b.storage.data,count+1);
          }
        }
        ++b.live;++live_buffers_;
        hide_slack(payload,prefix.size()+V,reservation);
        if(!b.returned_count&&assigned(b)==f.ranks)unlink(id);
        return (word(id)<<8)|rank;
    }
    // Bulk unpublished join: initialize both live spans before assigning the
    // complete StreamPack reservation. Unlike create()+tail overwrite, only
    // actual slack is zeroed. Inputs are disjoint from this fresh reservation.
    word create_join(unsigned which,std::span<const byte> left,std::span<const byte> right) requires(stream_capacity) {
        auto& f=families_[which];const unsigned reservation=f.capacity*V;
        ht::require(left.size()<=reservation&&right.size()<=reservation-left.size(),"joined capacity payload");
        const auto initialized=left.size()+right.size();
        const auto id=f.first==none?new_block(which):f.first;auto& b=*blocks_[id].owner;
        const auto source=view(b);const auto count=assigned(b);const bool reuse=b.returned_count!=0;
        const auto rank=reuse?unsigned(b.returned[--b.returned_count]):count;
        const unsigned offset=rank*reservation;auto* payload=b.storage.data+64+offset;
        std::memcpy(payload,left.data(),left.size());
        std::memcpy(payload+left.size(),right.data(),right.size());
        if(!reuse){
            if(initialized<reservation)std::memset(payload+initialized,0,reservation-initialized);
            ikea::no_coverage effects;
            const auto format=source.format();
            assign_initialized(source,format,count,offset,reservation,effects);
        }
        ++b.live;++live_buffers_;
        if(!b.returned_count&&assigned(b)==f.ranks)unlink(id);
        return (word(id)<<8)|rank;
    }
    void release(word reference)noexcept {
        const auto id=std::size_t(reference>>8);auto& slot=blocks_[id];auto& b=*slot.owner;
        --b.live;--live_buffers_;
        if(!b.live){unlink(id);assigned_bytes_-=b.storage.capacity();slot.owner.reset();slot.payload=nullptr;
            slot.next_free=first_free_;first_free_=id;return;}
        b.returned[b.returned_count++]=byte(reference&255);
        if(!b.listed)link(id);
    }
    std::size_t bytes_used()const {
        std::size_t result=sizeof(*this)+assigned_bytes_+blocks_.capacity()*sizeof(entry);
        for(const auto& b:blocks_)if(b.owner)result+=sizeof(block);
        return result;
    }
    std::size_t live_buffers()const{return live_buffers_;}
    struct reference_witness {word reference;unsigned which,count;};
    void validate(std::span<const reference_witness> owners)const {
        std::vector<std::array<bool,256>> referenced(blocks_.size());
        for(const auto& w:owners){const auto id=w.reference>>8;const auto rank=unsigned(w.reference&255);
            ht::require(id<blocks_.size()&&bool(blocks_[id].owner),"live capacity reference");
            const auto& b=*blocks_[id].owner;
            ht::require(w.which==b.which&&w.count&&w.count<=families_[b.which].capacity,"live capacity class and count");
            ht::require(rank<assigned(b)&&!referenced[id][rank],"unique assigned buffer owner");
            referenced[id][rank]=true;
        }
        std::vector<bool> available(blocks_.size()),free(blocks_.size());
        for(unsigned which=0;which<classes;++which){std::size_t previous=none;
            for(auto id=families_[which].first;id!=none;id=blocks_[id].owner->next){
                ht::require(id<blocks_.size()&&bool(blocks_[id].owner)&&!available[id],"acyclic available blocks");
                const auto& b=*blocks_[id].owner;
                ht::require(b.which==which&&b.previous==previous&&b.listed,"available block links");
                available[id]=true;previous=id;
            }
        }
        for(auto id=first_free_;id!=none;id=blocks_[id].next_free){
            ht::require(id<blocks_.size()&&!blocks_[id].owner&&!free[id],"acyclic free descriptors");free[id]=true;
        }
        std::size_t live=0;
        for(std::size_t id=0;id<blocks_.size();++id){const auto& slot=blocks_[id];
          if(slot.owner){auto& b=*slot.owner;const auto count=assigned(b);
            if constexpr(stream_capacity){auto source=view(b);ht::require(bool(source.validate()),"capacity buffer full validation");}
            ht::require(count<=families_[b.which].ranks&&b.live+b.returned_count==count,"capacity buffer assignment accounting");live+=b.live;
            ht::require(b.listed==(b.returned_count||count<families_[b.which].ranks),"capacity block availability");
            ht::require(available[id]==b.listed&&!free[id]&&slot.payload==b.storage.data+64,"descriptor placement");
            std::array<bool,256> returned{};
            for(unsigned i=0;i<b.returned_count;++i){const auto rank=b.returned[i];
                ht::require(rank<count&&!returned[rank]&&!referenced[id][rank],"unique returned buffer");returned[rank]=true;
            }
            for(unsigned rank=0;rank<count;++rank)
                ht::require(referenced[id][rank]!=returned[rank],"assigned buffer has exactly one owner or free slot");
          }else ht::require(free[id]&&!available[id]&&!slot.payload,"released descriptor reusable");
        }
        ht::require(live==live_buffers_&&live==owners.size(),"capacity buffer live accounting");
    }
};
}
