#pragma once
#include "common.h"
#include <random>
#include <unordered_map>
namespace ht {
struct input {
    std::vector<word> keys,absent,queries;
};
template<unsigned K> input make_input(unsigned n,word seed) {
    require(word(n)*2<=mask(K),"domain population");
    input out; std::unordered_map<word,bool> seen; std::mt19937_64 random(seed);
    while(out.keys.size()<n) {auto k=random()&mask(K);if(seen.emplace(k,true).second)out.keys.push_back(k);}
    while(out.absent.size()<n) {auto k=random()&mask(K);if(seen.emplace(k,true).second)out.absent.push_back(k);}
    for(unsigned i=0;i<n;++i) {out.queries.push_back(out.keys[i]);out.queries.push_back(out.absent[i]);}
    std::shuffle(out.queries.begin(),out.queries.end(),random);return out;
}
} // namespace ht
