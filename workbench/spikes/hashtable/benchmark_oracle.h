#pragma once
#include "common.h"
#include <map>
#include <vector>

namespace ht {
// Benchmark-only full-value/multiplicity oracle. Construct before the measured
// owner exists and retain until after it dies. Validation does not allocate or
// free: even untimed malloc traffic between phases can change measured release
// and the allocator state seen by the next history.
template<class Value> class benchmark_oracle {
    struct bag {
        std::vector<Value> values;
        std::vector<unsigned> required,seen;
    };
    std::map<word,bag> bags;
public:
    explicit benchmark_oracle(const std::map<word,std::vector<Value>>& expected) {
        for(const auto& [key,values]:expected) {
            auto sorted=values;std::sort(sorted.begin(),sorted.end());
            auto& b=bags[key];
            for(const auto& value:sorted) {
                if(b.values.empty()||b.values.back()!=value) {
                    b.values.push_back(value);b.required.push_back(1);b.seen.push_back(0);
                }else ++b.required.back();
            }
        }
    }
    void reset() {for(auto& [key,b]:bags)std::fill(b.seen.begin(),b.seen.end(),0);}
    bool observe(word key,const Value& value) {
        const auto at=bags.find(key);if(at==bags.end())return false;
        auto& b=at->second;const auto v=std::lower_bound(b.values.begin(),b.values.end(),value);
        if(v==b.values.end()||*v!=value)return false;
        const auto i=std::size_t(v-b.values.begin());
        if(b.seen[i]==b.required[i])return false;
        ++b.seen[i];return true;
    }
    bool complete()const {
        for(const auto& [key,b]:bags)if(b.seen!=b.required)return false;
        return true;
    }
    template<class Emit> void check(Emit&& emit) {
        reset();emit([&](word key,const Value& value) {
            require(observe(key,value),"independent full-value/multiplicity oracle");
        });require(complete(),"every independently expected value observed");
    }
    template<class Lookup> void check_key(word key,Lookup&& lookup) {
        const auto at=bags.find(key);
        if(at!=bags.end())std::fill(at->second.seen.begin(),at->second.seen.end(),0);
        const bool found=lookup([&](const Value& value) {
            require(observe(key,value),"point full-value/multiplicity oracle");
        });
        require(found==(at!=bags.end()),"independent point membership");
        if(found)require(at->second.seen==at->second.required,"complete point bag");
    }
    template<class Table> void check_bytes(const Table& table) {
        check([&](auto&& emit){table.each_bytes([&](word key,auto bytes) {
            Value value;std::copy(bytes.begin(),bytes.end(),value.begin());emit(key,value);
        });});
    }
};
} // namespace ht
