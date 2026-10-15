#pragma once
#include "single_bucket.h"

namespace hashtable_spike::tiered {
// A logical projection over admitted physical TuplePack windows. The ninth byte
// is needed when a full word crosses a byte boundary; it is never overread as
// another word. Preserving stores admit their entire physical windows.
template<unsigned Bits,unsigned Phases> struct packed_field {
    static_assert(Bits<=64);
    static constexpr unsigned bytes=[] {
        unsigned n=0;for(unsigned p=0;p<8;++p)if(Phases&(1u<<p))n=std::max(n,(Bits+p+7)/8);return n;
    }();
    single::field<std::min(bytes,8u)*8> physical;
    [[no_unique_address]] single::field<(bytes>8?8:0)> extra;
    template<bool Direct> [[gnu::always_inline]] word get(const byte* data,unsigned bit)const {
        const auto* p=data+bit/8;const unsigned shift=bit%8;
        word value=physical.template get<Direct>(p)>>shift;
        if constexpr(bytes>8)if(Bits+shift>64)
            value|=extra.template get<Direct>(p+8)<<(64-shift);
        return value&mask(Bits);
    }
    template<bool Direct> [[gnu::always_inline]] void put(byte* data,unsigned bit,word value)const {
        auto* p=data+bit/8;
        if constexpr(Bits==bytes*8)physical.template put<Direct>(p,value);
        else {
            const unsigned shift=bit%8;const word changed=mask(Bits)<<shift;
            const auto before=physical.template get<Direct>(p);
            physical.template put<Direct>(p,(before&~changed)|((value&mask(Bits))<<shift));
            if constexpr(bytes>8)if(Bits+shift>64) {
                const auto tail_mask=mask(Bits+shift-64),before_tail=extra.template get<Direct>(p+8);
                extra.template put<Direct>(p+8,(before_tail&~tail_mask)|((value>>(64-shift))&tail_mask));
            }
        }
    }
};
template<unsigned Phases> struct packed_field<0,Phases> {
    static constexpr unsigned bytes=0;
    template<bool> word get(const byte*,unsigned)const{return 0;}
    template<bool> void put(byte*,unsigned,word)const{}
};

// Preparation is shared by all entries. Full-word projections share one
// description; a residual projection has a second only when needed.
template<unsigned Bits,unsigned Phases> struct value_binding {
    static constexpr unsigned bits=Bits,words=(Bits+63)/64;
    static constexpr unsigned tail_bits=Bits>64?Bits%64:0;
    static constexpr unsigned bytes=[] {
        unsigned n=0;for(unsigned p=0;p<8;++p)if(Phases&(1u<<p))n=std::max(n,(Bits+p+7)/8);return n;
    }();
    [[no_unique_address]] packed_field<std::min(Bits,64u),Phases> whole;
    [[no_unique_address]] packed_field<tail_bits,Phases> tail;
    template<unsigned I>static constexpr unsigned width=std::min(64u,Bits-64*I);
    template<unsigned I>const auto& projection()const {
        static_assert(I<words);
        if constexpr(tail_bits&&I+1==words)return tail;else return whole;
    }
};

// Immediate terminal consumer/recipe input. It borrows one stable entry and
// carries no ownership. Do not retain it past the enclosing operation. Logical
// word projections need not be byte-aligned in the bucket representation.
template<unsigned Bits,unsigned Phases,bool Direct,bool Mutable> struct value_access {
    static constexpr unsigned bits=Bits,words=(Bits+63)/64;
    const value_binding<Bits,Phases>& shared;
    std::conditional_t<Mutable,byte*,const byte*> data;
    unsigned bit;
    template<unsigned I=0>[[gnu::always_inline]] word read()const {
        if constexpr(Bits==0){static_assert(I==0);return 0;}
        else return shared.template projection<I>().template get<Direct>(data,bit+64*I);
    }
    template<unsigned I=0>[[gnu::always_inline]] void write(word value)const requires Mutable {
        if constexpr(Bits)shared.template projection<I>().template put<Direct>(data,bit+64*I,value);
    }
    [[gnu::always_inline]] void assign(word value)const requires (Mutable&&Bits<=64) {write(value);}
    template<class Source>[[gnu::always_inline]] void assign(const Source& source)const requires Mutable {
        static_assert(Source::bits==Bits);
        [&]<std::size_t... I>(std::index_sequence<I...>) {(write<I>(source.template read<I>()),...);}
            (std::make_index_sequence<words>{});
    }
};
} // namespace hashtable_spike::tiered
