#pragma once
#include "capacity-buffers.h"

namespace write_arch {
// Architectural counterfactual: one allocation per external bag, addressed
// directly from its private cell. It retains full StreamPack reservations and
// the pool's growth classes, but removes both block-directory addressing and
// pooled reclamation. These process-local pointers are not persistent BlockIDs.
template<unsigned V> class direct_capacity_buffers {
    static_assert(V==8||V==64);
    static constexpr unsigned maximum=65535/V,last_class=std::bit_width(maximum),classes=last_class+1;
    struct block {
        block* previous=nullptr;
        block* next=nullptr;
        std::size_t allocated=0;
        unsigned which=0;
        std::array<byte,2> control{};
    };
    static_assert(sizeof(block)==32);
    struct family {std::optional<stream::layout> format;unsigned capacity=0;};
    std::array<family,classes> families_;
    block* first_=nullptr;
    std::size_t allocated_=0,live_=0;
    static byte* payload(block* p){return reinterpret_cast<byte*>(p)+sizeof(block);}
    static block* header(word reference){return reinterpret_cast<block*>(reference-sizeof(block));}
    stream::view view(block& p)const {
        auto* bytes=reinterpret_cast<stream::byte*>(payload(&p));
        return stream::view::assume_valid(*families_[p.which].format,
            {{bytes,std::size_t(families_[p.which].capacity)*V},{},{},
             {reinterpret_cast<stream::byte*>(p.control.data()),2}});
    }
    static void destroy(block* p)noexcept {p->~block();std::free(p);}
  public:
    static constexpr unsigned max_values=maximum;
    direct_capacity_buffers() {
        for(unsigned i=0;i<classes;++i){auto& f=families_[i];f.capacity=std::min(1u<<i,maximum);
            stream::description d{.family=stream::geometry::direct,.maximum=std::uint16_t(f.capacity*V),
                .byte_limit=f.capacity*V,.fixed_reservation=std::uint16_t(f.capacity*V),.full=true,.immortal=true};
            f.format=stream::layout::prepare(d).value();
            ht::require(f.format->directory_bytes()==0&&f.format->lengths_bytes()==0&&f.format->control_bytes()==2,"direct buffer recipe");
        }
    }
    direct_capacity_buffers(const direct_capacity_buffers&)=delete;
    ~direct_capacity_buffers(){while(first_){auto* next=first_->next;destroy(first_);first_=next;}}
    static unsigned class_for(unsigned count){ht::require(count&&count<=maximum,"direct bag maximum");return std::min(unsigned(std::bit_width(count-1)),last_class);}
    unsigned capacity(unsigned which)const{return families_[which].capacity;}
    byte* data(word reference,unsigned)const{return reinterpret_cast<byte*>(reference);}
    void append_fitting(word reference,unsigned,unsigned count,const ht::bytes<V>& incoming) {
        ikea::no_coverage effects;tuple_value<V>(reinterpret_cast<byte*>(reference)+std::size_t(count)*V,incoming,effects);
    }
    word create(unsigned which,std::span<const byte> prefix,const ht::bytes<V>& incoming) {
        const auto reservation=families_[which].capacity*V;
        ht::require(prefix.size()+V<=reservation,"direct buffer payload");
        // The 32-byte allocation header includes private owner links/accounting;
        // only two bytes are StreamPack metadata. No cache-line rounding is
        // required by these unaligned TuplePack/opaque payload consumers.
        const std::size_t bytes=sizeof(block)+reservation;
        auto* storage=std::malloc(bytes);if(!storage)throw std::bad_alloc();
        std::unique_ptr<block,decltype(&destroy)> owned(new(storage) block{},&destroy);
        auto& next=*owned;next.which=which;next.allocated=bytes;
        const auto source=view(next);ikea::no_coverage effects;
        ht::require(bool(stream::initialize(source,effects)),"direct buffer initialize");
        auto* p=payload(&next);
        if(prefix.size())std::memmove(p,prefix.data(),prefix.size());
        tuple_value<V>(p+prefix.size(),incoming,effects);
        const auto initialized=prefix.size()+V;
        if(initialized<reservation)std::memset(p+initialized,0,reservation-initialized);
        const auto format=source.format();
        const auto fixed=format.fixed(),used=format.has_used(),tombs=format.has_tombstones(),extent=format.has_extent();
        const auto control=format.control_bytes();
        __builtin_assume(fixed);__builtin_assume(!used);__builtin_assume(!tombs);__builtin_assume(!extent);__builtin_assume(control==2);
        const ht::bag_write::cursor cursor{&source,0,0};
        const ht::bag_write::append_access<std::uint16_t,std::uint32_t> access{&cursor,&format};
        [[clang::always_inline]] stream::author::append_fixed_run_unchecked(access,
            {reinterpret_cast<const stream::byte*>(p),reservation},1,effects);
        // No old cell/buffer changes until allocation and construction succeed.
        next.next=first_;if(first_)first_->previous=&next;first_=owned.release();
        allocated_+=bytes;++live_;return reinterpret_cast<word>(p);
    }
    void release(word reference)noexcept {
        auto* p=header(reference);
        if(p->previous)p->previous->next=p->next;else first_=p->next;
        if(p->next)p->next->previous=p->previous;
        allocated_-=p->allocated;--live_;destroy(p);
    }
    std::size_t bytes_used()const{return sizeof(*this)+allocated_;}
    std::size_t live_buffers()const{return live_;}
    struct reference_witness {word reference;unsigned which,count;};
    void validate(std::span<const reference_witness> owners)const {
        std::vector<reference_witness> ordered(owners.begin(),owners.end());
        std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a.reference<b.reference;});
        for(std::size_t i=1;i<ordered.size();++i)ht::require(ordered[i-1].reference!=ordered[i].reference,"unique direct bag owner");
        block* previous=nullptr;std::size_t count=0,bytes=0;
        for(auto* p=first_;p;p=p->next){
            ht::require(++count<=live_&&p->previous==previous&&p->which<classes,"direct allocation links");
            const auto ref=reinterpret_cast<word>(payload(p));
            const auto found=std::lower_bound(ordered.begin(),ordered.end(),ref,[](auto a,word b){return a.reference<b;});
            ht::require(found!=ordered.end()&&found->reference==ref&&found->which==p->which&&found->count&&found->count<=capacity(p->which),"direct allocation owner");
            ht::require(bool(view(*p).validate())&&ht::load<2>(p->control.data())==1,"direct StreamPack source");
            ht::require(p->allocated==sizeof(block)+std::size_t(capacity(p->which))*V,"direct allocation extent");
            bytes+=p->allocated;previous=p;
        }
        ht::require(count==live_&&count==owners.size()&&bytes==allocated_,"direct allocation accounting");
    }
};
}
