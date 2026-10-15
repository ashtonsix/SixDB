#pragma once
#include "fields.h"

namespace ht {
// The index spends a byte budget, independently of the element width. Values
// wider than that budget start external. Empty keys are represented by index
// occupancy, not a bag state. Preserve old tags: 0=one, 1=external, n>=2=count.
template<unsigned ValueBytes,unsigned PayloadBytes=8> struct inline_bag {
    static_assert(ValueBytes>=1 && ValueBytes<=64);
    // Eight bytes carry an external head/hint. The discriminator is separate
    // from the value budget, including a full 64-byte inline singleton.
    static_assert(PayloadBytes>=8 && PayloadBytes<=64 && PayloadBytes%8==0);
    static constexpr unsigned capacity=PayloadBytes/ValueBytes;
    static constexpr unsigned payload_bytes=PayloadBytes;
    // These padded discriminator widths divide a byte. No packed store crosses
    // its containing byte; the solver can keep the same field/window lowering.
    static constexpr unsigned tag_bits=std::bit_ceil(std::max(1u,unsigned(std::bit_width(capacity))));
    template<class Record> static bool external(const Record& r) {return r.tag==1;}
    template<class Record> static unsigned count(const Record& r) {return r.tag<2?1:r.tag;}
    template<class Record,class Visit> [[gnu::always_inline]] static void each_bytes(const Record& r,Visit&& visit) {
        if constexpr(capacity) {
            visit(std::span<const byte,ValueBytes>(r.payload.data(),ValueBytes));
            if constexpr(capacity>1)for(unsigned n=1;n<count(r);++n)
                visit(std::span<const byte,ValueBytes>(r.payload.data()+n*ValueBytes,ValueBytes));
        }
    }
    template<class Record> static bool append(Record& r,std::span<const byte,ValueBytes> value) {
        if constexpr(capacity>1)if(!external(r)) {
            const auto n=count(r);if(n<capacity) {
                std::memcpy(r.payload.data()+n*ValueBytes,value.data(),ValueBytes);r.tag=n+1;return true;
            }
        }
        return false;
    }
};
} // namespace ht
