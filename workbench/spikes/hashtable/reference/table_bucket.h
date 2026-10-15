#pragma once
#include "table_keys.h"

namespace hashtable_spike::tiered {
struct bit_field {
    unsigned offset,step,group_stride,group_slots;
    constexpr unsigned at(unsigned i)const{return offset+(i/group_slots)*group_stride+(i%group_slots)*step;}
    constexpr unsigned phases(unsigned n)const {
        unsigned out=0;for(unsigned i=0;i<n;++i)out|=1u<<(at(i)%8);return out;
    }
};
// A solver-selected physical bucket. Routing, allocation, lifetime and mutation
// admission belong to the table consumer; this body only interprets its bytes.
enum class key_search {selective,exact_encoded,exact_raw};
struct bucket_plan {
    unsigned key_bits, address_bits, fingerprint_bits, value_bits;
    unsigned slots, bytes, count_offset, count_bits;
    unsigned descendants_offset, descendants_bits, reference_offset, reference_bits;
    unsigned fingerprint_offset;
    bit_field suffix, value;
    unsigned suffix_byte_stride{};
    key_search search=key_search::selective;
    unsigned fingerprint_stride=1;
    constexpr unsigned remainder_bits() const {return key_bits-address_bits;}
    constexpr unsigned suffix_bits() const {return remainder_bits()-fingerprint_bits;}
    constexpr unsigned lanes()const{return (slots+7)/8*8;}
};
constexpr bucket_plan searching(bucket_plan p,key_search search) {
    p.search=search;return p;
}

template<bucket_plan P> struct bucket_binding {
    packed_field<P.count_bits,1u<<(P.count_offset%8)> count;
    [[no_unique_address]] packed_field<P.descendants_bits,1u<<(P.descendants_offset%8)> descendants;
    [[no_unique_address]] packed_field<P.reference_bits,1u<<(P.reference_offset%8)> reference;
    [[no_unique_address]] std::conditional_t<P.suffix_byte_stride==0,
        packed_field<P.suffix_bits(),P.suffix.phases(P.slots)>,no_planes> suffix;
    [[no_unique_address]] value_binding<P.value_bits,P.value.phases(P.slots)> value;
    [[no_unique_address]] std::conditional_t<P.suffix_byte_stride!=0,
        identity_plane<P.lanes()==8?8:16>,no_planes> plane;
    [[no_unique_address]] std::conditional_t<P.suffix_byte_stride!=0,single::field<8>,no_planes> fragment;
    [[no_unique_address]] std::conditional_t<P.fingerprint_stride==4,word_tile,no_planes> words;
    bucket_binding() {
        require(P.slots>=1&&P.slots<=32);
        require(P.fingerprint_bits<=8&&P.address_bits<P.key_bits);
        require(P.fingerprint_offset+(P.lanes()-1)*P.fingerprint_stride+1<=P.bytes);
        if constexpr(P.fingerprint_stride==4) {
            require(P.slots==16&&P.remainder_bits()==32&&P.suffix_byte_stride==0);
            require(P.fingerprint_bits==8&&P.suffix.offset%8==0);
            require(P.search==key_search::exact_raw&&P.suffix.offset/8+3==P.fingerprint_offset);
            require(P.suffix.step==32&&P.suffix.group_slots==16);
        }
        if constexpr(P.suffix_byte_stride) {
            require(P.lanes()==8||P.lanes()==16||P.lanes()==32);
            require(P.suffix_byte_stride==P.lanes()&&P.suffix.offset%8==0);
            require(P.suffix.offset/8+((P.suffix_bits()+7)/8)*P.suffix_byte_stride<=P.bytes);
        } else require(P.search==key_search::selective||P.fingerprint_stride==4);
        auto fits=[](unsigned bit,unsigned bytes) {return bit/8+bytes<=P.bytes;};
        require(fits(P.count_offset,count.bytes)&&fits(P.descendants_offset,descendants.bytes)&&
                fits(P.reference_offset,reference.bytes));
        for(unsigned i=0;i<P.slots;++i) {
            if constexpr(!P.suffix_byte_stride)require(fits(P.suffix.at(i),suffix.bytes));
            require(fits(P.value.at(i),value.bytes));
        }
    }
};

template<bucket_plan P,bool Direct=false> struct bucket {
    byte* data;
    const bucket_binding<P>& shared;
    unsigned size()const{return unsigned(shared.count.template get<Direct>(data,P.count_offset));}
    word descendants()const{return shared.descendants.template get<Direct>(data,P.descendants_offset);}
    word reference()const{return shared.reference.template get<Direct>(data,P.reference_offset);}
    void descendants(word n)const {shared.descendants.template put<Direct>(data,P.descendants_offset,n);}
    void reference(word n)const {shared.reference.template put<Direct>(data,P.reference_offset,n);}
    static byte fingerprint(word h){return byte((h>>P.suffix_bits())&mask(P.fingerprint_bits));}
    [[gnu::always_inline]] word suffix(unsigned i)const {
        if constexpr(!P.suffix_byte_stride)return shared.suffix.template get<Direct>(data,P.suffix.at(i));
        else {
            word value=0;
            [&]<std::size_t... B>(std::index_sequence<B...>) {
                ((value|=shared.fragment.template get<Direct>(data+P.suffix.offset/8+B*P.suffix_byte_stride+i)<<(8*B)),...);
            }(std::make_index_sequence<(P.suffix_bits()+7)/8>{});
            return value&mask(P.suffix_bits());
        }
    }
    [[gnu::always_inline]] void suffix(unsigned i,word value)const {
        if constexpr(!P.suffix_byte_stride)shared.suffix.template put<Direct>(data,P.suffix.at(i),value);
        else {
            // Every stored fragment is zero-extended. Unused high bits belong
            // to this physical field and are initialized on every replacement.
            value&=mask(P.suffix_bits());
            [&]<std::size_t... B>(std::index_sequence<B...>) {
                (shared.fragment.template put<Direct>(data+P.suffix.offset/8+B*P.suffix_byte_stride+i,byte(value>>(8*B))),...);
            }(std::make_index_sequence<(P.suffix_bits()+7)/8>{});
        }
    }
    [[gnu::always_inline]] int exact_find(word h)const {
        static_assert(P.suffix_byte_stride!=0);
        const auto fp=fingerprint(h);
        for(unsigned first=0;first<P.lanes();first+=16) {
            const auto tags=[&] {
                if constexpr(P.lanes()==8) {
#if defined(__aarch64__)
                    return vcombine_u8(vld1_u8(data+P.fingerprint_offset),vdup_n_u8(0));
#else
                    return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data+P.fingerprint_offset));
#endif
                } else return sp::native::read16<sp::format<8>,true>(data+P.fingerprint_offset+first,8,0).v[0];
            }();
#if defined(__aarch64__)
            auto equal=vceqq_u8(tags,vdupq_n_u8(fp));
#elif defined(__AVX512BW__) && defined(__AVX512VL__)
            unsigned equal=_mm_cmpeq_epi8_mask(tags,_mm_set1_epi8(char(fp)));
#else
            auto equal=_mm_cmpeq_epi8(tags,_mm_set1_epi8(char(fp)));
#endif
            const word wanted=h&mask(P.suffix_bits());
            [&]<std::size_t... B>(std::index_sequence<B...>) {
                auto compare=[&]<unsigned I>() {
                    const auto bytes=shared.plane.template get<Direct>(data+P.suffix.offset/8+I*P.suffix_byte_stride+first);
#if defined(__aarch64__)
                    equal=vandq_u8(equal,vceqq_u8(bytes,vdupq_n_u8(byte(wanted>>(8*I)))));
#elif defined(__AVX512BW__) && defined(__AVX512VL__)
                    equal&=_mm_cmpeq_epi8_mask(bytes,_mm_set1_epi8(char(wanted>>(8*I))));
#else
                    equal=_mm_and_si128(equal,_mm_cmpeq_epi8(bytes,_mm_set1_epi8(char(wanted>>(8*I)))));
#endif
                };
                (compare.template operator()<B>(),...);
            }(std::make_index_sequence<(P.suffix_bits()+7)/8>{});
#if defined(__aarch64__)
            word candidates=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0)&0x1111111111111111ULL;
            if constexpr(P.lanes()==8)candidates&=0x11111111;
            if(candidates) {
                const unsigned i=first+(std::countr_zero(candidates)>>2);
                return fp==255&&i>=size()?-1:int(i);
            }
#else
#if defined(__AVX512BW__) && defined(__AVX512VL__)
            unsigned candidates=equal;
#else
            unsigned candidates=unsigned(_mm_movemask_epi8(equal));
#endif
            if constexpr(P.lanes()==8)candidates&=255;
            if(candidates) {
                const unsigned i=first+std::countr_zero(candidates);
                return fp==255&&i>=size()?-1:int(i);
            }
#endif
        }
        return -1;
    }
    void initialize()const {
        std::memset(data,0,P.bytes);
        if constexpr(P.fingerprint_stride==4)for(unsigned i=0;i<P.slots;++i)tag(i,255);
        else std::memset(data+P.fingerprint_offset,255,P.lanes());
    }
    void clear()const {
        shared.count.template put<Direct>(data,P.count_offset,0);
        shared.descendants.template put<Direct>(data,P.descendants_offset,0);
        shared.reference.template put<Direct>(data,P.reference_offset,0);
        if constexpr(P.fingerprint_stride==4)for(unsigned i=0;i<P.slots;++i)tag(i,255);
        else std::memset(data+P.fingerprint_offset,255,P.lanes());
    }
    [[gnu::always_inline]] int find(word h)const {
        if constexpr(P.fingerprint_stride==4) {
            const int i=shared.words.template find<Direct>(data+P.suffix.offset/8,std::uint32_t(h));
            return i>=0&&fingerprint(h)==255&&unsigned(i)>=size()?-1:i;
        } else if constexpr(P.search!=key_search::selective)return exact_find(h);
        const byte fp=fingerprint(h);
        const auto* tags=data+P.fingerprint_offset;
#if defined(__aarch64__)
        // Preserve the native sparse mask. No per-query dense bit compaction.
        for(unsigned first=0;first<P.lanes();first+=16) {
            const auto lanes=[&] {
                if(P.lanes()-first==8)return vcombine_u8(vld1_u8(tags+first),vdup_n_u8(255));
                return sp::native::read16<sp::format<8>,true>(tags+first,8,0).v[0];
            }();
            const auto eq=vceqq_u8(lanes,vdupq_n_u8(fp));
            word candidates=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq),4)),0)&0x1111111111111111ULL;
            if(P.lanes()-first==8)candidates&=0x11111111;
            while(candidates) {
                const auto i=first+(std::countr_zero(candidates)>>2);
                if(fp==255&&i>=size())return -1;
                if(suffix(i)==(h&mask(P.suffix_bits())))return int(i);
                candidates&=candidates-1;
            }
        }
