#pragma once
#include "common.h"
namespace ht {
// Executable initial solver grammar: byte selector tiles and byte-aligned
// suffix/value planes, dense slots, primary descendant count, secondary head.
// Unit size is a candidate input; selected slots follow complete physical cost.
struct recipe {
    unsigned unit,slots,header,fp,suffix,values,suffix_bytes,value_bytes;
    constexpr bool operator==(const recipe&)const=default;
};
constexpr recipe solve(unsigned suffix_bits,unsigned value_bytes,unsigned unit,bool secondary) {
    unsigned header=secondary?9:5, sb=(suffix_bits+7)/8;
    recipe best{};
    for(unsigned n=1;n<=32;++n) {
        unsigned tags=((n+7)/8)*8;
        unsigned values=header+tags+n*sb;
        if(values+n*value_bytes<=unit)
            best={unit,n,header,header,header+tags,values,sb,value_bytes};
    }
    return best;
}
} // namespace ht
