#include "mask_probe.h"
using namespace source_probe;
#define PROBE_WIDTH(W) \
extern "C" word split_##W(word x) { return explicit_split<W>(x); } \
extern "C" word pair_##W(word x) { return explicit_pair<W>(x); } \
extern "C" word cpp_##W(word x) { return ordinary<W>(x); }
PROBE_WIDTH(0) PROBE_WIDTH(1) PROBE_WIDTH(8) PROBE_WIDTH(16) PROBE_WIDTH(24)
PROBE_WIDTH(32) PROBE_WIDTH(33) PROBE_WIDTH(40) PROBE_WIDTH(48)
PROBE_WIDTH(56) PROBE_WIDTH(63) PROBE_WIDTH(64) PROBE_WIDTH(65)
PROBE_WIDTH(256) PROBE_WIDTH(4294967295u)
extern "C" word dynamic_width(word x,unsigned width) {return runtime_width(x,width);}
extern "C" word constant_value() {return explicit_split<40>(0x123456789abcdef0ULL);}
extern "C" word bounded_result(word x) {return explicit_split<40>(x) >> 40;}
extern "C" word hint_40(word x) {return count_hint<40>(x);}
#define PRODUCER(NAME,F) \
extern "C" word NAME(unsigned i, word seed) { \
    word key=word(i)*0xd6e8feb86659fd93ULL & width_mask(40); \
    return F<40>(key ^ seed); \
}
PRODUCER(cpp_producer,ordinary)
PRODUCER(split_producer,explicit_split)
PRODUCER(pair_producer,explicit_pair)
PRODUCER(hint_producer,count_hint)
// A data-consuming loop with runtime seed makes producer-folding costs visible.
// This is codegen evidence, not a serial-dependency or throughput benchmark.
#define LOOP(NAME,F) \
extern "C" word NAME(const word* data,unsigned n,word seed) { \
    word sum=0; for(unsigned i=0;i<n;++i) { \
        word key=word(i)*0xd6e8feb86659fd93ULL & width_mask(40); \
        word x=F<40>(key ^ seed); \
        sum+=data[x & 255] ^ (x >> 10); \
    } return sum; \
}
LOOP(cpp_loop,ordinary)
LOOP(split_loop,explicit_split)
LOOP(pair_loop,explicit_pair)
LOOP(hint_loop,count_hint)
