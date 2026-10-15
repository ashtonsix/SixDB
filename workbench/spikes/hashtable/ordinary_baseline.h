#pragma once
#include "small_baseline.h"
#include <absl/container/internal/hashtable_debug.h>
namespace ht {
// Ordinary stateless-allocator container, including ordinary inline-vector bags.
// Inventory is outside timing; backing/RSS and rehash peaks are not inferred.
template<unsigned K,container_kind Kind,bool Exact> struct ordinary_abseil {
    using reference=small_abseil<K,Kind,Exact>;
    using key_type=typename reference::key_type;
    typename reference::ordinary storage;
    ordinary_abseil()=default;
    explicit ordinary_abseil(oa::Pool&,word) {}
    [[gnu::always_inline]] void set_value(word k,word v) {
        if constexpr(Kind==container_kind::set) {[[clang::always_inline]] storage.insert(key_type(k));}
        else if constexpr(Kind==container_kind::map) {[[clang::always_inline]] storage.insert_or_assign(key_type(k),v);}
        else {
            std::pair<typename reference::ordinary::iterator,bool> result;
            [[clang::always_inline]] result=storage.try_emplace(key_type(k));result.first->second.push_back(v);
        }
    }
    template<class Consume> [[gnu::always_inline]] bool get(word key,Consume&& consume)const {
        typename reference::ordinary::iterator at;
        [[clang::always_inline]] at=const_cast<typename reference::ordinary&>(storage).find(key_type(key));
        if(at==storage.end())return false;
        if constexpr(Kind==container_kind::set)consume(word(0));
        else if constexpr(Kind==container_kind::map)consume(at->second);
        else for(auto v:at->second)consume(v);
        return true;
    }
    template<class Visit> void each(Visit&& visit)const {
        auto key=[](const auto& k)->word {if constexpr(Exact)return k.value();else return k;};
        if constexpr(Kind==container_kind::set)for(auto k:storage)visit(key(k),word(0));
        else for(const auto& [k,value]:storage) {
            if constexpr(Kind==container_kind::map)visit(key(k),value);
            else for(auto v:value)visit(key(k),v);
        }
    }
    template<unsigned N> unsigned setN(std::span<const word,N> keys,
            std::span<const word,N> values,unsigned active) {
        unsigned processed=0;
        lanes<N>([&](auto i){if(active&(1u<<i)) {
            set_value(keys[i],values[i]);processed|=1u<<i;
        }});
        return processed;
    }
    template<unsigned N,class Consume> unsigned getN(std::span<const word,N> keys,
            unsigned active,Consume&& consume)const {
        unsigned found=0;
        lanes<N>([&](auto i){if((active&(1u<<i))&&
                get(keys[i],[&](word value){consume(i,value);}))found|=1u<<i;});
        return found;
    }
    [[gnu::always_inline]] void erase(word k) {
        typename reference::ordinary::iterator at;
        [[clang::always_inline]] at=storage.find(key_type(k));
        if(at!=storage.end())storage.erase(at);
    }
    void reserve(unsigned n) { storage.reserve(n); }
    memory footprint()const {
        word bytes=absl::container_internal::AllocatedByteSize(storage);
        if constexpr(Kind==container_kind::multimap)for(const auto& [k,values]:storage) {
            const auto data=reinterpret_cast<std::uintptr_t>(values.data());
            const auto object=reinterpret_cast<std::uintptr_t>(&values);
            if(data<object||data>=object+sizeof(values))bytes+=values.capacity()*sizeof(word);
        }
        return {bytes,0,sizeof(*this)};
    }
};

} // namespace ht
