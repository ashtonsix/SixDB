#pragma once
#include "../effects.h"
namespace ikea::hashtable {
// The frontier's compound owner qualifies all leaf effects before its first
// store. Retirement remains a distinct owner operation; this is a write list.
template<class Context> class compound_damage {
    Context& context_;
    const void* owner_;
    const void* index_;
    ikea::bytes (*resolve_)(const void*,const void*,unsigned);
    ikea::bytes input_;
    std::array<ikea::journal::entry,12> writes_{};
    unsigned count_=0;
  public:
    template<class Owner,class Index> compound_damage(Context& context,const Owner& owner,
            const Index& index,ikea::bytes input={}):context_(context),owner_(&owner),index_(&index),
        resolve_(+[](const void* material,const void* source,unsigned plane){
            return static_cast<const Owner*>(material)->effect_storage(source,plane);
        }),input_(input) {
        add({owner_,unsigned(plane::header),0,sizeof(owner)});
    }
    void add(ikea::journal::entry write) {
        if(count_==writes_.size())std::terminate();
        writes_[count_++]=write;
    }
    template<class Index> bool enter(const Index& index,std::uint64_t,access_intent)const {
        return &index==index_;
    }
    bool prepare(std::span<const ikea::journal::entry> index_writes) {
        for(auto d:index_writes)if(d.plane!=unsigned(plane::header))add(d);
        const auto writes=std::span<const ikea::journal::entry>(writes_.data(),count_);
        for(const auto& d:writes){
            const auto storage=resolve_(owner_,d.source,d.plane);
            if(d.offset>storage.size()||d.size>storage.size()-d.offset)return false;
            if(d.size&&input_overlaps(input_,storage.data()+d.offset,d.size))return false;
        }
        if(!context_.prepare(writes))return false;
        static_assert(noexcept(context_.before(writes_[0].source,writes_[0].plane,writes_[0].offset,writes_[0].size)));
        for(const auto& d:writes)context_.before(d.source,d.plane,d.offset,d.size);
        return true;
    }
    template<class Index> void before(const Index&,unsigned,std::size_t,std::size_t)const noexcept {}
};
}
