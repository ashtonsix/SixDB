#pragma once
#include "fields.h"
#include <ikea/streampack.h>
#include <optional>
#include <span>
#include <type_traits>
#ifndef HT_LINK_ZERO_SOURCE
#define HT_LINK_ZERO_SOURCE 0
#endif
#ifndef HT_LINK_RESERVE_BLOCK
#define HT_LINK_RESERVE_BLOCK 0
#endif
namespace ht {
namespace st=ikea::streampack;
#if HT_LINK_ZERO_SOURCE
// Matched caller control: supply the same fully initialized slot bytes to the
// same StreamPack operation, without constructing a largest-family stack copy.
// Shared across link encodings; it is program data, not per-owner allocation.
inline constexpr std::array<std::byte,65535> zero_link_input{};
#endif
enum class link_event_kind {
    before_block_write, before_free_list_write, before_active_write,
    directory_relocated, directory_inserted, allocation_grown
};
// A synchronous owner-qualified observation. A block ordinal survives directory
// and payload relocation; none of the pointers may escape this callback. The
// three structural outcomes describe successful owner changes, not before-images.
struct link_event {
    link_event_kind kind;
    const void* owner;
    std::uint32_t block;
    unsigned family;
    const oa::Allocation* allocation;
    const void* address;
    std::size_t bytes;
};
struct no_link_observer { void operator()(const link_event&) const noexcept {} };
// Scratch owner of fixed/full StreamPack slots. Links per block are a byte-limit
// policy, not a different StreamPack rank encoding. Growth and retained-block
// damage costs must be charged when selecting that experimental policy.
// Empty logical links remain assigned slots and are reused through per-size
// free lists. The eventual MVCC/compaction owner may substitute that policy.
// Value-only records use Bag=true and have no key field. The allocation,
// reservation, clone and damage machinery remains the same owner.
template<unsigned K,bool Direct=false,unsigned ValueBytes=8,bool AdmittedAppend=false,bool Bag=false,
         unsigned SlotsPerBlock=16,bool VectorPrefix=false> class links {
    static_assert(!VectorPrefix || Bag);
    static_assert(Bag ? K==0 && ValueBytes>0 : K>=8 && K<=64 && K%8==0);
    static constexpr bool bag_operations=Bag || (K==64 && ValueBytes==0);
    // Key-overflow rows may contain a 64-byte value and a discriminator.
    // User bag elements retain the separate 1..64-byte value domain.
    static_assert(ValueBytes<=(Bag?64u:65u));
    static constexpr unsigned entry_bytes=K/8+ValueBytes;
    static constexpr unsigned families=std::min(10u,unsigned(std::bit_width((65535u-6)/(4*entry_bytes))));
    static_assert(SlotsPerBlock>=1 && SlotsPerBlock<=256 && std::has_single_bit(SlotsPerBlock));
    static constexpr unsigned per_block=SlotsPerBlock;
    static unsigned capacity(unsigned f) { return 4u<<f; }
    static unsigned width(unsigned f) { return 6+capacity(f)*entry_bytes; }
    struct block { oa::Allocation allocation; unsigned family; };
    oa::Pool* pool_;
    // Shared layouts are immutable after construction. Retain their admitted
    // address once: repeated static-initialization guards in read paths are
    // opaque calls that also prevent a surrounding table loop hoisting seeds
    // and allocation bases. This pointer owns neither formats nor payloads.
    const st::layout* layouts_;
    std::vector<block> blocks_;
    std::array<std::uint32_t,families> active_{}; // one-based block id
    std::array<std::uint32_t,families> free_{};
    bool reserved_ = false;
    template<class Observer> void observe_block(std::uint32_t id,Observer& observer) const noexcept {
        static_assert(std::is_nothrow_invocable_v<Observer&,const link_event&>);
        const auto& b=blocks_[id-1];
        observer(link_event{link_event_kind::before_block_write,this,id,b.family,
                            &b.allocation,b.allocation.address,b.allocation.size});
    }
    template<class Observer> void observe_free(unsigned f,Observer& observer) const noexcept {
        observer(link_event{link_event_kind::before_free_list_write,this,0,f,nullptr,
                            &free_[f],sizeof(free_[f])});
    }
    static const auto& formats() {
        static const auto p=[] {
            std::vector<st::layout> out; out.reserve(families);
            for(unsigned f=0;f<families;++f) {
                auto w=width(f);
                auto l=st::layout::prepare({.maximum=std::uint16_t(w),.byte_limit=per_block*w,
                    .fixed_reservation=std::uint16_t(w),.full=true,.immortal=true});
                require(bool(l),"link layout");
                if constexpr(AdmittedAppend)require(l->fixed() && l->fixed_width()==w &&
                    !l->has_used() && !l->has_tombstones() && !l->has_extent() &&
                    l->directory_bytes()==0 && l->lengths_bytes()==0 && l->control_bytes()==2 &&
                    l->capacity()>=per_block,"fixed bag author admission");
                out.push_back(*l);
            }
            return out;
        }(); return p;
    }
    st::view bind(block& b) const {
        auto& a=b.allocation;
        return st::view::assume_valid(layouts_[b.family],
            {{a.address+2,a.size-2},{},{},{a.address,2}});
    }
    template<class Observer> std::optional<std::uint32_t> acquire(unsigned family,Observer& observer) {
        if(free_[family]) {
            auto ref=free_[family]; auto* p=data(ref);
            observe_free(family,observer);observe_block(ref>>8,observer);
            free_[family]=std::uint32_t(get<4,Direct>(p));
            put<4,Direct>(p,0); return ref;
        }
        auto id=active_[family];
        if(!id || bind(blocks_[id-1]).size()==per_block) {
            if(blocks_.size()>=0x00ffffffu) return {};
            // Metadata allocation precedes payload acquisition. A metadata OOM
            // is represented as refusal; the map remains unchanged.
            try {
                if(blocks_.size()==blocks_.capacity()) {
                    blocks_.reserve(std::max(std::size_t(4),blocks_.capacity()*2));
                    observer(link_event{link_event_kind::directory_relocated,this,0,family,nullptr,
                                        blocks_.data(),blocks_.size()*sizeof(block)});
                }
            }
            catch(const std::bad_alloc&) { return {}; }
            // Experimental reservation control: ask Orbital for address-space
            // headroom for this whole block while exposing only its first slot.
            // This can avoid relocation on growth, but may change finite/Window
            // selection and page slack. It is not a second allocation layer.
            auto a=oa::allocate(*pool_,{.bytes=2+width(family),
                .reserved_bytes=HT_LINK_RESERVE_BLOCK?2+per_block*width(family):0,.alignment=8});
            if(!a) return {};
            blocks_.push_back({std::move(*a),family});
            id=blocks_.size();
            observer(link_event{link_event_kind::directory_inserted,this,id,family,
                                &blocks_.back().allocation,&blocks_.back(),sizeof(block)});
            observer(link_event{link_event_kind::before_active_write,this,0,family,nullptr,
                                &active_[family],sizeof(active_[family])});
            active_[family]=id;
            observe_block(id,observer);
            auto v=bind(blocks_.back()); ikea::no_coverage effects;
            require(bool(st::initialize(v,effects)),"link initialize");
        }
        auto& b=blocks_[id-1]; auto n=bind(b).size();
        if(b.allocation.size<2+(n+1)*width(family)) {
            unsigned slots=std::min(per_block,std::bit_ceil(n+1));
            if(!oa::grow(b.allocation,{.bytes=2+slots*width(family),.alignment=8})) return {};
            observer(link_event{link_event_kind::allocation_grown,this,id,family,&b.allocation,
                                b.allocation.address,b.allocation.size});
        }
        // Bind after Orbital growth: no pointer into the old address survives.
        auto v=bind(b); ikea::no_coverage effects;
#if HT_LINK_ZERO_SOURCE
        const auto& blank=zero_link_input;
#else
        std::array<std::byte,6+(4u<<(families-1))*entry_bytes> blank;
        std::memset(blank.data(),0,width(family));
#endif
        observe_block(id,observer);
        if constexpr(AdmittedAppend) {
            // The private owner admits a fixed/full/immortal slot. n<per_block,
            // and growth above covers (n+1)*width. Rebind follows relocation;
            // the initialized zero input is disjoint from the payload. This
            // nonsuspending reservation excludes other mutations. The whole
            // allocation observation precedes these infallible exact writes.
            // W/O lane types are unused: this schema has no R/U/directory lanes.
            st::author::access<st::geometry::width,std::uint16_t,std::uint32_t,std::byte> a(v);
            st::author::append_unchecked(a,{blank.data(),width(family)},width(family),effects);
        }else require(bool(st::append(v,{blank.data(),width(family)},width(family),effects)),"link append");
        return (id<<8)|n;
    }
  public:
    // Covers reserve and either commit or cancellation, including a new block,
    // directory relocation, StreamPack control, owner arrays and a prior edge.
    static constexpr unsigned bag_value_bytes=entry_bytes;
    static constexpr unsigned reservation_event_bound=VectorPrefix?12:10;
    template<class Observer> class reservation {
        friend class links;
        links* owner_;
        Observer* observer_;
        std::uint32_t head_,previous_,ref_;
        // A vector resize retires one old slot only after the new prefix is live.
        struct no_replacement {};
        [[no_unique_address]] std::conditional_t<VectorPrefix,std::uint32_t,no_replacement> replaced_{};
        unsigned first_,needed_;
        bool fresh_;
        reservation(links& owner,Observer& observer,std::uint32_t head,std::uint32_t previous,
                    std::uint32_t ref,unsigned first,unsigned needed,bool fresh,std::uint32_t replaced=0) noexcept
            :owner_(&owner),observer_(&observer),head_(head?head:ref),previous_(previous),ref_(ref),
             first_(first),needed_(needed),fresh_(fresh) {if constexpr(VectorPrefix)replaced_=replaced;}
        void finish() noexcept {owner_->reserved_=false;owner_=nullptr;}
        template<class Write> void publish(Write&& write) noexcept {
            require(owner_!=nullptr,"live link reservation");
            owner_->observe_block(ref_>>8,*observer_);
            // reserve may relocate either the directory or this payload. Resolve
            // only now, after every fallible operation has completed.
            auto* p=owner_->data(ref_);
            if constexpr(VectorPrefix)if(replaced_)
                std::memcpy(p+6,owner_->data(replaced_)+6,std::size_t(first_)*entry_bytes);
            write(p+6+first_*entry_bytes);
            put<2,Direct>(p+4,first_+needed_);
            if(fresh_ && previous_) {
                owner_->observe_block(previous_>>8,*observer_);
                put<4,Direct>(owner_->data(previous_),ref_);
            }
            if constexpr(VectorPrefix)if(replaced_) {
                const auto family=owner_->family(replaced_);
                owner_->observe_block(replaced_>>8,*observer_);owner_->observe_free(family,*observer_);
                auto* old=owner_->data(replaced_);
                put<2,Direct>(old+4,0);put<4,Direct>(old,owner_->free_[family]);
                owner_->free_[family]=replaced_;
            }
            finish();
        }
        void commit_entry(word key,word value) noexcept {
            require(needed_==1,"single link entry reservation");
            publish([&](byte* p) noexcept {put<K/8,Direct>(p,key);put<ValueBytes,Direct>(p+K/8,value);});
        }
      public:
        reservation(const reservation&)=delete;
        reservation& operator=(const reservation&)=delete;
        reservation(reservation&& other) noexcept
            :owner_(std::exchange(other.owner_,nullptr)),observer_(other.observer_),head_(other.head_),
             previous_(other.previous_),ref_(other.ref_),replaced_(other.replaced_),first_(other.first_),needed_(other.needed_),
             fresh_(other.fresh_) {}
        reservation& operator=(reservation&& other) noexcept {
            if(this!=&other) {
                cancel();owner_=std::exchange(other.owner_,nullptr);observer_=other.observer_;
                head_=other.head_;previous_=other.previous_;ref_=other.ref_;first_=other.first_;
                needed_=other.needed_;fresh_=other.fresh_;replaced_=other.replaced_;
            }
            return *this;
        }
        ~reservation() {cancel();}
        std::uint32_t head_after() const noexcept {return head_;}
        std::uint32_t insertion_after() const noexcept {return ref_;}
        void cancel() noexcept {
            if(!owner_)return;
            if(fresh_) {
                const auto f=owner_->family(ref_);
                owner_->observe_block(ref_>>8,*observer_);owner_->observe_free(f,*observer_);
                // Reserved links have zero live entries and were never attached
                // to a logical chain. Reuse their assigned StreamPack rank.
                put<4,Direct>(owner_->data(ref_),owner_->free_[f]);owner_->free_[f]=ref_;
            }
            finish();
        }
        void commit(std::span<const word> values) noexcept requires(bag_operations && entry_bytes==8) {
            require(values.size()==needed_,"bag reservation value count");
            publish([&](byte* p) noexcept {
                for(unsigned i=0;i<needed_;++i)put<8,Direct>(p+i*entry_bytes,values[i]);
            });
        }
        void commit_bytes(std::span<const byte> values) noexcept requires(bag_operations) {
            require(values.size()==std::size_t(needed_)*entry_bytes,"bag reservation byte count");
            publish([&](byte* p) noexcept {
                for(unsigned n=0;n<needed_;++n) {
                    const auto* in=values.data()+std::size_t(n)*entry_bytes;auto* out=p+std::size_t(n)*entry_bytes;
                    lanes<entry_bytes/8>([&](auto i){put<8,Direct>(out+8*i,get<8,Direct>(in+8*i));});
                    if constexpr(entry_bytes%8)put<entry_bytes%8,Direct>(out+(entry_bytes/8)*8,get<entry_bytes%8,Direct>(in+(entry_bytes/8)*8));
                }
            });
        }
        void commit_ranges(std::span<const byte> first,std::span<const byte> last)noexcept requires(bag_operations) {
            require(first.size()%entry_bytes==0 && last.size()%entry_bytes==0 &&
                first.size()+last.size()==std::size_t(needed_)*entry_bytes,"group reservation byte count");
            publish([&](byte* p)noexcept {copy_plain_bytes(p,first);copy_plain_bytes(p+first.size(),last);});
        }
        // Two independently stable, disjoint inputs; one reservation and one
        // publication. First spill need not assemble a concatenated proposal.
        // The caller admits aggregate-input tail widths during preparation.
        template<std::size_t Left,std::size_t Right>
        void commit_bytes(std::span<const byte,Left> first,std::span<const byte,Right> last)noexcept requires(bag_operations) {
            static_assert(Left && Right && Left%entry_bytes==0 && Right%entry_bytes==0);
            require(needed_==(Left+Right)/entry_bytes,"split bag reservation byte count");
            publish([&](byte* p)noexcept {
                auto copy=[]<std::size_t Bytes>(byte* out,std::span<const byte,Bytes> in)noexcept {
                    lanes<Bytes/8>([&](auto i){put<8,Direct>(out+8*i,get<8,Direct>(in.data()+8*i));});
                    if constexpr(Bytes%8)put<Bytes%8,Direct>(out+(Bytes/8)*8,get<Bytes%8,Direct>(in.data()+(Bytes/8)*8));
                };
                copy(p,first);copy(p+Left,last);
            });
        }
        void commit_record(word key,std::span<const byte,ValueBytes> bytes) noexcept requires(!Bag) {
            require(needed_==1,"one row reservation");
            publish([&](byte* p) noexcept {
                put<K/8,Direct>(p,key);p+=K/8;
                lanes<ValueBytes/8>([&](auto i){word value;std::memcpy(&value,bytes.data()+8*i,8);put<8,Direct>(p+8*i,value);});
                if constexpr(ValueBytes%8) {word value=0;std::memcpy(&value,bytes.data()+(ValueBytes/8)*8,ValueBytes%8);
                    put<ValueBytes%8,Direct>(p+(ValueBytes/8)*8,value);}
            });
        }
    };
  private:
    template<class Observer> std::optional<reservation<Observer>> reserve_entries(
            std::uint32_t head,unsigned needed,Observer& observer,std::uint32_t insertion_hint=0) {
        static_assert(std::is_nothrow_invocable_v<Observer&,const link_event&>);
        require(!reserved_ && needed>=1 && needed<=capacity(families-1),"exclusive bounded link reservation");
        std::uint32_t previous=0,ref=insertion_hint?insertion_hint:head;
        unsigned first=0;
        while(ref) {
            const auto* p=data(ref);first=get<2,Direct>(p+4);
            if(capacity(family(ref))-first>=needed)break;
            previous=ref;ref=get<4,Direct>(p);
        }
        const bool fresh=!ref;
        if(fresh) {
            unsigned f=previous?std::min(families-1,family(previous)+1):0;
            // One first spill may contain many narrow values. Admit one link
            // large enough for the complete group, so commit stays infallible.
            while(capacity(f)<needed)++f;
            auto acquired=acquire(f,observer);if(!acquired)return {};
            ref=*acquired;first=0; // Every acquired/free-list link has zero live entries.
        }
        reserved_=true;
        return reservation<Observer>(*this,observer,head,previous,ref,first,needed,fresh);
    }
  public:
    // One exclusive, nonsuspending mutation reservation per owner. The named
    // observer and owner outlive it and its moved-to instance. No other mutation
    // may interleave. Reserve can change capacity/structural ownership but never
    // the values reachable from head; refusal may retain that extra capacity.
    // Observers must not reenter or mutate this owner. Commit inputs must remain
    // valid after reserve; copying old values before reserve avoids stale borrows.
    // Preadmit reservation_event_bound observer slots. Cancellation can emit too.
    template<class Observer> [[nodiscard]] std::optional<reservation<Observer>> reserve_values(
            std::uint32_t head,unsigned needed,Observer& observer,std::uint32_t insertion_hint=0) requires(bag_operations) {
        // An optional owner-held ref into this chain. The caller invalidates it
        // when deleting its link or relocating refs. Starting here may leave
        // earlier holes for later compaction; it never changes reachability.
        if constexpr(VectorPrefix)if(head && (!insertion_hint || insertion_hint==head)) {
            require(!reserved_ && needed>=1 && needed<=capacity(families-1),"exclusive vector reservation");
            const auto* old=data(head);const unsigned first=get<2,Direct>(old+4);
            // Once a bag exceeds the largest slot, preserve the ordinary chain
            // continuation. The experiment removes intermediate small links.
            if(get<4,Direct>(old)==0 && first+needed<=capacity(families-1)) {
                if(first+needed<=capacity(family(head))) {
                    reserved_=true;
                    return reservation<Observer>(*this,observer,head,0,head,first,needed,false);
                }
                unsigned f=family(head)+1;while(capacity(f)<first+needed)++f;
                auto acquired=acquire(f,observer);if(!acquired)return {};
                reserved_=true;
                return reservation<Observer>(*this,observer,*acquired,0,*acquired,first,needed,true,head);
            }
        }
        return reserve_entries(head,needed,observer,insertion_hint);
    }
    template<class Observer> [[nodiscard]] std::optional<reservation<Observer>> reserve_record(
            std::uint32_t head,Observer& observer) requires(!Bag) {
        return reserve_entries(head,1,observer);
    }
    std::size_t release_event_bound(std::uint32_t head) const noexcept
            requires(bag_operations) {
        std::size_t events=0;
        for(auto ref=head;ref;ref=next(ref))events+=2;
        return events;
    }
    std::pair<word,std::size_t> release_info(std::uint32_t head)const requires(bag_operations) {
        word values=0;std::size_t events=0;
        for(auto ref=head;ref;ref=next(ref)){values+=count(ref);events+=2;}return {values,events};
    }
    // Caller removes its head reference in the same infallible commit. Every
    // count/edge write and free-list update is observed before it occurs. As with
    // reserve, this is an exclusive mutation and observer capacity is preadmitted.
    template<class Observer> word release_values(std::uint32_t head,Observer& observer) noexcept
            requires(bag_operations) {
        static_assert(std::is_nothrow_invocable_v<Observer&,const link_event&>);
        require(!reserved_,"release outside link reservation");
        word removed=0;
        for(auto ref=head;ref;) {
            const auto after=next(ref),f=family(ref);removed+=count(ref);
            observe_block(ref>>8,observer);observe_free(f,observer);
            auto* p=data(ref);put<2,Direct>(p+4,0);put<4,Direct>(p,free_[f]);free_[f]=ref;
            ref=after;
        }
        return removed;
    }
    struct position { std::uint32_t ref=0; unsigned slot=0; explicit operator bool()const{return ref!=0;} };
    static void prepare() {
        (void)formats();
        if constexpr(!Direct) {
            admit_identity<2>();admit_identity<4>();admit_identity<K/8>();
            if constexpr(ValueBytes>=8)admit_identity<8>();
            if constexpr(ValueBytes%8)admit_identity<ValueBytes%8>();
        }
    }
    explicit links(oa::Pool& pool):pool_(&pool),layouts_(formats().data()) { prepare(); }
    // An independent private version with the same representation-local refs.
    // The source remains immutable throughout the copy. Retain reservation sizes
    // and all assigned slots, including free-list links; no absolute payload or
    // directory pointer crosses owners. This is whole-block copying, not COW.
    std::optional<links> clone(oa::Pool& pool)const {
        require(!reserved_,"clone outside link reservation");
        links result(pool);
        try {result.blocks_.reserve(blocks_.size());}
        catch(const std::bad_alloc&) {return {};}
        for(const auto& b:blocks_) {
            auto allocation=oa::allocate(pool,{.bytes=b.allocation.size,.alignment=8});
            if(!allocation)return {};
            const auto used=2+bind(const_cast<block&>(b)).size()*width(b.family);
            std::memcpy(allocation->address,b.allocation.address,used);
            result.blocks_.push_back({std::move(*allocation),b.family});
        }
        result.active_=active_;result.free_=free_;
        return result;
    }
    byte* data(std::uint32_t ref)const {
        auto& b=const_cast<block&>(blocks_[(ref>>8)-1]);
        auto v=bind(b);
        st::author::access<st::geometry::width,std::uint16_t,std::uint32_t,std::byte> a(v);
        return reinterpret_cast<byte*>(const_cast<std::byte*>(a.value(ref&255).data()));
    }
    unsigned family(std::uint32_t ref)const { return blocks_[(ref>>8)-1].family; }
    std::uint32_t next(std::uint32_t ref)const { return get<4,Direct>(data(ref)); }
    unsigned count(std::uint32_t ref)const { return get<2,Direct>(data(ref)+4); }
    byte* entry(position p)const { return data(p.ref)+6+p.slot*entry_bytes; }
    std::span<byte,ValueBytes> payload(position p)const {return std::span<byte,ValueBytes>(entry(p)+K/8,ValueBytes);}
    template<class Observer> void observe_payload(position p,Observer& observer)const noexcept {
        observe_block(p.ref>>8,observer);
    }
    word key(position p)const requires(!Bag) { return get<K/8,Direct>(entry(p)); }
    word value(position p)const requires(!Bag && ValueBytes<=8) {
        if constexpr(ValueBytes==0)return 0;
        else return get<ValueBytes,Direct>(entry(p)+K/8);
    }
    void value(position p,word v) requires(!Bag && ValueBytes<=8) {
        if constexpr(ValueBytes>0)put<ValueBytes,Direct>(entry(p)+K/8,v);
    }
    template<class Transform> [[gnu::always_inline]] void update(position at,Transform&& transform) requires(!Bag && ValueBytes<=8) {
        if constexpr(ValueBytes==0) {
            [[clang::always_inline]] (void)transform(word(0));
        } else {
            auto* p=entry(at)+K/8;
            word result;[[clang::always_inline]] result=transform(get<ValueBytes,Direct>(p));
            put<ValueBytes,Direct>(p,result);
        }
    }
    position find(std::uint32_t head,word key)const requires(!Bag) {
        for(auto ref=head;ref;) {
            const auto* p=data(ref);const auto n=get<2,Direct>(p+4);
            for(unsigned i=0;i<n;++i)
                if(get<K/8,Direct>(p+6+i*entry_bytes)==key)return {ref,i};
            ref=get<4,Direct>(p);
        }
        return {};
    }
    std::span<const byte> directory_window(std::uint32_t ref)const {
        return {reinterpret_cast<const byte*>(&blocks_[(ref>>8)-1]),sizeof(block)};
    }
    std::span<const byte> payload_window(std::uint32_t ref)const {
        return {data(ref),width(family(ref))};
    }
    // Resolve directory state before issuing the payload demand. The count is
    // stored in the payload header, so it is read only at the ready stage.
    auto resolve_values(std::uint32_t ref)const requires(bag_operations) {
        return std::pair{payload_window(ref),std::size_t(0)};
    }
    bag_value_window value_window(std::uint32_t ref)const requires(bag_operations) {
        const auto* p=data(ref);
        return {{p+6,std::size_t(get<2,Direct>(p+4))*entry_bytes},std::uint32_t(get<4,Direct>(p))};
    }
    template<class Visit> [[gnu::always_inline]] static std::uint32_t consume_values(
            const byte* p,std::size_t,Visit&& visit) requires(bag_operations) {
        const auto n=get<2,Direct>(p+4),after=get<4,Direct>(p);
        for(unsigned i=0;i<n;++i)visit(std::span<const byte,entry_bytes>(p+6+std::size_t(i)*entry_bytes,entry_bytes));
        return after;
    }
    // One qualified StreamPack link, not an unbounded linked-list walk.
    [[gnu::always_inline]] static bool read_payload(const byte* p,word key,word& value) requires(!Bag && ValueBytes<=8) {
        for(unsigned i=0,n=get<2,Direct>(p+4);i<n;++i) {
            auto* e=p+6+i*entry_bytes;
            if(get<K/8,Direct>(e)==key) {value=get<ValueBytes,Direct>(e+K/8);return true;}
        }
        return false;
    }
    template<class Visit> void allocations(Visit&& visit)const {
        for(const auto& b:blocks_) visit(b.allocation);
    }
    // Independent owner inventory for the spike's untimed window checks.
    std::span<const byte> directory_storage()const {
        return {reinterpret_cast<const byte*>(blocks_.data()),blocks_.size()*sizeof(block)};
    }
    // Key is proven absent. Failure precedes visible links, counts and payload.
    bool append(std::uint32_t& head,word key,word value) requires(!Bag && ValueBytes<=8) {
        no_link_observer observer;
        auto pending=reserve_entries(head,1,observer);if(!pending)return false;
        pending->commit_entry(key,value);head=pending->head_after();return true;
    }
    static constexpr unsigned erase_event_bound=3;
    template<class Observer> void erase(std::uint32_t& head,position p,Observer& observer) noexcept {
        require(!reserved_,"erase outside link reservation");
        observe_block(p.ref>>8,observer);
        auto n=count(p.ref); auto* dst=entry(p); auto* src=entry({p.ref,n-1});
        if(p.slot!=n-1) std::memcpy(dst,src,entry_bytes);
        put<2,Direct>(data(p.ref)+4,n-1);
        if(n!=1) return;
        auto after=next(p.ref);
        if(head==p.ref) head=after;
        else {
            auto prev=head; while(next(prev)!=p.ref) prev=next(prev);
            observe_block(prev>>8,observer);
            put<4,Direct>(data(prev),after);
        }
        auto f=family(p.ref); observe_free(f,observer);put<4,Direct>(data(p.ref),free_[f]); free_[f]=p.ref;
    }
    void erase(std::uint32_t& head,position p) {no_link_observer observer;erase(head,p,observer);}
    // A nonsuspending read scope: the consumer must not mutate/relocate this
    // owner. Bind the admitted StreamPack window once per link, so consumer
    // aliasing cannot force repeated directory/layout resolution per value.
    template<class Visit> void each(std::uint32_t head,Visit&& visit)const requires(!Bag && ValueBytes<=8) {
        for(auto ref=head;ref;) {
            const auto* p=data(ref);const auto n=get<2,Direct>(p+4),after=get<4,Direct>(p);
            for(unsigned i=0;i<n;++i) {
                const auto* row=p+6+i*entry_bytes;
                visit(get<K/8,Direct>(row),get<ValueBytes,Direct>(row+K/8));
            }
            ref=after;
        }
    }
    template<class Visit> [[gnu::always_inline]] void each_bytes(std::uint32_t head,Visit&& visit)const requires(bag_operations) {
        for(auto ref=head;ref;) {
            const auto* p=data(ref);const auto n=get<2,Direct>(p+4),after=get<4,Direct>(p);
            for(unsigned i=0;i<n;++i)visit(std::span<const byte,entry_bytes>(p+6+std::size_t(i)*entry_bytes,entry_bytes));
            ref=after;
        }
    }
    template<class Visit> void each_record(std::uint32_t head,Visit&& visit)const requires(!Bag) {
        for(auto ref=head;ref;) {
            const auto* p=data(ref);const auto n=get<2,Direct>(p+4),after=get<4,Direct>(p);
            for(unsigned i=0;i<n;++i) {
                const auto* row=p+6+i*entry_bytes;
                visit(get<K/8,Direct>(row),std::span<const byte,ValueBytes>(row+K/8,ValueBytes));
            }
            ref=after;
        }
    }
    memory footprint()const {
        memory m{0,0,sizeof(*this)+blocks_.capacity()*sizeof(block)};
        for(auto& b:blocks_) {m.requested+=b.allocation.size;m.backing+=oa::capacity(b.allocation);}
        return m;
    }
    void validate()const {
        for(auto& b:blocks_) require(bool(bind(const_cast<block&>(b)).validate()),"StreamPack contents");
    }
    static word preparation_bytes() {return formats().capacity()*sizeof(st::layout);}
};
// Opaque value entries; keys are absent rather than reinterpreted as values.
template<unsigned Bytes,bool AdmittedAppend=false,unsigned SlotsPerBlock=16>
using linked_bags=links<0,false,Bytes,AdmittedAppend,true,SlotsPerBlock>;
// Ordinary fanouts stay contiguous; exceptionally large bags continue in
// maximum-family segments. Compact refs survive block relocation and cloning.
template<unsigned Bytes,bool AdmittedAppend=false,unsigned SlotsPerBlock=16>
using pooled_vector_bags=links<0,false,Bytes,AdmittedAppend,true,SlotsPerBlock,true>;
} // namespace ht
