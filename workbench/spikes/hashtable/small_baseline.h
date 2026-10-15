#pragma once
#include "list.h"
#include "baseline.h"
#include "reference/packed_key_baseline.h"
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>

namespace ht {
// Key-table allocations are counted without changing slot size. Keep the
// ordinary stateless-allocator bag: embedding a counter pointer in every bag
// would inflate a baseline slot by eight bytes. Nested requested capacity is
// inventoried outside timing; no exact nested-allocation peak is claimed.
template<unsigned K,container_kind Kind,bool Exact=false> struct small_abseil {
    using key_type=std::conditional_t<Exact,hashtable_spike::exact_unsigned_key<K/8>,typename abseil<K>::key_type>;
    template<class T>using alloc=hashtable_spike::counting_allocator<T>;
    using bag=absl::InlinedVector<word,1>;
    using value_type=std::conditional_t<Kind==container_kind::multimap,bag,word>;
    using ordinary_map=absl::flat_hash_map<key_type,value_type>;
    using ordinary_set=absl::flat_hash_set<key_type>;
    using map=absl::flat_hash_map<key_type,value_type,typename ordinary_map::hasher,
        typename ordinary_map::key_equal,alloc<std::pair<const key_type,value_type>>>;
    using set=absl::flat_hash_set<key_type,typename ordinary_set::hasher,
        typename ordinary_set::key_equal,alloc<key_type>>;
    using storage_type=std::conditional_t<Kind==container_kind::set,set,map>;
    using ordinary=std::conditional_t<Kind==container_kind::set,ordinary_set,ordinary_map>;
    hashtable_spike::allocation_counters counters;
    storage_type storage;
    explicit small_abseil(oa::Pool&,word):storage(0,typename storage_type::hasher{},
        typename storage_type::key_equal{},typename storage_type::allocator_type(counters)) {}
    // Abseil's native copy with fresh accounting owned by the destination. The
    // ordinary container copy selects the source allocator, whose counter would
    // otherwise dangle when a retained source version is reclaimed.
    explicit small_abseil(const small_abseil& other)
        :storage(other.storage,typename storage_type::allocator_type(counters)) {}
    small_abseil& operator=(const small_abseil&)=delete;
    [[gnu::always_inline]] mutation_delta set_value(word key,word value) {
        if constexpr(Kind==container_kind::set) {
            std::pair<typename storage_type::iterator,bool> r;
            [[clang::always_inline]] r=storage.insert(key_type(key));return {r.second,0};
        } else if constexpr(Kind==container_kind::map) {
            std::pair<typename storage_type::iterator,bool> r;
            [[clang::always_inline]] r=storage.insert_or_assign(key_type(key),value);return {r.second,r.second};
        } else {
            std::pair<typename storage_type::iterator,bool> r;
            [[clang::always_inline]] r=storage.try_emplace(key_type(key));
            r.first->second.push_back(value);return {r.second,1};
        }
    }
    template<class Consume> [[gnu::always_inline]] bool get(word key,Consume&& consume)const {
        typename storage_type::iterator it;
        [[clang::always_inline]] it=const_cast<storage_type&>(storage).find(key_type(key));
        if(it==storage.end())return false;
        if constexpr(Kind==container_kind::set)consume(word(0));
        else if constexpr(Kind==container_kind::map)consume(it->second);
        else for(auto value:it->second)consume(value);
        return true;
    }
    mutation_delta erase(word key) {
        auto it=storage.find(key_type(key));if(it==storage.end())return {};
        const auto values=[&]()->std::int64_t {
            if constexpr(Kind==container_kind::set)return 0;
            else if constexpr(Kind==container_kind::map)return 1;
            else return it->second.size();
        }();storage.erase(it);return {-1,-values};
    }
    template<class Visit> void each(Visit&& visit)const {
        auto key=[](const auto& k)->word {if constexpr(Exact)return k.value();else return k;};
        if constexpr(Kind==container_kind::set)for(auto k:storage)visit(key(k),0);
        else for(const auto& [k,v]:storage) {
            if constexpr(Kind==container_kind::map)visit(key(k),v);
            else for(auto item:v)visit(key(k),item);
        }
    }
    template<unsigned N> unsigned setN(std::span<const word,N> keys,std::span<const word,N> values,unsigned active) {
        unsigned processed=0;lanes<N>([&](auto i){if(active&(1u<<i)){set_value(keys[i],values[i]);processed|=1u<<i;}});return processed;
    }
    template<unsigned N,class Consume> unsigned getN(std::span<const word,N> keys,unsigned active,Consume&& consume)const {
        unsigned found=0;lanes<N>([&](auto i){if((active&(1u<<i))&&get(keys[i],[&](word v){consume(i,v);}))found|=1u<<i;});return found;
    }
    void clear() {storage.clear();}
    memory footprint()const {
        word bytes=counters.current_bytes;
        if constexpr(Kind==container_kind::multimap)for(const auto& [key,values]:storage) {
            const auto address=reinterpret_cast<std::uintptr_t>(values.data());
            const auto object=reinterpret_cast<std::uintptr_t>(&values);
            if(address<object||address>=object+sizeof(values))bytes+=values.capacity()*sizeof(word);
        }
        return {bytes,0,sizeof(ordinary)};
    }
};
} // namespace ht
