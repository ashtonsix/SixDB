#pragma once
#include "table_value.h"
#include <ikea/streampack.h>
#include <memory>

namespace hashtable_spike::tiered {
namespace stream=ikea::streampack;
inline constexpr std::uint32_t no_link=~std::uint32_t{};

// Representation-local block/rank references. A copy shares immutable blocks;
// a writer copies a shared block before changing its bytes or growing it.
// This owns StreamPack's storage and links, not another allocation mechanism:
// every payload is an ordinary Orbital Allocation and grows through oa::grow.
template<unsigned K,unsigned V,bool Direct=false> class chain_store {
    static_assert(K>=8&&K<=64&&K%8==0&&V%8==0);
    static constexpr unsigned entry_bytes=(K+V)/8,header_bytes=8,block_header=48;
    static constexpr unsigned max_capacity=std::bit_floor((65535-header_bytes)/entry_bytes);
    static_assert(max_capacity>=4,"This geometric-link experiment requires four entries to fit one StreamPack slot");
    static constexpr unsigned classes=std::bit_width(max_capacity)-2;
    static constexpr unsigned max_width=header_bytes+max_capacity*entry_bytes;
    static unsigned capacity(unsigned c){return 4u<<c;}
    static unsigned width(unsigned c){return header_bytes+capacity(c)*entry_bytes;}
    static stream::layout format(unsigned c) {
        const auto w=width(c);
        auto l=stream::layout::prepare({.maximum=std::uint16_t(w),.byte_limit=256*w,
                                      .fixed_reservation=std::uint16_t(w),.full=true});
        require(bool(l)&&l->directory_bytes()==32&&l->control_bytes()==2&&l->lengths_bytes()==0);
        return *l;
    }
    template<std::size_t... I> static auto make_formats(std::index_sequence<I...>) {
        return std::array{format(unsigned(I))...};
    }
    inline static const auto formats=make_formats(std::make_index_sequence<classes>{});
    inline static const std::array<std::byte,max_width> zeroes{};
    inline static const single::field<32> next_field{};
    inline static const single::field<16> count_field{};
    inline static const single::field<K> key_field{};
    inline static const value_binding<V,1> value_field{};
    struct block {
        oa::Allocation allocation;
        unsigned family{},live{};
        block(oa::Allocation a,unsigned c,unsigned n=0):allocation(std::move(a)),family(c),live(n){}
    };
    struct slot {std::shared_ptr<block> body;std::uint32_t free_next=no_link;};
    oa::Pool* pool_;
    std::vector<slot> blocks_;
    std::array<std::uint32_t,classes> current_=[] {std::array<std::uint32_t,classes> a; a.fill(no_link);return a;}();
    std::uint32_t free_head_=no_link;
    static stream::view bind(block& b) {
        auto* p=b.allocation.address;
        return stream::view::assume_valid(formats[b.family],
            {{p+block_header,b.allocation.size-block_header},{p+2,32},{},{p,2}});
    }
    static stream::const_view bind(const block& b) {
        const auto* p=b.allocation.address;
        return stream::const_view::assume_valid(formats[b.family],
            {{p+block_header,b.allocation.size-block_header},{p+2,32},{},{p,2}});
    }
    orbital::Result<block*> writable(unsigned id) {
        auto& owned=blocks_[id].body;
        if(!owned.unique()) {
            auto memory=oa::allocate(*pool_,{.bytes=owned->allocation.size,.alignment=16});
            if(!memory)return std::unexpected(memory.error());
            std::memcpy(memory->address,owned->allocation.address,memory->size);
            try {owned=std::make_shared<block>(std::move(*memory),owned->family,owned->live);}
            catch(const std::bad_alloc&){return std::unexpected(orbital::Error::capacity);}
        }
        return owned.get();
    }
    static byte* address(block& b,unsigned rank) {
        auto view=bind(b);
        const auto value=stream::author::dispatch(view,[&](auto access){return access.value(rank);});
        // The caller established private write authority before binding.
        return reinterpret_cast<byte*>(b.allocation.address+block_header+(value.data()-view.windows().payload.data()));
    }
    static const byte* address(const block& b,unsigned rank) {
        auto view=bind(b);
        return stream::author::dispatch(view,[&](auto access){return reinterpret_cast<const byte*>(access.value(rank).data());});
    }
  public:
    explicit chain_store(oa::Pool& pool):pool_(&pool){}
    // Directory copies retain immutable blocks. All borrows end before mutation.
    chain_store(const chain_store&)=default;
    chain_store& operator=(const chain_store&)=default;
    chain_store(chain_store&&)noexcept=default;
    chain_store& operator=(chain_store&&)noexcept=default;
    void clear()noexcept {
        // Retain only directory capacity. Published copies keep their blocks;
        // this writer releases its references without changing shared bytes.
        blocks_.clear();current_.fill(no_link);free_head_=no_link;
    }
    struct link {
        const byte* data;
        unsigned capacity;
        std::uint32_t next()const{return std::uint32_t(next_field.template get<Direct>(data));}
        unsigned size()const{return unsigned(count_field.template get<Direct>(data+4));}
        word key(unsigned i)const{return key_field.template get<Direct>(data+header_bytes+i*entry_bytes);}
        auto read_value(unsigned i)const {
            return value_access<V,1,Direct,false>{value_field,data+header_bytes+i*entry_bytes+K/8,0};
        }
        word value(unsigned i)const requires(V<=64){return read_value(i).read();}
    };
    link read(std::uint32_t ref)const {
        const auto& b=*blocks_[ref>>8].body;
        return {address(b,ref&255),capacity(b.family)};
    }
    // Immutable read-epoch stages. Each method only consumes the window hinted
    // by its predecessor; resolving a reference is not hidden in a prefetch call.
    struct read_probe {
        const slot* entry{};
        const block* body{};
        link payload{};
        unsigned rank{};
    };
    template<class Emit>read_probe prepare_read(std::uint32_t ref,Emit&& emit)const {
        const auto* entry=&blocks_[ref>>8];emit(reinterpret_cast<const byte*>(entry),sizeof(slot));
        return {.entry=entry,.rank=ref&255};
    }
    template<class Emit>static void read_directory(read_probe& p,Emit&& emit) {
        p.body=p.entry->body.get();emit(reinterpret_cast<const byte*>(p.body),sizeof(block));
    }
    template<class Emit>static void read_body(read_probe& p,Emit&& emit) {
        // Fixed/full addressing reads no per-slot geometry or liveness. The
        // published link reference already identifies a live immutable value;
        // staging the block's control prefix would invent a dependency.
        p.payload={address(*p.body,p.rank),capacity(p.body->family)};
        // These fixed/full reservations are initialized in their entirety.
        // Capacity is not used to invent bytes beyond the StreamPack value.
        emit(p.payload.data,width(p.body->family));
    }
    unsigned family(std::uint32_t ref)const{return blocks_[ref>>8].body->family;}
    static unsigned next_family(unsigned c){return std::min(c+1,classes-1);}

    // Private construction completes before returning a discoverable reference.
    // Full reservations omit U; supplying the initialized zero image is charged
    // work, including its copy. No unchecked append bypasses full's contract.
    orbital::Result<std::uint32_t> allocate(unsigned c) {
        require(c<classes);
        auto id=current_[c];
        if(id==no_link||bind(*blocks_[id].body).size()==256) {
            auto memory=oa::allocate(*pool_,{.bytes=block_header+width(c),.alignment=16});
            if(!memory)return std::unexpected(memory.error());
            std::memset(memory->address,0,block_header);
            std::shared_ptr<block> added;
            try {
                added=std::make_shared<block>(std::move(*memory),c);
                if(free_head_==no_link) {
                    if(blocks_.size()>=0xffffffu)return std::unexpected(orbital::Error::capacity);
                    id=std::uint32_t(blocks_.size());blocks_.push_back({added});
                } else {
                    id=free_head_;free_head_=blocks_[id].free_next;blocks_[id]={added};
                }
            }catch(const std::bad_alloc&){return std::unexpected(orbital::Error::capacity);}
            auto view=bind(*added);ikea::no_coverage scratch;require(bool(stream::initialize(view,scratch)));
            current_[c]=id;
        }
        auto owned=writable(id);if(!owned)return std::unexpected(owned.error());
        auto& b=**owned;
        const auto rank=bind(b).size();
        const auto needed=block_header+(rank+1)*width(c);
        if(b.allocation.size<needed) {
            const auto grown=oa::grow(b.allocation,{.bytes=needed,.alignment=16});
            if(!grown)return std::unexpected(grown.error());
        }
        auto view=bind(b);ikea::no_coverage scratch;
        require(bool(stream::append(view,std::span(zeroes).first(width(c)),width(c),scratch)));
        auto* p=address(b,rank);next_field.template put<Direct>(p,no_link);
        ++b.live;return (id<<8)|rank;
    }
    // The enclosing mutation prepares every affected bucket/block before its
    // first discoverable store. Repeated calls on private blocks are cheap.
    orbital::Result<void> prepare(std::uint32_t ref) {
        auto result=writable(ref>>8);if(!result)return std::unexpected(result.error());return {};
    }
    void next(std::uint32_t ref,std::uint32_t next) {
        auto& b=*blocks_[ref>>8].body;require(blocks_[ref>>8].body.unique());
        next_field.template put<Direct>(address(b,ref&255),next);
    }
    template<class Recipe>void append_with(std::uint32_t ref,word key,Recipe&& recipe) {
        auto& b=*blocks_[ref>>8].body;require(blocks_[ref>>8].body.unique());auto* p=address(b,ref&255);
        const auto n=unsigned(count_field.template get<Direct>(p+4));require(n<capacity(b.family));
        key_field.template put<Direct>(p+header_bytes+n*entry_bytes,key);
        recipe(value_access<V,1,Direct,true>{value_field,p+header_bytes+n*entry_bytes+K/8,0});
        count_field.template put<Direct>(p+4,n+1);
    }
    void append(std::uint32_t ref,word key,word value) requires(V<=64) {
        append_with(ref,key,[&](auto at){at.assign(value);});
    }
    template<class Recipe>void value_with(std::uint32_t ref,unsigned i,Recipe&& recipe) {
        auto& b=*blocks_[ref>>8].body;require(blocks_[ref>>8].body.unique());
        recipe(value_access<V,1,Direct,true>{value_field,address(b,ref&255)+header_bytes+i*entry_bytes+K/8,0});
    }
    void value(std::uint32_t ref,unsigned i,word value) requires(V<=64) {
        value_with(ref,i,[&](auto at){at.assign(value);});
    }
    void erase(std::uint32_t ref,unsigned i) {
        auto& b=*blocks_[ref>>8].body;require(blocks_[ref>>8].body.unique());auto* p=address(b,ref&255);
        const auto last=unsigned(count_field.template get<Direct>(p+4))-1;require(i<=last);
        if(i!=last) {
            key_field.template put<Direct>(p+header_bytes+i*entry_bytes,key_field.template get<Direct>(p+header_bytes+last*entry_bytes));
            const value_access<V,1,Direct,false> source{value_field,p+header_bytes+last*entry_bytes+K/8,0};
            value_access<V,1,Direct,true>{value_field,p+header_bytes+i*entry_bytes+K/8,0}.assign(source);
        }
        count_field.template put<Direct>(p+4,last);
    }
    // Caller has removed this reference from every current head/predecessor.
    // Old representations retain their separate immutable directory and block.
    void retire(std::uint32_t ref) {
        const auto id=ref>>8;auto& entry=blocks_[id];require(entry.body.unique());auto& b=*entry.body;
        auto view=bind(b);ikea::no_coverage scratch;require(bool(stream::retire(view,ref&255,scratch)));
        if(!--b.live) {
            if(current_[b.family]==id)current_[b.family]=no_link;
            entry.body.reset();entry.free_next=free_head_;free_head_=id;
        }
    }
    struct footprint {std::size_t payload{},capacity{},controls{},live_links{},assigned_links{};};
    footprint memory()const {
        footprint out{.controls=sizeof(*this)+blocks_.capacity()*sizeof(slot)};
        for(const auto& item:blocks_)if(item.body) {
            out.payload+=item.body->allocation.size;out.capacity+=oa::capacity(item.body->allocation);
            out.controls+=sizeof(block);out.live_links+=item.body->live;out.assigned_links+=bind(*item.body).size();
        }
        // shared_ptr control blocks and allocator rounding are separately
        // unmeasured; sharing can make sums across versions double count bodies.
        return out;
    }
    template<class F> void allocations(F&& f)const {
        for(const auto& item:blocks_)if(item.body)f(item.body->allocation);
    }
    template<class F> void metadata(F&& f)const {
        if(blocks_.capacity())f(blocks_.data(),blocks_.capacity()*sizeof(slot));
        for(const auto& item:blocks_)if(item.body)f(item.body.get(),sizeof(block));
    }
    template<class F> static void preparation(F&& f) {
        f(&formats,sizeof(formats));f(&next_field,sizeof(next_field));f(&count_field,sizeof(count_field));
        f(&key_field,sizeof(key_field));f(&value_field,sizeof(value_field));
    }
    static constexpr std::size_t static_zero_image_bytes=sizeof(zeroes);
    void audit()const {
        for(const auto& item:blocks_)if(item.body) {
            const auto view=bind(*item.body);require(bool(view.validate()));
            unsigned live=0;for(unsigned i=0;i<view.size();++i)live+=view.live_unchecked(i);
            require(live==item.body->live);
        }
    }
};
} // namespace hashtable_spike::tiered
