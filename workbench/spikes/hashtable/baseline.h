#pragma once
#include "common.h"
#include "reference/baseline.h" // pinned dependency and allocator counters only
namespace ht {
template<unsigned K> struct abseil {
    using key_type=std::conditional_t<(K<=8),std::uint8_t,
                   std::conditional_t<(K<=16),std::uint16_t,
                   std::conditional_t<(K<=32),std::uint32_t,std::uint64_t>>>;
    using ordinary_map=absl::flat_hash_map<key_type,word>;
    using allocator=hashtable_spike::counting_allocator<std::pair<const key_type,word>>;
    using map_type=absl::flat_hash_map<key_type,word,typename ordinary_map::hasher,
                                    typename ordinary_map::key_equal,allocator>;
    // The ordinary implementation is unchanged. Both contenders request fused
    // point lookup; a wrapper attribute alone left Abseil find outlined on x86.
    hashtable_spike::allocation_counters counters;
    map_type map;
    explicit abseil(oa::Pool&,word,word):map(0,typename ordinary_map::hasher{},
                                          typename ordinary_map::key_equal{},allocator(counters)) {}
    void reserve(std::size_t n) {map.reserve(n);}
    [[gnu::always_inline]] bool get_value(word key,word& value)const {
        typename map_type::iterator found;
        // Abseil's const overload forwards through this same const_cast to the
        // nonconst lookup. Attribute that actual lookup, not just its wrapper.
        [[clang::always_inline]] found=const_cast<map_type&>(map).find(static_cast<key_type>(key));
        if(found==map.end()) return false;
        value=found->second;return true;
    }
    int set(word key,word value) {return map.insert_or_assign(static_cast<key_type>(key),value).second;}
    template<class Combine> [[gnu::always_inline]] int upsert(word key,word value,Combine&& combine) {
        static_assert(noexcept(combine(word{},word{})));
        std::pair<typename map_type::iterator,bool> result;
        [[clang::always_inline]] result=map.try_emplace(static_cast<key_type>(key),value);
        if(!result.second) {
            [[clang::always_inline]] result.first->second=combine(result.first->second,value);
        }
        return result.second;
    }
    bool erase(word key) {return map.erase(static_cast<key_type>(key))!=0;}
    template<class Visit> void each(Visit&& visit)const {for(auto [k,v]:map)visit(k,v);}
    const abseil& bind_read()const {return *this;}
    template<unsigned N> [[gnu::always_inline]] unsigned getN(const word* keys,word* values)const {
        unsigned found=0;
        lanes<N>([&](auto i) {if(get_value(keys[i],values[i])) found|=1u<<i;});
        return found;
    }
    [[gnu::always_inline]] void prefetch(word key)const {
        [[clang::always_inline]] map.prefetch(static_cast<key_type>(key));
    }
    void validate()const {}
    memory footprint()const {return {counters.current_bytes,0,sizeof(ordinary_map)};}
};
} // namespace ht
