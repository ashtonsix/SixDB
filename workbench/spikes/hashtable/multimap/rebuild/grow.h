#pragma once
#include <ikea/hashtable/author/table.h>

namespace write_arch::rebuild {
namespace ht=ikea::hashtable;
using byte=std::byte;
using ht::word;
using access=ht::author::table_access;

template<class T> auto& reader_owner(T& owner) {
    if constexpr(requires{typename T::reader_owner;}) {
        using Base=typename T::reader_owner;
        if constexpr(std::is_const_v<T>)return static_cast<const Base&>(owner);
        else return static_cast<Base&>(owner);
    }else return owner;
}

// Local SeriesPack stores the high E/8 bytes per row followed by E%8 low
// bit planes. This transforms a complete tile without unpacking/repacking its
// eight keys. All excluded rows become the destination's all-ones sentinel.
template<unsigned K,unsigned E> [[gnu::always_inline]] void numeric_split_tile(
        const std::uint8_t* __restrict__ source,std::uint8_t live,std::uint8_t* __restrict__ low,
        std::uint8_t* __restrict__ high,std::uint8_t& low_empty,std::uint8_t& high_empty) {
    static_assert(K>9&&E>=K);
    constexpr unsigned body=E/8,tail=E%8,split=K-1;
#if defined(__aarch64__)
    if constexpr((K==14&&E==14)||(K==30&&E==30)){
        const std::uint8_t weights[8]={1,2,4,8,16,32,64,128};
        const auto live_bytes=vtst_u8(vdup_n_u8(live),vld1_u8(weights));
        auto source_bodies=[&]{if constexpr(body==1){uint8x8x3_t x{};x.val[0]=vld1_u8(source);return x;}
            else return vld3_u8(source);}();
        const auto upper=vtst_u8(source_bodies.val[body-1],vdup_n_u8(128));
        const auto dead_low=vmvn_u8(vbic_u8(live_bytes,upper));
        const auto dead_high=vmvn_u8(vand_u8(live_bytes,upper));
        low_empty=std::uint8_t(ikea::detail::filter::byte_bits(vcombine_u8(dead_low,vdup_n_u8(0))));
        high_empty=std::uint8_t(ikea::detail::filter::byte_bits(vcombine_u8(dead_high,vdup_n_u8(0))));
        source_bodies.val[body-1]=vand_u8(source_bodies.val[body-1],vdup_n_u8(127));
        uint8x8x3_t a{},b{};
        ht::each<body>([&](auto limb) __attribute__((always_inline)){
            a.val[limb]=vorr_u8(source_bodies.val[limb],dead_low);
            b.val[limb]=vorr_u8(source_bodies.val[limb],dead_high);});
        if constexpr(body==1){vst1_u8(low,a.val[0]);vst1_u8(high,b.val[0]);}
        else {vst3_u8(low,a);vst3_u8(high,b);}
    }else
#elif defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__BMI2__)
    if constexpr(K==30&&E==30){
        const auto bytes=_mm256_maskz_loadu_epi8(0xffffffu,source);
        const unsigned upper=_pext_u32(unsigned(_mm256_movemask_epi8(bytes)),0x924924u);
        const unsigned a=live&~upper,b=live&upper;
        low_empty=std::uint8_t(~a);high_empty=std::uint8_t(~b);
        const auto reduced=_mm256_mask_and_epi32(bytes,0xff,bytes,
            _mm256_setr_epi8(-1,-1,127,-1,-1,127,-1,-1,127,-1,-1,127,-1,-1,127,-1,
                            -1,127,-1,-1,127,-1,-1,127,0,0,0,0,0,0,0,0));
        const auto all=_mm256_set1_epi8(-1);
        _mm256_mask_storeu_epi8(low,0xffffffu,_mm256_mask_mov_epi8(all,_pdep_u32(a,0x249249u)*7,reduced));
        _mm256_mask_storeu_epi8(high,0xffffffu,_mm256_mask_mov_epi8(all,_pdep_u32(b,0x249249u)*7,reduced));
    }else
#endif
    {
#if defined(__SSE2__)
    if constexpr(K==14&&E==14){
        const auto bytes=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(source));
        const unsigned upper=unsigned(_mm_movemask_epi8(bytes));
        const std::uint8_t a=std::uint8_t(live&~upper),b=std::uint8_t(live&upper);
        low_empty=std::uint8_t(~a);high_empty=std::uint8_t(~b);
        const auto weights=_mm_setr_epi8(1,2,4,8,16,32,64,char(128),0,0,0,0,0,0,0,0);
        const auto dead_a=_mm_cmpeq_epi8(_mm_and_si128(_mm_set1_epi8(char(a)),weights),_mm_setzero_si128());
        const auto dead_b=_mm_cmpeq_epi8(_mm_and_si128(_mm_set1_epi8(char(b)),weights),_mm_setzero_si128());
        const auto reduced=_mm_and_si128(bytes,_mm_set1_epi8(127));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(low),_mm_or_si128(reduced,dead_a));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(high),_mm_or_si128(reduced,dead_b));
    }else
