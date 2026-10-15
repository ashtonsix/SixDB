#pragma once
#include "fields.h"
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace ht {
enum class buffer_event_kind {
    before_payload_write, before_entry_write, before_free_write,
    directory_relocated, entry_acquired, allocation_grown, before_release
};
// Directory IDs are representation-local. Observations are synchronous; an
// allocation or directory pointer here grants no retention or write permission.
// Only before_payload_write is byte damage. Entry/free-list events describe
// owner metadata; their sizes are not ranges inside an associated allocation.
struct buffer_event {
    buffer_event_kind kind;
    const void* owner;
    std::uint32_t id;
    const oa::Allocation* allocation;
    const void* address;
    std::size_t offset,bytes;
};

// One Orbital allocation per promoted bag, addressed by a compact directory ID.
// The hash bucket still carries its ordinary eight-byte descriptor plus tag.
// This is an exclusive private owner, not an allocator or a shared-reader body.
template<unsigned ValueBytes=8> class buffer_bags {
    static_assert(ValueBytes>0 && ValueBytes<=64);
    struct entry {
        oa::Allocation allocation;
        // With no allocation this is the next free directory ID.
        std::size_t count=0;
    };
    oa::Pool* pool_;
    std::vector<entry> entries_;
    std::uint32_t free_=0;
    bool reserved_=false;
    template<class Observer> void observe_entry(std::uint32_t id,Observer& observer)const noexcept {
        const auto& e=entries_[id-1];
        observer(buffer_event{buffer_event_kind::before_entry_write,this,id,&e.allocation,&e,0,sizeof(e)});
    }
    template<class Observer> void observe_free(Observer& observer)const noexcept {
        observer(buffer_event{buffer_event_kind::before_free_write,this,0,nullptr,&free_,0,sizeof(free_)});
    }
    template<class Observer> void release(std::uint32_t id,Observer& observer)noexcept {
        auto& e=entries_[id-1];
        observer(buffer_event{buffer_event_kind::before_release,this,id,&e.allocation,
                             e.allocation.address,0,e.allocation.size});
        observe_entry(id,observer);observe_free(observer);
        e.allocation={};e.count=free_;free_=id;
    }
  public:
    // Includes prepare followed by commit OR cancellation. It deliberately
    // charges directory/free-list observations as well as payload damage.
    static constexpr unsigned bag_value_bytes=ValueBytes;
    static constexpr unsigned reservation_event_bound=9;
    static void prepare() {
        if constexpr(ValueBytes>=8)admit_identity<8>();
        if constexpr(ValueBytes%8)admit_identity<ValueBytes%8>();
    }
    static constexpr word preparation_bytes(){return 0;}
    explicit buffer_bags(oa::Pool& pool):pool_(&pool){prepare();}
    buffer_bags(const buffer_bags&)=delete;
    buffer_bags(buffer_bags&& other)noexcept
        :pool_(other.pool_),entries_(std::move(other.entries_)),free_(std::exchange(other.free_,0)) {
        require(!other.reserved_,"move outside buffer reservation");
    }
    buffer_bags& operator=(buffer_bags&& other)noexcept {
        require(!reserved_ && !other.reserved_,"move assignment outside buffer reservation");
        if(this==&other)return *this;
        pool_=other.pool_;entries_=std::move(other.entries_);free_=std::exchange(other.free_,0);return *this;
    }
    template<class Observer> class reservation {
        friend class buffer_bags;
        buffer_bags* owner_;
        Observer* observer_;
        std::uint32_t id_;
        std::size_t first_;
        unsigned needed_;
        bool fresh_;
        reservation(buffer_bags& owner,Observer& observer,std::uint32_t id,
                    std::size_t first,unsigned needed,bool fresh)noexcept
            :owner_(&owner),observer_(&observer),id_(id),first_(first),needed_(needed),fresh_(fresh){}
        void finish()noexcept {owner_->reserved_=false;owner_=nullptr;}
        template<class Write> void publish(Write&& write)noexcept {
            require(owner_,"live buffer reservation");
            // Re-resolve after preparation; no directory/payload borrow survives
            // its growth. Existing count changes only after initialized writes.
            auto& e=owner_->entries_[id_-1];
            observer_->operator()(buffer_event{buffer_event_kind::before_payload_write,owner_,id_,
                &e.allocation,e.allocation.address,first_*ValueBytes,std::size_t(needed_)*ValueBytes});
            owner_->observe_entry(id_,*observer_);
            write(reinterpret_cast<byte*>(e.allocation.address)+first_*ValueBytes);
            e.count=first_+needed_;finish();
        }
      public:
        reservation(const reservation&)=delete;
        reservation& operator=(const reservation&)=delete;
        reservation(reservation&& other)noexcept
            :owner_(std::exchange(other.owner_,nullptr)),observer_(other.observer_),id_(other.id_),
             first_(other.first_),needed_(other.needed_),fresh_(other.fresh_){}
        reservation& operator=(reservation&& other)noexcept {
            if(this!=&other){cancel();owner_=std::exchange(other.owner_,nullptr);observer_=other.observer_;
                id_=other.id_;first_=other.first_;needed_=other.needed_;fresh_=other.fresh_;}return *this;
        }
        ~reservation(){cancel();}
        std::uint32_t head_after()const noexcept{return id_;}
        // Buffers have no chain traversal hint. Keep unused descriptor bits zero.
        std::uint32_t insertion_after()const noexcept{return 0;}
        void cancel()noexcept {
            if(!owner_)return;
            if(fresh_)owner_->release(id_,*observer_);
            finish();
        }
        void commit(std::span<const word> values)noexcept requires(ValueBytes==8) {
            require(values.size()==needed_,"buffer reservation value count");
            publish([&](byte* p)noexcept {for(unsigned i=0;i<needed_;++i)put<8>(p+std::size_t(i)*8,values[i]);});
        }
        void commit_bytes(std::span<const byte> values)noexcept {
            require(values.size()==std::size_t(needed_)*ValueBytes,"buffer reservation byte count");
            publish([&](byte* p)noexcept {std::memcpy(p,values.data(),values.size());});
        }
        void commit_ranges(std::span<const byte> first,std::span<const byte> last)noexcept {
            require(first.size()%ValueBytes==0 && last.size()%ValueBytes==0 &&
                first.size()+last.size()==std::size_t(needed_)*ValueBytes,"group buffer reservation byte count");
            publish([&](byte* p)noexcept {copy_plain_bytes(p,first);copy_plain_bytes(p+first.size(),last);});
        }
        template<std::size_t Left,std::size_t Right>
        void commit_bytes(std::span<const byte,Left> first,std::span<const byte,Right> last)noexcept {
            static_assert(Left && Right && Left%ValueBytes==0 && Right%ValueBytes==0);
            require(needed_==(Left+Right)/ValueBytes,"split buffer reservation byte count");
            publish([&](byte* p)noexcept {std::memcpy(p,first.data(),Left);std::memcpy(p+Left,last.data(),Right);});
        }
    };
    // Inputs to commit must survive reserve and be disjoint from destination
    // bytes. Copy inline bytes before any relocation; caller publishes the
    // returned ID only after commit succeeds.
    template<class Observer> [[nodiscard]] std::optional<reservation<Observer>> reserve_values(
            std::uint32_t head,unsigned needed,Observer& observer,std::uint32_t hint=0) {
        static_assert(std::is_nothrow_invocable_v<Observer&,const buffer_event&>);
        require(!reserved_ && needed>=1 && hint==0,"exclusive bounded buffer reservation");
        const bool fresh=!head;
        std::size_t first=0;
        if(fresh) {
            if(!free_) {
                if(entries_.size()>=UINT32_MAX)return {};
                try {
                    if(entries_.size()==entries_.capacity()) {
                        entries_.reserve(std::max(std::size_t(4),entries_.capacity()*2));
                        observer(buffer_event{buffer_event_kind::directory_relocated,this,0,nullptr,
                            entries_.data(),0,entries_.size()*sizeof(entry)});
                    }
                }catch(const std::bad_alloc&){return {};}
            }
            auto allocation=oa::allocate(*pool_,{.bytes=std::size_t(needed)*ValueBytes,.alignment=8});
            if(!allocation)return {};
            if(free_) {
                head=free_;auto& e=entries_[head-1];observe_entry(head,observer);observe_free(observer);
                free_=e.count;e.allocation=std::move(*allocation);e.count=0;
            }else {
                entries_.push_back({std::move(*allocation),0});head=entries_.size();
            }
            const auto& e=entries_[head-1];
            observer(buffer_event{buffer_event_kind::entry_acquired,this,head,&e.allocation,&e,0,sizeof(e)});
        }else {
            auto& e=entries_[head-1];require(bool(e.allocation),"live buffer ID");first=e.count;
            constexpr auto max_count=std::numeric_limits<std::size_t>::max()/ValueBytes;
            if(needed>max_count || first>max_count-needed)return {};
            const auto size=first+needed;
            if(size*ValueBytes>e.allocation.size) {
                // Geometric growth is a measured policy, independent of bucket
                // cardinality. Reject before bit_ceil or multiplication wraps.
                if(size>std::bit_floor(max_count))return {};
                if(!oa::grow(e.allocation,{.bytes=std::bit_ceil(size)*ValueBytes,.alignment=8}))return {};
                observer(buffer_event{buffer_event_kind::allocation_grown,this,head,&e.allocation,
                    e.allocation.address,0,e.allocation.size});
            }
        }
        reserved_=true;return reservation<Observer>(*this,observer,head,first,needed,fresh);
    }
    std::size_t value_count(std::uint32_t id)const {return id?entries_[id-1].count:0;}
    std::size_t release_event_bound(std::uint32_t id)const noexcept {return id?3:0;}
    std::pair<word,std::size_t> release_info(std::uint32_t id)const {return {value_count(id),release_event_bound(id)};}
    template<class Observer> word release_values(std::uint32_t id,Observer& observer)noexcept {
        require(!reserved_,"release outside buffer reservation");if(!id)return 0;
        const auto count=value_count(id);release(id,observer);return count;
    }
    // A nonsuspending read scope. The visitor must not mutate/move this owner;
    // neither the directory nor the payload address may relocate during it.
    template<class Visit> [[gnu::always_inline]] void each(std::uint32_t id,Visit&& visit)const requires(ValueBytes==8) {
        if(!id)return;const auto& e=entries_[id-1];const auto* p=reinterpret_cast<const byte*>(e.allocation.address);
        for(std::size_t i=0;i<e.count;++i)visit(get<8>(p+std::size_t(i)*8),word(0));
    }
    // The same scope contract applies to borrowed byte spans.
    template<class Visit> void each_bytes(std::uint32_t id,Visit&& visit)const {
        if(!id)return;const auto& e=entries_[id-1];const auto* p=reinterpret_cast<const byte*>(e.allocation.address);
        for(std::size_t i=0;i<e.count;++i)visit(std::span<const byte,ValueBytes>(p+i*ValueBytes,ValueBytes));
    }
    std::span<const byte> directory_storage()const {
        return {reinterpret_cast<const byte*>(entries_.data()),entries_.size()*sizeof(entry)};
    }
    std::span<const byte> directory_window(std::uint32_t id)const {
        return {reinterpret_cast<const byte*>(&entries_[id-1]),sizeof(entry)};
    }
    auto resolve_values(std::uint32_t id)const {
        const auto& e=entries_[id-1];
        return std::pair{std::span<const byte>(reinterpret_cast<const byte*>(e.allocation.address),e.count*ValueBytes),e.count};
    }
    bag_value_window value_window(std::uint32_t id)const {return {resolve_values(id).first,0};}
    template<class Visit> [[gnu::always_inline]] static std::uint32_t consume_values(
            const byte* p,std::size_t count,Visit&& visit) {
        for(std::size_t i=0;i<count;++i)visit(std::span<const byte,ValueBytes>(p+i*ValueBytes,ValueBytes));
        return 0;
    }
    template<class Visit> void allocations(Visit&& visit)const {
        for(const auto& e:entries_)if(e.allocation)visit(e.allocation);
    }
    std::optional<buffer_bags> clone(oa::Pool& pool)const {
        require(!reserved_,"clone outside buffer reservation");buffer_bags result(pool);
        try{result.entries_.resize(entries_.size());}catch(const std::bad_alloc&){return {};}
        for(std::size_t i=0;i<entries_.size();++i) {
            const auto& from=entries_[i];auto& to=result.entries_[i];to.count=from.count;
            if(!from.allocation)continue;
            auto allocation=oa::allocate(pool,{.bytes=from.allocation.size,.alignment=8});
            if(!allocation)return {};
            std::memcpy(allocation->address,from.allocation.address,from.count*ValueBytes);
            to.allocation=std::move(*allocation);
        }
        result.free_=free_;return result;
    }
    memory footprint()const {
        memory out{0,0,sizeof(*this)+entries_.capacity()*sizeof(entry)};
        allocations([&](const oa::Allocation& a){out.requested+=a.size;out.backing+=oa::capacity(a);});return out;
    }
    void validate()const {
        std::vector<bool> free(entries_.size());
        for(auto id=free_;id;) {
            require(id<=entries_.size() && !free[id-1],"buffer free-list bounds/cycle");free[id-1]=true;
            const auto& e=entries_[id-1];require(!e.allocation,"free buffer has no allocation");id=e.count;
        }
        for(std::size_t i=0;i<entries_.size();++i) {
            const auto& e=entries_[i];
            require(bool(e.allocation)!=free[i],"every directory slot live or free");
            if(e.allocation)require(e.count<=e.allocation.size/ValueBytes,"buffer initialized extent");
        }
    }
};
} // namespace ht
