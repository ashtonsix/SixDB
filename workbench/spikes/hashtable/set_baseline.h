#pragma once
#include "baseline.h"
#include <absl/container/flat_hash_set.h>
namespace ht {
template<unsigned K> struct abseil_set {
    using key_type=typename abseil<K>::key_type;
    using ordinary_set=absl::flat_hash_set<key_type>;
    using allocator=hashtable_spike::counting_allocator<key_type>;
    using set_type=absl::flat_hash_set<key_type,typename ordinary_set::hasher,
                                    typename ordinary_set::key_equal,allocator>;
    hashtable_spike::allocation_counters counters;
    set_type set;
    explicit abseil_set(oa::Pool&,word,word):set(0,typename ordinary_set::hasher{},
                                               typename ordinary_set::key_equal{},allocator(counters)) {}
    void reserve(std::size_t n) {set.reserve(n);}
    [[gnu::always_inline]] bool contains(word key)const {
        typename set_type::iterator found;
        [[clang::always_inline]] found=const_cast<set_type&>(set).find(static_cast<key_type>(key));
        return found!=set.end();
    }
    [[gnu::always_inline]] int insert(word key) {
        std::pair<typename set_type::iterator,bool> result;
        [[clang::always_inline]] result=set.insert(static_cast<key_type>(key));return result.second;
    }
    [[gnu::always_inline]] bool erase(word key) {return set.erase(static_cast<key_type>(key))!=0;}
    template<class Visit> void each(Visit&& visit)const {for(auto k:set)visit(k);}
    void validate()const {}
    memory footprint()const {return {counters.current_bytes,0,sizeof(ordinary_set)};}
};
template<unsigned K,unsigned A,unsigned B,unsigned P,unsigned Q>
struct packed_set {
    using map_type=table<K,A,B,P,Q,false,false,0>;
    map_type map;
    explicit packed_set(oa::Pool& p,word a,word b):map(p,a,b) {}
    [[gnu::always_inline]] bool contains(word k)const {word unused=0;return map.get_value(k,unused);}
    [[gnu::always_inline]] int insert(word k) {return map.set(k,0);}
    [[gnu::always_inline]] bool erase(word k) {return map.erase(k);}
    template<class Visit> void each(Visit&& visit)const {map.each([&](word k,word v){require(v==0,"set has no value");visit(k);});}
    void validate()const {map.validate();}
    memory footprint()const {return map.footprint();}
};
} // namespace ht
