#pragma once
#include "table.h"
#include "baseline.h"
#include "input.h"
#include <unordered_map>
#include <string_view>

namespace ht {
struct record {word key,value;};
struct trace {
    std::vector<word> keys;
    std::vector<record> rows;
    std::unordered_map<word,word> expected;
    trace(unsigned n,unsigned repeats,bool skew):keys(make_input<40>(n,638251).keys) {
        rows.reserve(std::size_t(n)*repeats);std::mt19937_64 random(94321);
        for(word i=0;i<word(n)*repeats;++i) {
            // Cover every key once, then include skewed or uniform repetitions.
            auto at=i<n?i:random()%((skew&&random()%10<9)?std::max(1u,n/100):n);
            auto value=i%17==0?~word(0):i%19==0?0:random();
            rows.push_back({keys[at],value});expected[keys[at]]+=value;
        }
        std::shuffle(rows.begin(),rows.end(),random);
    }
};
struct add {[[gnu::always_inline]] word operator()(word old,word incoming)const noexcept {return old+incoming;}};
// One visible complete consumer per arm. The shared translation-unit inlining
// budget and final binary guard qualify Abseil's private insertion search too.
template<bool TwoSearch=false,class Map> [[gnu::noinline]] word accumulate(Map& map,std::span<const record> input) {
    word distinct=0;
    for(auto r:input) {
        int delta;
        if constexpr(TwoSearch) {
            word old=0;map.get_value(r.key,old);delta=map.set(r.key,old+r.value);
        } else delta=map.upsert(r.key,r.value,add{});
        require(delta>=0,"accumulation admission");distinct+=delta;
    }
    return distinct;
}
template<class Map> [[gnu::noinline]] word consume_values(const Map& map,std::span<const word> keys) {
    word sum=0;
    for(auto k:keys) {word v=0;require(map.get_value(k,v),"result lookup");sum+=v;}
    asm volatile("" :: "r"(sum) : "memory");return sum;
}
} // namespace ht
