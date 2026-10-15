#pragma once
#include "common.h"
#include <orbital/object.h>
#include <memory_resource>

namespace ht {
// ObjectStore currently consumes complete materials, not Ikea byte journals.
// This adapter captures an already private body. It is the publication/lifetime
// control, not an invented damage consumer or a chosen COW granularity.
template<class Body> struct publication_material {
    struct Value {std::pmr::memory_resource* resource;std::size_t references;Body body;Value* next;};
    inline static Value* live=nullptr;
    struct Selection {Value* value=nullptr;explicit operator bool()const{return value;}};
    struct Specification {Body* pending;};
    using PartSource=std::byte;enum class ExportPolicy {owned};
    struct Footprint {std::size_t requested_bytes,copied_metadata;};
    static Value* get(Selection s)noexcept{return s.value;}
    static Selection selection(Value* v)noexcept{return {v};}
    static void retain(Value* p)noexcept {if(p)++p->references;}
    static void release(Value* p)noexcept {
        if(!p||--p->references)return;
        auto** at=&live;while(*at!=p)at=&(*at)->next;*at=p->next;
        auto* resource=p->resource;std::destroy_at(p);resource->deallocate(p,sizeof(Value),alignof(Value));
    }
    static orbital::Result<Value*> capture(std::pmr::memory_resource& resource,
            const Specification& spec,std::span<const PartSource> parts)noexcept {
        if(!spec.pending||!parts.empty())return std::unexpected(orbital::Error::invalid_argument);
        try {
            static_assert(std::is_nothrow_move_constructible_v<Body>);
            auto* p=static_cast<Value*>(resource.allocate(sizeof(Value),alignof(Value)));
            std::construct_at(p,Value{&resource,1,std::move(*spec.pending),live});live=p;return p;
        }catch(const std::bad_alloc&) {return std::unexpected(orbital::Error::capacity);}
    }
    static bool accepts(Value*,orbital::ObjectId,std::uint64_t)noexcept{return true;}
    static bool accepts_resource(Value* p,std::pmr::memory_resource& r)noexcept{return p->resource==&r;}
    static bool compatible(Value*,Value*)noexcept{return false;}
    static Footprint footprint(Value*)noexcept{return {sizeof(Value),0};}
    static memory retained() {
        memory m;for(auto* p=live;p;p=p->next) {
            auto b=p->body.footprint();m.requested+=b.requested;m.backing+=b.backing;
            // Body itself is already in catalog_resource's live allocation.
            require(b.metadata>=sizeof(Body),"material body accounting");m.metadata+=b.metadata-sizeof(Body);
        }return m;
    }
    struct Export {
        Value* value;orbital::ObjectBinding binding;
        Export(Value* p,orbital::ObjectBinding b):value(p),binding(b){retain(p);}
        Export(Export&& e)noexcept:value(std::exchange(e.value,nullptr)),binding(e.binding){}
        ~Export(){release(value);}
    };
    static orbital::Result<Export> export_part(Selection s,std::size_t i,orbital::ObjectBinding b,ExportPolicy)noexcept {
        if(!s||i)return std::unexpected(orbital::Error::absent);return Export(s.value,b);
    }
};
struct catalog_resource:std::pmr::memory_resource {
    std::size_t live=0,peak=0,calls=0,fail_at=SIZE_MAX;
    void* do_allocate(std::size_t bytes,std::size_t alignment)override {
        if(calls++==fail_at)throw std::bad_alloc{};
        auto* p=std::pmr::new_delete_resource()->allocate(bytes,alignment);
        live+=bytes;peak=std::max(peak,live);return p;
    }
    void do_deallocate(void* p,std::size_t bytes,std::size_t alignment)override {
        require(live>=bytes,"catalog accounting");live-=bytes;
        std::pmr::new_delete_resource()->deallocate(p,bytes,alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other)const noexcept override{return this==&other;}
};
} // namespace ht