#endif
    {
    unsigned upper=0;
    if constexpr(split<tail)upper=source[body*8+split];
    else {
        constexpr unsigned bit=split-tail;
        ht::each<8>([&](auto row) __attribute__((always_inline)){upper|=((source[row*body+bit/8]>>(bit%8))&1u)<<row;});
    }
    const std::uint8_t active[2]={std::uint8_t(live&~upper),std::uint8_t(live&upper)};
    low_empty=std::uint8_t(~active[0]);high_empty=std::uint8_t(~active[1]);
    std::uint8_t* targets[2]={low,high};
    for(unsigned child=0;child<2;++child){
        ht::each<8>([&](auto row) __attribute__((always_inline)){
            constexpr word keep=split<tail?ht::mask<body*8>:
                ht::mask<body*8> ^ (word{1}<<(split-tail));
            const word selected=word{0}-word((active[child]>>row)&1u);
            ht::store<body>(targets[child]+row*body,
                (ht::load<body>(source+row*body)&keep)|~selected);
        });
    }
    }}
    if constexpr(tail){
        static_assert(split>=tail);
        constexpr word repeated=0x0101010101010101ULL&ht::mask<tail*8>;
        const auto planes=ht::load<tail>(source+body*8);
        ht::store<tail>(low+body*8,planes|word(low_empty)*repeated);
        ht::store<tail>(high+body*8,planes|word(high_empty)*repeated);
    }
}

template<unsigned K,unsigned E> [[gnu::always_inline]] void split_tile(
        const byte* source,byte live,byte* low,byte* high,byte& low_empty,byte& high_empty) {
    std::uint8_t low_mask,high_mask;
    numeric_split_tile<K,E>(ikea::to_u8(source),std::to_integer<std::uint8_t>(live),
        ikea::to_u8(low),ikea::to_u8(high),low_mask,high_mask);
    low_empty=byte(low_mask);high_empty=byte(high_mask);
}

template<class Table> bool live_row(const byte* p,unsigned row) {
    if constexpr(Table::bitmap)return (number(p[row/8])&(1u<<(row%8)))!=0;
    else return !(number(p[Table::flags_offset+row/8])&(1u<<(row%8)));
}
template<class Table> void occupy(byte* p,unsigned row,word q) {
    if constexpr(Table::bitmap)p[row/8]|=byte(1u<<(row%8));
    else {ht::set_point<Table::encoding>(ikea::to_u8(p),row,Table::N,q);
        p[Table::flags_offset+row/8]&=byte(~(1u<<(row%8)));}
}

