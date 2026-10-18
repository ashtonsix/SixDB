#pragma once
#include "storage.h"
#include <orbital/allocation.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
#ifndef IBS_ALLOCATION_TRACE
#define IBS_ALLOCATION_TRACE 0
#endif

namespace iceberg {
namespace oa=orbital::allocation;
#if IBS_ALLOCATION_TRACE
// Diagnostic-only build: logical calls and bytes at this owner's storage seam.
// This excludes vector metadata, Abseil allocations, and physical page traffic.
struct allocation_events {
    std::size_t allocations=0,assigned_bytes=0,growths=0,relocations=0,copied_bytes=0,zeroed_bytes=0;
    std::size_t block_scans=0,block_resets=0;
};
inline thread_local allocation_events allocation_trace;
#endif
// Borrowed caller Pool. The caller retains its Lane/Pool until every Allocation
// and captured Hold has retired. Allocation selection is absent from lookups.
struct allocation_context {oa::Pool& pool;};
struct storage_error:std::runtime_error {
    orbital::Error reason;
    explicit storage_error(orbital::Error why):std::runtime_error("Orbital storage preparation refused"),reason(why){}
};
struct allocation {
    std::uint8_t* data=nullptr;
    std::size_t size=0;
  private:
    std::size_t heap_capacity_=0;
    allocation_context* context_=nullptr;
    oa::Allocation storage_;
    static std::size_t round64(std::size_t n){
        if(n>std::numeric_limits<std::size_t>::max()-63)throw std::length_error("allocation extent");
        return (n+63)&~std::size_t{63};
    }
    void refresh(){data=reinterpret_cast<std::uint8_t*>(storage_.address);size=storage_.size;}
  public:
    allocation()=default;
    explicit allocation(std::size_t n,allocation_context* context=nullptr,bool zero=true,std::size_t reserve=0):context_(context){
        n=round64(n);reserve=round64(std::max(n,reserve));
        if(context_){
            auto result=oa::allocate(context_->pool,{.bytes=n,.reserved_bytes=reserve,.alignment=64,
                .initialization=zero?oa::Initialization::zero:oa::Initialization::uninitialized});
            if(!result)throw storage_error(result.error());storage_=std::move(*result);refresh();
        }else {
            data=static_cast<std::uint8_t*>(std::aligned_alloc(64,reserve));
            if(!data)throw std::bad_alloc();size=n;heap_capacity_=reserve;
            if(zero)std::memset(data,0,n);
        }
#if IBS_ALLOCATION_TRACE
        ++allocation_trace.allocations;allocation_trace.assigned_bytes+=n;
        if(zero)allocation_trace.zeroed_bytes+=n;
#endif
    }
    allocation(const allocation&)=delete;
    allocation(allocation&& other)noexcept{swap(other);}
    allocation& operator=(allocation&& other)noexcept{swap(other);return *this;}
    void swap(allocation& other)noexcept{
        std::swap(data,other.data);std::swap(size,other.size);std::swap(heap_capacity_,other.heap_capacity_);
        std::swap(context_,other.context_);std::swap(storage_,other.storage_);
    }
    ~allocation(){if(!context_)std::free(data);}
    allocation_context* context()const{return context_;}
    std::size_t capacity()const{return context_?oa::capacity(storage_):heap_capacity_;}
    std::size_t reserved_bytes()const{return context_?oa::reserved_bytes(storage_):heap_capacity_;}
    storage_usage usage()const{return {size,capacity(),reserved_bytes()};}
    // Ordinary private-owner preflight. Refusal preserves the previous bytes;
    // the caller decides how to suspend/retry a reclaim conflict or capacity wait.
    void grow(std::size_t n,std::size_t reserve=0,bool zero_new=true){
        n=round64(n);reserve=round64(std::max(n,reserve));
        if(n<size)throw std::invalid_argument("growth would contract storage");
        const auto previous=size;
#if IBS_ALLOCATION_TRACE
        const auto previous_address=reinterpret_cast<std::uintptr_t>(data);
        const auto copied_extent=context_?storage_.size+storage_.readable_padding:size;
#endif
        if(context_){
            const auto result=oa::grow(storage_,{.bytes=n,.reserved_bytes=reserve,.alignment=64});
            if(!result)throw storage_error(result.error());refresh();
        }else if(reserve>heap_capacity_){
            allocation replacement(n,nullptr,false,reserve);
            if(size)std::memcpy(replacement.data,data,size);swap(replacement);
        }else size=n;
        if(zero_new&&n>previous)std::memset(data+previous,0,n-previous);
#if IBS_ALLOCATION_TRACE
        ++allocation_trace.growths;
        if(reinterpret_cast<std::uintptr_t>(data)!=previous_address){
            ++allocation_trace.relocations;allocation_trace.copied_bytes+=copied_extent;
        }
        if(zero_new&&n>previous)allocation_trace.zeroed_bytes+=n-previous;
#endif
    }
    oa::Hold hold(orbital::ByteRange range)const {
        if(!context_)throw std::invalid_argument("heap storage has no Orbital Hold");
        auto result=oa::hold(storage_,range);if(!result)throw storage_error(result.error());return std::move(*result);
    }
    // Retain the actual exposed extent, including initialized/readable padding.
    void retain(std::vector<oa::Hold>& holds)const {if(context_&&size)holds.push_back(hold({0,size}));}
    orbital::Result<void> relinquish_tail(std::size_t n){
        n=round64(n);
        if(n>size)return std::unexpected(orbital::Error::invalid_argument);
        if(context_&&storage_){
            auto result=oa::relinquish_tail(storage_,n);if(!result)return result;refresh();
        }else size=n;
        return {};
    }
    orbital::Result<void> relinquish_surplus(){return relinquish_tail(size);}
    const oa::Allocation& orbital_storage()const{return storage_;}
    oa::Allocation& orbital_storage(){return storage_;}
};
// Whole-buffer storage is a static owner choice. Lookups never dispatch through
// this policy, and captured retention retains each concrete allocation.
struct orbital_storage_policy {
    using context=allocation_context;
    using buffer=allocation;
    using retention=std::vector<oa::Hold>;
    using trim_result=orbital::Result<void>;
};

}