#else
        unsigned candidates;
        if constexpr(P.lanes()==32) {
            const auto a=sp::native::read16<sp::format<8>,true>(tags,8,0).v[0];
            const auto b=sp::native::read16<sp::format<8>,true>(tags+16,8,0).v[0];
            const auto lanes=_mm256_set_m128i(b,a);
#if defined(__AVX512BW__) && defined(__AVX512VL__)
            candidates=_mm256_cmpeq_epi8_mask(lanes,_mm256_set1_epi8(fp));
#else
            candidates=unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(lanes,_mm256_set1_epi8(fp))));
#endif
        } else {
            const auto lanes=[&] {
                if constexpr(P.lanes()==8)return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(tags));
                else return sp::native::read16<sp::format<8>,true>(tags,8,0).v[0];
            }();
#if defined(__AVX512BW__) && defined(__AVX512VL__)
            candidates=_mm_cmpeq_epi8_mask(lanes,_mm_set1_epi8(fp));
#else
            candidates=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(lanes,_mm_set1_epi8(fp))));
#endif
            if constexpr(P.lanes()==8)candidates&=255;
            if constexpr(P.lanes()==24) {
                const auto tail=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(tags+16));
                const unsigned more=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(tail,_mm_set1_epi8(fp))))&255;
                candidates|=more<<16;
            }
        }
        if(fp==255)candidates&=unsigned(mask(size()));
        while(candidates) {
            const auto i=std::countr_zero(candidates);
            if(suffix(i)==(h&mask(P.suffix_bits())))return int(i);
            candidates&=candidates-1;
        }
