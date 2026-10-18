#pragma once
#include <ikea/effects.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>

namespace iceberg {
enum class access_intent { read, write };
enum class mutation_status { inserted, updated, unchanged, erased, absent, denied, needs_capacity };
enum class lookup_status { found, absent, denied };
enum class owner_part { header, primary, swiss_controls, swiss_tuples,
    singleton_bags, small_variable_bags, large_variable_bags, associated_bags };

// The caller resolves this live source to its object incarnation, physical part
// and generation. Pointer/offset alone is not a persistent damage identity.
struct mutation_source {
    const void* owner;
    owner_part part;
    const std::uint8_t* base;
    std::size_t extent;
};
struct mutation_damage {
    mutation_source source;
    ikea::byte_write span;
};

inline bool input_overlaps(std::span<const std::uint8_t> input,const void* base,std::size_t extent) {
    if(input.empty()||!extent)return false;
    const auto src=reinterpret_cast<std::uintptr_t>(input.data()),dst=reinterpret_cast<std::uintptr_t>(base);
    return src<=dst?dst-src<input.size():src-dst<extent;
}

// A context is supplied by the caller, not a key-lock implementation:
//   enter(owner, canonical_key, intent): validate the logical grant and retain
//       stable read storage / private or exclusively writable physical storage.
//   prepare(span<mutation_damage>): validate source generations and preflight
//       all preservation/journal capacity; false means no effects or stores.
//   before(source, byte_write) noexcept: publish qualified damage before stores.
// No callback may reenter or invalidate the owner or remaining input. A successful enter retains
// its permissions until the whole synchronous operation has returned. Distinct
// key grants alone do not protect shared packed bytes. Growth must be prepared
// separately and followed by fresh source admission.

// The multimap owns its index and arenas as one material. The index discovers
// its spans without storing; this adapter combines them with the prepared bag
// spans, admits the complete operation, and publishes all damage before the
// first index store. Leaf hooks are then covered. No allocation or suspension
// is permitted between this admission and the end of the bag commit.
template<class Context> class compound_damage {
    Context& context_;
    const void* owner_;
    const void* index_;
    std::span<const std::uint8_t> input_;
    std::array<mutation_damage,12> writes_{};
    unsigned count_=0;
  public:
    template<class Owner,class Index> compound_damage(Context& context,const Owner& owner,
            const Index& index,std::span<const std::uint8_t> input={}):
        context_(context),owner_(&owner),index_(&index),input_(input) {
        add({{owner_,owner_part::header,reinterpret_cast<const std::uint8_t*>(&owner),sizeof(owner)},
            {0,0,sizeof(owner)}});
    }
    void add(mutation_damage write) {
        // The fixed Direct StreamPack recipe needs at most five spans, plus
        // one retirement, three index spans and the enclosing owner header.
        if(count_==writes_.size())std::terminate();
        write.source.owner=owner_;writes_[count_++]=write;
    }
    template<class Index> bool enter(const Index& index,std::uint64_t,access_intent)const {
        return &index==index_; // The outer operation already admitted the key.
    }
    bool prepare(std::span<const mutation_damage> index_writes) {
        for(auto d:index_writes)if(d.source.part!=owner_part::header)add(d);
        const auto writes=std::span<const mutation_damage>(writes_.data(),count_);
        // The input must survive index, metadata and payload stores. In
        // particular, publishing the new reference must not destroy its input.
        const auto input=reinterpret_cast<std::uintptr_t>(input_.data());
        for(const auto& d:writes){
            const auto dst=reinterpret_cast<std::uintptr_t>(d.source.base)+d.span.offset;
            if(!input_.empty()&&((input<=dst&&dst-input<input_.size())||
                (dst<input&&input-dst<d.span.size)))return false;
        }
        if(!context_.prepare(writes))return false;
        static_assert(noexcept(context_.before(writes_[0].source,writes_[0].span)));
        for(const auto& d:writes)context_.before(d.source,d.span);
        return true;
    }
    void before(mutation_source,ikea::byte_write)const noexcept {}
};
}
