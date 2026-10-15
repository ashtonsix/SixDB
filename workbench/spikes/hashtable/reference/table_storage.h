#pragma once
#include "table_bucket.h"
#include <memory>

namespace hashtable_spike::tiered {
enum class sharing { flat, chunks };

// The table compares flat replacement with a two-level directory of immutable
// chunks. This is a physical sharing decision, independent of bucket encoding.
// Preparing a writer may replace equal bytes but performs no logical mutation.
template<bucket_plan P,sharing Kind> class bucket_storage {
    static constexpr std::size_t buckets=std::size_t{1}<<P.address_bits;
    static constexpr unsigned chunk_bits=std::min(4u,P.address_bits);
    static constexpr unsigned leaf_bits=std::min(7u,P.address_bits-chunk_bits);
    static constexpr std::size_t per_chunk=std::size_t{1}<<chunk_bits;
    static constexpr std::size_t per_leaf=std::size_t{1}<<leaf_bits;
    static constexpr std::size_t chunks=buckets/per_chunk;
    struct body {
        oa::Allocation allocation;
        explicit body(oa::Allocation a):allocation(std::move(a)){}
    };
    struct leaf {
        std::array<std::shared_ptr<body>,per_leaf> owners;
        std::array<byte*,per_leaf> addresses{};
    };
    struct leaf_handle {
        std::shared_ptr<leaf> owner;
        byte* const* addresses{};
        explicit leaf_handle(std::shared_ptr<leaf> p):owner(std::move(p)),addresses(owner->addresses.data()){}
    };
    oa::Pool* pool_;
    std::shared_ptr<body> flat_;
    byte* flat_address_{};
    std::vector<leaf_handle> roots_;
    explicit bucket_storage(oa::Pool& pool):pool_(&pool){}
    orbital::Result<std::shared_ptr<body>> allocate(std::size_t bytes,const byte* source=nullptr) {
        auto result=oa::allocate(*pool_,{.bytes=bytes,.alignment=64});
        if(!result)return std::unexpected(result.error());
        if(source)std::memcpy(result->address,source,bytes);
        try {return std::make_shared<body>(std::move(*result));}
        catch(const std::bad_alloc&){return std::unexpected(orbital::Error::capacity);}
    }
  public:
    bucket_storage(const bucket_storage&)=default;
    bucket_storage& operator=(const bucket_storage&)=default;
    bucket_storage(bucket_storage&&)noexcept=default;
    bucket_storage& operator=(bucket_storage&&)noexcept=default;
    static orbital::Result<bucket_storage> make(oa::Pool& pool,const bucket_binding<P>& shared) {
        bucket_storage result(pool);
        if constexpr(Kind==sharing::flat) {
            auto memory=result.allocate(buckets*P.bytes);if(!memory)return std::unexpected(memory.error());
            result.flat_=std::move(*memory);result.flat_address_=reinterpret_cast<byte*>(result.flat_->allocation.address);
        } else {
            try {
                result.roots_.reserve((chunks+per_leaf-1)/per_leaf);
                for(std::size_t i=0;i<chunks;i+=per_leaf) {
                    auto page=std::make_shared<leaf>();
                    for(std::size_t j=0;j<std::min(per_leaf,chunks-i);++j) {
                        auto memory=result.allocate(per_chunk*P.bytes);if(!memory)return std::unexpected(memory.error());
                        page->owners[j]=std::move(*memory);page->addresses[j]=reinterpret_cast<byte*>(page->owners[j]->allocation.address);
                    }
                    result.roots_.emplace_back(std::move(page));
                }
            }catch(const std::bad_alloc&){return std::unexpected(orbital::Error::capacity);}
        }
        for(std::size_t i=0;i<buckets;++i)bucket<P>{result.at(i),shared}.initialize();
        return result;
    }
    [[gnu::always_inline]] byte* at(std::size_t i)const {
        if constexpr(Kind==sharing::flat)return flat_address_+i*P.bytes;
        else {
            const auto chunk=i>>chunk_bits;
            return roots_[chunk>>leaf_bits].addresses[chunk&(per_leaf-1)]+(i&(per_chunk-1))*P.bytes;
        }
    }
    orbital::Result<void> prepare(std::size_t i) {
        if constexpr(Kind==sharing::flat) {
            if(!flat_.unique()) {
                auto copy=allocate(buckets*P.bytes,flat_address_);if(!copy)return std::unexpected(copy.error());
                flat_=std::move(*copy);flat_address_=reinterpret_cast<byte*>(flat_->allocation.address);
            }
        } else {
            const auto chunk=i>>chunk_bits;
            auto& root=roots_[chunk>>leaf_bits];
            if(!root.owner.unique()) {
                try {root=leaf_handle(std::make_shared<leaf>(*root.owner));}
                catch(const std::bad_alloc&){return std::unexpected(orbital::Error::capacity);}
            }
            const auto rank=chunk&(per_leaf-1);auto& owner=root.owner->owners[rank];
            if(!owner.unique()) {
                auto copy=allocate(per_chunk*P.bytes,root.owner->addresses[rank]);if(!copy)return std::unexpected(copy.error());
                owner=std::move(*copy);root.owner->addresses[rank]=reinterpret_cast<byte*>(owner->allocation.address);
            }
        }
        return {};
    }
    // Visit actual allocation identities to deduplicate shared versions in
    // untimed accounting. Summing per-version requested sizes overstates live
    // physical memory for the chunked representation.
    template<class F> void allocations(F&& f)const {
        if constexpr(Kind==sharing::flat)f(flat_->allocation);
        else for(const auto& root:roots_)for(const auto& owner:root.owner->owners)if(owner)f(owner->allocation);
    }
    template<class F> void metadata(F&& f)const {
        if constexpr(Kind==sharing::flat)f(flat_.get(),sizeof(body));
        else {
            f(roots_.data(),roots_.capacity()*sizeof(leaf_handle));
            for(const auto& root:roots_) {
                f(root.owner.get(),sizeof(leaf));
                for(const auto& owner:root.owner->owners)if(owner)f(owner.get(),sizeof(body));
            }
        }
        // These are known structures, not allocator requests: std::shared_ptr
        // control-block bytes and allocation rounding are not included.
    }
};
} // namespace hashtable_spike::tiered