#endif
        return -1;
    }
    auto read_value(unsigned i)const {
        return value_access<P.value_bits,P.value.phases(P.slots),Direct,false>{shared.value,data,P.value.at(i)};
    }
    auto write_value(unsigned i)const {
        return value_access<P.value_bits,P.value.phases(P.slots),Direct,true>{shared.value,data,P.value.at(i)};
    }
    word value(unsigned i)const requires(P.value_bits<=64){return read_value(i).read();}
    void value(unsigned i,word v)const requires(P.value_bits<=64){write_value(i).assign(v);}
    void tag(unsigned i,byte fp)const {
        if constexpr(P.fingerprint_stride==4)word_tags::put(data+P.suffix.offset/8,i,fp);
        else sp::detail::put_payload<sp::format<8>>(data+P.fingerprint_offset+(i/8)*8,i%8,fp);
    }
    byte tag(unsigned i)const {
        if constexpr(P.fingerprint_stride==4)return word_tags::get(data+P.suffix.offset/8,i);
        else return data[P.fingerprint_offset+i];
    }
    // Enclosing owner has admitted all affected bytes and capacity before entry.
    template<class Recipe>void append_with(word h,Recipe&& recipe)const {
        const auto i=size();require(i<P.slots);
        tag(i,fingerprint(h));suffix(i,h&mask(P.suffix_bits()));
        recipe(write_value(i));shared.count.template put<Direct>(data,P.count_offset,i+1);
    }
    void append(word h,word v)const requires(P.value_bits<=64){append_with(h,[&](auto at){at.assign(v);});}
    void erase(unsigned i)const {
        const auto last=size()-1;require(i<=last);
        if(i!=last) {
            tag(i,tag(last));
            suffix(i,suffix(last));
            write_value(i).assign(read_value(last));
        }
        tag(last,255);shared.count.template put<Direct>(data,P.count_offset,last);
    }
    word hash(unsigned bucket_index,unsigned i)const {
        const word high=[&]{if constexpr(P.address_bits==0)return word{0};else return word(bucket_index)<<P.remainder_bits();}();
        return high|
            (word(tag(i))<<P.suffix_bits())|
            suffix(i);
    }
};
} // namespace hashtable_spike::tiered
