#include "codec.h"
#include <algorithm>

namespace ikea::integers {
// Independent bit-at-a-time implementation: no production bit map or native
// operations. Widths 3 and 6 retain independently stated fields. The other
// widths allocate values sequentially, carrying low bits into the next byte.
static BitAddress bit(Layout l,unsigned k,unsigned i,unsigned b) {
    if(l==Layout::local) return {i/8*k+b,i%8};
    const unsigned groups=8/std::gcd(k,8u), stripes=k/std::gcd(k,8u);
    const unsigned g=i/32%groups;
    unsigned position=0;
    if(k==3) {
        constexpr unsigned map[8][3]={{0,1,2},{3,4,5},{6,7,14},{8,9,10},{11,12,13},{22,23,15},{16,17,18},{19,20,21}};
        position=map[g][b];
    } else if(k==6) {
        constexpr unsigned map[4][6]={{0,1,2,3,4,5},{8,9,10,11,6,7},{12,13,14,15,22,23},{16,17,18,19,20,21}};
        position=map[g][b];
    } else {
        unsigned byte=0,used=0;
        for(unsigned value=0;value<=g;++value) {
            const unsigned available=8-used;
            if(value==g) {
                if(k<=available) position=byte*8+used+b;
                else if(b<k-available) position=(byte+1)*8+b;
                else position=byte*8+used+b-(k-available);
            }
            used+=k; byte+=used/8; used%=8;
        }
    }
    return {(i/(32*groups))*32*stripes+32*(position/8)+i%32,position%8};
}
void oracle_encode(Layout l,unsigned k,const uint8_t* in,uint8_t* out) {
    std::fill_n(out,32*k,0);
    for(unsigned i=0;i<256;++i) for(unsigned b=0;b<k;++b) {
        const auto a=bit(l,k,i,b); out[a.byte]|=((in[i]>>b)&1)<<a.bit;
    }
}
uint8_t oracle_get(Layout l,unsigned k,const uint8_t* p,unsigned i) {
    unsigned value=0;
    for(unsigned b=0;b<k;++b) {const auto a=bit(l,k,i,b); value|=((p[a.byte]>>a.bit)&1)<<b;}
    return value;
}
} // namespace ikea::integers