// Exclusive coherent source; unpublished independent heap destination. This
// seam does not supply key authority, MVCC publication, or retained snapshots.
// Tiles=true is a legal encoding alternative, not a destination read profile.
// CopyImage requires fully initialized source images, including dead value rows;
// it cannot consume prefix-only or live-row-only storage.
template<class Destination,bool Tiles=true,bool PreserveSwiss=true,bool CopyImage=false,class Owner>
std::unique_ptr<Destination> grow(const Owner& owner) {
    const auto& original=reader_owner(owner);
    using Source=std::remove_cvref_t<decltype(original)>;
    using Target=std::remove_cvref_t<decltype(reader_owner(std::declval<Destination&>()))>;
    static_assert(std::is_same_v<typename Source::value_type,typename Target::value_type>);
    static_assert(std::is_same_v<typename Source::storage_policy,ht::heap_storage_policy>);
    static_assert(std::is_same_v<typename Target::storage_policy,ht::heap_storage_policy>);
    static_assert(Source::K==Target::K+1);
    static_assert(Source::bucket_count*2==Target::bucket_count);
    static_assert(Source::key_bytes==Target::key_bytes);
    static_assert(Target::bitmap||Target::N>=Source::N);
    constexpr bool tile_split=Tiles&&!Source::bitmap&&!Target::bitmap&&
        Source::encoding==Target::encoding&&Source::T==Target::T&&Source::B==Target::B;
    static_assert(!CopyImage||(tile_split&&Source::stride==Target::stride));
    const auto from=access::get(original);
    auto result=[&]{if constexpr(CopyImage)return std::make_unique<Destination>(access::uninitialized_image<Target>(from.mix.seed));
        else return std::make_unique<Destination>(from.mix.seed);}();
    auto to=access::get(reader_owner(*result));
    for(std::size_t i=0;i<Source::bucket_count;++i){
        const auto* p=from.primary.data+i*Source::stride;
        auto* low=to.primary.data+(i*2)*Target::stride;
        auto* high=low+Target::stride;
        if constexpr(CopyImage){
            std::memcpy(low,p,Source::stride);std::memcpy(high,p,Source::stride);
            low[Target::overflow_offset]=high[Target::overflow_offset]=byte{0};
        }
        if constexpr(tile_split)ht::each<Source::T>([&](auto tile){
            split_tile<Source::K,Source::encoding>(p+tile*Source::encoding,
                byte(~p[Source::flags_offset+tile]),low+tile*Target::encoding,
                high+tile*Target::encoding,low[Target::flags_offset+tile],
                high[Target::flags_offset+tile]);
        });
        // The destination masks already partition the value rows. Reuse those
        // masks instead of decoding quotients again just to choose a child.
        if constexpr(tile_split&&!CopyImage&&std::tuple_size_v<typename Source::value_type>!=0){
            for(auto* target:{low,high}){
                word live=~ht::load<Target::T>(target+Target::flags_offset)&ht::mask<Target::N>;
                while(live){const auto row=unsigned(std::countr_zero(live));live&=live-1;
                    access::write_value<Target>(ikea::to_u8(target),row,access::primary_value<Source>(ikea::to_u8(p),row));}
            }
        }
        // Preserve row indices when possible; only bitmap destinations use the
        // quotient as their row. Values use the existing owner's actual format.
        if constexpr(!tile_split)
        for(unsigned row=0;row<Source::N;++row)if(live_row<Source>(p,row)){
            const word q=[&]{if constexpr(Source::bitmap)return word(row);
                else return write_arch::point<Source::encoding>(p,row,Source::N);}();
            const auto reduced=q&ht::mask<Target::K>;
            auto* target=(q>>Target::K)?high:low;
            const auto next_row=Target::bitmap?unsigned(reduced):row;
            if constexpr(!tile_split)occupy<Target>(target,next_row,reduced);
            access::write_value<Target>(ikea::to_u8(target),next_row,access::primary_value<Source>(ikea::to_u8(p),row));
        }
    }
    if constexpr(CopyImage){
        constexpr auto used=Target::bucket_count*Target::stride;
        std::memset(to.primary.data+used,0,to.primary.size-used);
    }
    to.size=from.size-from.spill;
    if constexpr(PreserveSwiss&&!Target::bitmap){
        // Swiss hash/fingerprint and probe placements do not depend on K/b.
        to.controls=ht::duplicate_buffer<ht::heap_storage_policy>(from.controls,nullptr);
        to.slots=ht::duplicate_buffer<ht::heap_storage_policy>(from.slots,nullptr);
        to.groups=from.groups;to.spill=from.spill;to.deleted=from.deleted;to.size=from.size;
        for(std::size_t slot=0;slot<from.groups*16;++slot)if(write_arch::occupied_control(from.controls.data[slot])){
            const auto key=ht::load<Source::key_bytes>(from.slots.data+slot*Source::tuple_bytes);
            const word lo=(key^from.mix.seed)*Source::mix_type::multiplier;
            const auto bucket=(lo>>Target::K)&(Target::bucket_count-1);
            auto& count=to.primary.data[bucket*Target::stride+Target::overflow_offset];
            if(count!=byte{255})count=byte(number(count)+1);
        }
    }else {
        for(std::size_t slot=0;slot<from.groups*16;++slot)if(write_arch::occupied_control(from.controls.data[slot])){
            const auto* tuple=from.slots.data+slot*Source::tuple_bytes;
            const auto key=ht::load<Source::key_bytes>(tuple);
            typename Source::value_type value{};
            if constexpr(value.size())std::memcpy(value.data(),tuple+Source::key_bytes,value.size());
            result->insert(key,value);
        }
    }
    return result;
}
}
