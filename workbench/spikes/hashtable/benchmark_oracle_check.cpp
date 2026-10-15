#include "benchmark_oracle.h"

int main() {
    using namespace ht;
    using value=std::array<byte,64>;
    value a{},b{};b.back()=1;
    benchmark_oracle<value> oracle({{7,{a,a,b}},{19,{b}}});
    require(!oracle.complete(),"missing values fail");
    require(!oracle.observe(8,a),"unknown key fails");
    auto wrong=b;wrong.front()=3;
    require(!oracle.observe(7,wrong),"wrong full bytes fail");
    require(oracle.observe(7,a)&&oracle.observe(7,a),"repeated identical values count");
    require(!oracle.observe(7,a),"extra multiplicity fails");
    require(oracle.observe(19,b)&&!oracle.complete(),"missing distinct value fails");
    require(oracle.observe(7,b)&&oracle.complete(),"complete multiset succeeds");
    oracle.check([](auto emit){value zero{},tail{};tail.back()=1;
        emit(19,tail);emit(7,zero);emit(7,tail);emit(7,zero);});
    oracle.check_key(8,[](auto){return false;});
    oracle.check_key(7,[&](auto emit){emit(b);emit(a);emit(a);return true;});
    benchmark_oracle<std::array<byte,0>> set({{0,{{}}},{255,{{}}}});
    require(set.observe(255,{})&&!set.complete()&&set.observe(0,{})&&set.complete(),"zero-byte membership");
    benchmark_oracle<word> words({{3,{9,11,9}}});
    words.check([](auto emit){emit(3,11);emit(3,9);emit(3,9);});
    std::puts("preallocated oracle: full bytes, duplicates, missing/extra keys and reset pass");
}
