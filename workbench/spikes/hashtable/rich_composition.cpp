// Bounded composition experiment, not a table implementation. Both arms own
// their encoding and candidate routing; only the logical input/consumer is shared.
// sp_tp_split is the strongest existing-primitive composition in this driver.
// sp_tp_prepared and sp_tp_admitted retain the original all-code projection as
// diagnostics of runtime instruction controls and avoidable projection costs.
// F7+C13 is an independent primitive stress case, not clamp-derived hash geometry.
// F8+C13 corresponds to K40/A19 bucket components; hashing/table costs are excluded.
#include "common.h"
#include <ikea/seriespack/author/native.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/native.h>
#include <span>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>

namespace rich {
using ht::byte; using ht::word; using ht::require; using ht::mask;
namespace sp=ikea::seriespack;
namespace tp=ikea::tuplepack;
#ifndef HT_RICH_FP_BITS
#define HT_RICH_FP_BITS 7
#endif
#ifndef HT_RICH_COMPOSED_MASK_BITS
#define HT_RICH_COMPOSED_MASK_BITS 64
#endif
#ifndef HT_RICH_RAW_MASK_BITS
#define HT_RICH_RAW_MASK_BITS 32
#endif
constexpr unsigned fingerprint_bits=HT_RICH_FP_BITS;
static_assert(fingerprint_bits==7 || fingerprint_bits==8);
static_assert(HT_RICH_COMPOSED_MASK_BITS==32 || HT_RICH_COMPOSED_MASK_BITS==64);
static_assert(HT_RICH_RAW_MASK_BITS==32 || HT_RICH_RAW_MASK_BITS==64);
constexpr unsigned capacity=16, fingerprint_bytes=2*fingerprint_bits;
constexpr unsigned fingerprint_limit=1u<<fingerprint_bits;
constexpr unsigned fingerprint_mask=fingerprint_limit-1;
struct entry { word value; unsigned suffix; byte fingerprint, tag; };
struct answer { word value=0; unsigned slot=capacity; byte tag=0;
    bool operator==(const answer&) const=default;
};
struct query { unsigned bucket, suffix; byte fingerprint; };
template<unsigned Row> using bytes=std::array<byte,fingerprint_bytes+capacity*Row>;
using records=std::array<entry,capacity>;
template<unsigned Row> constexpr unsigned value_bits=Row==6?32:64;
template<unsigned Row> constexpr unsigned value_start=Row==6?14:16;

// The independent oracle operates on individual physical bits, never TP codes
// or the raw arm's word packing. It also describes the two preserved spare bits.
word bit_read(const byte* p,unsigned start,unsigned count) {
    word v=0;
    for(unsigned b=0;b<count;++b) v|=word((p[(start+b)/8]>>((start+b)%8))&1)<<b;
    return v;
}
void bit_write(byte* p,unsigned start,unsigned count,word v) {
    for(unsigned b=0;b<count;++b) {
        auto& out=p[(start+b)/8]; auto m=byte(1u<<((start+b)%8));
        out=byte((out&~m)|(((v>>b)&1)?m:0));
    }
}
template<unsigned Row> bytes<Row> oracle_encode(const records& rows) {
    bytes<Row> out; out.fill(0xa5);
    for(unsigned s=0;s<capacity;++s) {
        if constexpr(fingerprint_bits==7) {
            for(unsigned b=0;b<7;++b)
                bit_write(out.data()+7*(s/8),b*8+s%8,1,rows[s].fingerprint>>b);
        } else bit_write(out.data(),s*8,8,rows[s].fingerprint);
        byte* p=out.data()+fingerprint_bytes+s*Row;
        bit_write(p,0,13,rows[s].suffix); bit_write(p,13,1,rows[s].tag);
        bit_write(p,value_start<Row>,value_bits<Row>,rows[s].value);
    }
    return out;
}

struct prepared {
    tp::layout layout;
    tp::reader<8> read;
    tp::writer<8> write_all, write_value;
    tp::reader<8> read_value;
    tp::reader<8> suffix_read, tag_low_read, high_read;
    tp::writer<8> low_write, high_write;
    template<std::size_t N> static tp::layout make_layout(unsigned width,const std::array<tp::code,N>& codes) {
        auto p=tp::layout::make(width,codes); require(bool(p),"rich layout"); return *p;
    }
    static tp::reader<8> reader(const tp::layout& l,std::span<const byte> order) {
        auto p=tp::reader<8>::make(l,order); require(bool(p),"rich reader"); return *p;
    }
    static tp::writer<8> writer(const tp::layout& l,std::span<const byte> order) {
        auto p=tp::writer<8>::make(l,order); require(bool(p),"rich writer"); return *p;
    }
    explicit prepared(unsigned row):layout(row==6?
        make_layout(6,std::array<tp::code,8>{{{0,0,8},{1,0,5},{1,5,1},{1,6,2},
                                            {2,0,8},{3,0,8},{4,0,8},{5,0,6}}}):
        make_layout(10,std::array<tp::code,11>{{{0,0,8},{1,0,5},{1,5,1},
                     {2,0,8},{3,0,8},{4,0,8},{5,0,8},{6,0,8},{7,0,8},{8,0,8},{9,0,8}}})),
        read(reader(layout,row==6?std::span<const byte>(all6):std::span<const byte>(header10))),
        write_all(writer(layout,row==6?std::span<const byte>(all6):std::span<const byte>(header10))),
        write_value(writer(layout,row==6?std::span<const byte>(value6):std::span<const byte>(value10))),
        read_value(reader(layout,row==6?std::span<const byte>(value6):std::span<const byte>(value10))),
        suffix_read(reader(layout,suffix_codes)),
        tag_low_read(reader(layout,row==6?std::span<const byte>(tag_low6):std::span<const byte>(tag10))),
        high_read(reader(layout,row==6?std::span<const byte>(high6):std::span<const byte>(value10))),
        low_write(writer(layout,row==6?std::span<const byte>(low6):std::span<const byte>(tag10))),
        high_write(writer(layout,row==6?std::span<const byte>(high6):std::span<const byte>(value10))) {}
    static constexpr std::array<byte,8> all6{0,1,2,3,4,5,6,7};
    static constexpr std::array<byte,3> header10{0,1,2};
    static constexpr std::array<byte,5> value6{3,4,5,6,7};
    static constexpr std::array<byte,8> value10{3,4,5,6,7,8,9,10};
    static constexpr std::array<byte,2> suffix_codes{0,1},tag_low6{2,3};
    static constexpr std::array<byte,1> tag10{2},low6{3};
    static constexpr std::array<byte,4> high6{4,5,6,7};
};
template<unsigned Row> inline const prepared plan{Row};

// Spike-only instruction binding, not a proposed public API. These point bodies
// never inspect packet_layout: Rows==1 selects a direct one-row branch. The
// admitted ordering is nevertheless checked along with every consumed operand
// and the complete physical effects. Production preparation remains the oracle.
template<unsigned Row> struct recipe {
    template<std::size_t N> static consteval tp::detail::gpr_read field_read(
        std::array<tp::code,N> codes,tp::detail::word_transfer transfer={}) {
        tp::detail::gpr_read r{};r.slots=N;r.bytes=Row;r.transfer=transfer;
        for(unsigned i=0;i<N;++i){r.codes[i]=codes[i];r.reads|=word(1)<<codes[i].offset;}
        return r;
    }
    // Only the fixed nonoverlapping byte projections below qualify. This is a
    // small literal-control builder, checked against production, not a planner.
    static consteval tp::detail::gpr_write field_write(tp::detail::gpr_read r) {
        tp::detail::gpr_write w{};w.read=r;w.transfer=r.transfer;
        w.count=r.slots;w.runs=1;w.writes=r.reads;
        w.word.offset=r.codes[0].offset;w.word.bytes=byte(r.slots);w.word.selected=byte(r.slots);
        w.footprint[0]={w.word.offset,byte(r.slots)};
        for(unsigned i=0;i<r.slots;++i) {
            auto c=r.codes[i];auto& s=w.stores[i];s.offset=c.offset;s.count=1;
            s.mask=byte(mask(c.width)<<c.shift);s.input[0]=byte(i);s.shift[0]=c.shift;
            w.invalid[i]=byte(~mask(c.width));w.invalid_word|=word(w.invalid[i])<<(8*i);
            w.word.input_shift[i]=byte(8*i);w.word.output_shift[i]=byte(8*i+c.shift);
            w.word.changed|=mask(c.width)<<(8*i+c.shift);
        }
        if(((r.transfer.mask<<r.transfer.shift)&mask(8*r.transfer.bytes))!=mask(8*r.transfer.bytes))
            w.native_reads=w.writes;
        return w;
    }
    static constexpr auto suffix_read=field_read(std::array<tp::code,2>{{{0,0,8},{1,0,5}}},{8191,0,0,2});
    static constexpr auto tag_low_read=[] {
        if constexpr(Row==6)return field_read(std::array<tp::code,2>{{{1,5,1},{1,6,2}}});
        else return field_read(std::array<tp::code,1>{{{1,5,1}}},{1,1,5,1});
    }();
    static constexpr auto high_read=[] {
        if constexpr(Row==6)return field_read(std::array<tp::code,4>{{{2,0,8},{3,0,8},{4,0,8},{5,0,6}}},{0x3fffffff,2,0,4});
        else {
            std::array<tp::code,8> codes{};for(unsigned i=0;i<8;++i)codes[i]={byte(i+2),0,8};
            return field_read(codes,{~word(0),2,0,8});
        }
    }();
    static constexpr auto low_write=[] {
        if constexpr(Row==6)return field_write(field_read(std::array<tp::code,1>{{{1,6,2}}},{3,1,6,1}));
        else return field_write(tag_low_read);
    }();
    static constexpr auto high_write=field_write(high_read);
    static constexpr tp::detail::gpr_read read=[] {
        tp::detail::gpr_read r{};r.bytes=Row;
        if constexpr(Row==6) {
            r.slots=8;r.reads=63;
            r.codes={{{0,0,8},{1,0,5},{1,5,1},{1,6,2},{2,0,8},{3,0,8},{4,0,8},{5,0,6}}};
        } else {
            r.slots=3;r.reads=3;r.codes={{{0,0,8},{1,0,5},{1,5,1}}};
        }
        return r;
    }();
    static constexpr tp::detail::gpr_read value_read=[] {
        tp::detail::gpr_read r{};r.bytes=Row;
        if constexpr(Row==6) {
            r.slots=5;r.reads=62;
            r.codes={{{1,6,2},{2,0,8},{3,0,8},{4,0,8},{5,0,6}}};
        } else {
            r.slots=8;r.reads=1020;r.transfer={~word(0),2,0,8};
            for(unsigned i=0;i<8;++i)r.codes[i]={byte(i+2),0,8};
        }
        return r;
    }();
    static constexpr tp::detail::gpr_write write=[] {
        tp::detail::gpr_write w{};
        static_cast<tp::detail::scalar_read<8>&>(w.read)=value_read;
        w.count=Row==6?5:8;w.runs=1;w.writes=Row==6?62:1020;
        w.footprint[0]={byte(Row==6?1:2),byte(w.count)};
        w.word.selected=byte(w.count);w.word.offset=byte(Row==6?1:2);w.word.bytes=byte(w.count);
        w.word.changed=Row==6?(word(0xffffffff)<<6):~word(0);
        for(unsigned i=0;i<w.count;++i) {
            auto& s=w.stores[i];s.offset=byte(w.word.offset+i);s.count=1;
            s.mask=byte(Row==6?(i==0?192:i==4?63:255):255);
            s.input[0]=byte(i);s.shift[0]=byte(Row==6&&i==0?6:0);
            w.word.input_shift[i]=byte(i*8);w.word.output_shift[i]=byte(i*8+s.shift[0]);
        }
        if constexpr(Row==6) {
            w.invalid[0]=252;w.invalid[4]=192;w.invalid_word=word(192)<<32|252;
            w.native_reads=62;
        } else w.transfer={~word(0),2,0,8};
        return w;
    }();
    static bool transfer_equal(const tp::detail::word_transfer& a,const tp::detail::word_transfer& b) {
        return a.mask==b.mask&&a.offset==b.offset&&a.shift==b.shift&&a.bytes==b.bytes;
    }
    static bool read_equal(const tp::detail::scalar_read<8>& actual,const tp::detail::scalar_read<8>& fixed) {
        if(actual.codes!=fixed.codes||actual.slots!=fixed.slots||actual.bytes!=fixed.bytes||actual.reads!=fixed.reads)
            return false;
        const auto& o=actual.ordering;
        if(o.rows()!=1||o.map_size()!=fixed.slots||!o.single_group()||!o.row_major())return false;
        for(unsigned i=0;i<8;++i) {
            if(o.row_indices()[i]!=(i<fixed.slots?0:255))return false;
            if(i<fixed.slots&&o.offset(0,i)!=i)return false;
        }
        return true;
    }
    static void check_read(const tp::detail::gpr_read& actual,const tp::detail::gpr_read& fixed) {
        require(read_equal(actual,fixed)&&transfer_equal(actual.transfer,fixed.transfer),"rich fixed read admission");
    }
    static void check_write(const tp::detail::gpr_write& w,const tp::detail::gpr_write& fixed) {
        require(read_equal(w.read,fixed.read)&&transfer_equal(w.transfer,fixed.transfer),"rich fixed write read/transfer");
        require(w.count==fixed.count&&w.runs==fixed.runs&&w.writes==fixed.writes&&
            w.invalid==fixed.invalid&&w.invalid_word==fixed.invalid_word&&w.native_reads==fixed.native_reads,
            "rich fixed write effects");
        const auto& a=w.word;const auto& b=fixed.word;
        require(a.changed==b.changed&&a.input_shift==b.input_shift&&a.output_shift==b.output_shift&&
            a.offset==b.offset&&a.bytes==b.bytes&&a.selected==b.selected,"rich fixed write word");
        for(unsigned i=0;i<8;++i) {
            const auto& x=w.stores[i];const auto& y=fixed.stores[i];
            require(x.offset==y.offset&&x.mask==y.mask&&x.count==y.count&&x.input==y.input&&x.shift==y.shift,
                "rich fixed write store");
            require(w.footprint[i].offset==fixed.footprint[i].offset&&w.footprint[i].size==fixed.footprint[i].size,
                "rich fixed write footprint");
        }
    }
    static void admit() {
        const auto& p=plan<Row>;
        check_read(p.read.controls(),read);check_read(p.read_value.controls(),value_read);
        check_write(p.write_value.controls(),write);
        check_read(p.suffix_read.controls(),suffix_read);check_read(p.tag_low_read.controls(),tag_low_read);
        check_read(p.high_read.controls(),high_read);check_write(p.low_write.controls(),low_write);
        check_write(p.high_write.controls(),high_write);
    }
};

[[gnu::always_inline]] inline word value_packet32(word v) {
    return (v&3)|(((v>>2)&255)<<8)|(((v>>10)&255)<<16)|
           (((v>>18)&255)<<24)|(((v>>26)&63)<<32);
}
[[gnu::always_inline]] inline word unpack32(word packet) {
    return (packet&3)|(((packet>>8)&255)<<2)|(((packet>>16)&255)<<10)|
           (((packet>>24)&255)<<18)|(((packet>>32)&63)<<26);
}

template<unsigned Row,bool Bound=false,bool Split=false> struct composed {
    static_assert(Row==6||Row==10);
    static constexpr const char* name=Split?"sp_tp_split":Bound?"sp_tp_admitted":"sp_tp_prepared";
#if defined(__aarch64__)
    using candidates_type=word;
#else
    // Bounded F8 x86 crossover control; defaults retain the original experiment.
    using candidates_type=std::conditional_t<fingerprint_bits==8 && HT_RICH_COMPOSED_MASK_BITS==32,unsigned,word>;
#endif
    [[gnu::always_inline]] static const auto& read_control() {
        if constexpr(Bound)return recipe<Row>::read;else return plan<Row>.read.controls();
    }
    [[gnu::always_inline]] static const auto& value_control() {
        if constexpr(Bound)return recipe<Row>::value_read;else return plan<Row>.read_value.controls();
    }
    [[gnu::always_inline]] static const auto& write_control() {
        if constexpr(Bound)return recipe<Row>::write;else return plan<Row>.write_value.controls();
    }
    static bytes<Row> encode(const records& rows) {
        bytes<Row> out; out.fill(0xa5);
        for(unsigned s=0;s<capacity;++s) {
            const auto& x=rows[s];
            sp::detail::put_payload<sp::format<fingerprint_bits>>(out.data()+fingerprint_bits*(s/8),s%8,x.fingerprint);
            byte* p=out.data()+fingerprint_bytes+s*Row;
            word packet=(x.suffix&255)|(word(x.suffix>>8)<<8)|(word(x.tag)<<16);
            if constexpr(Row==6) packet|=value_packet32(x.value)<<24;
            tp::native::write_body(plan<Row>.write_all.controls(),p,packet);
            if constexpr(Row==10) tp::native::write_body(plan<Row>.write_value.controls(),p,x.value);
        }
        return out;
    }
    [[gnu::always_inline]] static candidates_type matches(const byte* fp,byte key,unsigned active) {
        auto v=sp::native::read16<sp::format<fingerprint_bits>,true>(fp,fingerprint_bits,0).v[0];
#if defined(__aarch64__)
        auto eq=vceqq_u8(v,vdupq_n_u8(key));
        word bits=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq),4)),0);
        return bits&mask(active*4);
#elif defined(__AVX2__)
#if defined(__AVX512BW__) && defined(__AVX512VL__)
        return unsigned(_mm_cmpeq_epi8_mask(v,_mm_set1_epi8(key)))&mask(active);
#else
        return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(v,_mm_set1_epi8(key))))&mask(active);
#endif
#else
#error Explicit NEON or AVX2 required
#endif
    }
    [[gnu::always_inline]] static answer find(const byte* fp,const byte* rows,unsigned active,
                                               byte key,unsigned suffix) {
        if(!active) return {};
        auto candidates=matches(fp,key,active);
        while(candidates) {
#if defined(__aarch64__)
            unsigned slot=std::countr_zero(candidates)/4;
            candidates&=~(word(15)<<(slot*4));
#else
            unsigned slot=std::countr_zero(candidates); candidates&=candidates-1;
#endif
            const byte* p=rows+slot*Row;
            if constexpr(Split) {
                word candidate=tp::native::read_transfer_body<1,2>(recipe<Row>::suffix_read,[&](unsigned){return p;},1);
                if(candidate!=suffix)continue;
                word tag_low=tp::native::read_body(recipe<Row>::tag_low_read,p);
                word high=tp::native::read_transfer_body<1,Row==6?4:8>(recipe<Row>::high_read,[&](unsigned){return p;},1);
                word value=Row==6?((tag_low>>8)|(high<<2)):high;
                return {value,slot,byte(tag_low&1)};
            }
            word packet=tp::native::read_body(read_control(),p);
            if(unsigned((packet&255)|(((packet>>8)&31)<<8))!=suffix) continue;
            word v;
            if constexpr(Row==6) v=unpack32(packet>>24);
            else v=tp::native::read_body(value_control(),p);
            return {v,slot,byte((packet>>16)&1)};
        }
        return {};
    }
    [[gnu::always_inline]] static answer update(const byte* fp,byte* rows,unsigned active,
                                                 byte key,unsigned suffix) {
        auto a=find(fp,rows,active,key,suffix);
        if(a.slot!=capacity) {
            word next=a.value^mask(value_bits<Row>);
            if constexpr(Split) {
                byte* p=rows+a.slot*Row;
                if constexpr(Row==6)tp::native::write_body(recipe<Row>::low_write,p,next&3);
                tp::native::write_body(recipe<Row>::high_write,p,Row==6?next>>2:next);
            } else {
                if constexpr(Row==6) next=value_packet32(next);
                tp::native::write_body(write_control(),rows+a.slot*Row,next);
            }
        }
        return a;
    }
};
template<unsigned Row> using admitted=composed<Row,true>;
template<unsigned Row> using split=composed<Row,true,true>;

// Independent fused control: equality reduces the seven bitplanes directly;
// it does not materialize 16 unpacked fingerprints or call either primitive.
// Loads are exact 4+2+1 bytes, including the second Local7 tile at a guard page.
// The F8 companion independently encodes raw bytes and compares one explicit
// 16-byte SIMD load; it never calls SeriesPack to produce candidates.
template<unsigned Row> struct fused {
    static constexpr const char* name="raw_fused";
    static bytes<Row> encode(const records& rows) {
        bytes<Row> out; out.fill(0xa5);
        if constexpr(fingerprint_bits==7) {
            for(unsigned tile=0;tile<2;++tile)for(unsigned b=0;b<7;++b) {
                byte plane=0;
                for(unsigned s=0;s<8;++s) plane|=byte(((rows[tile*8+s].fingerprint>>b)&1)<<s);
                out[tile*7+b]=plane;
            }
        } else for(unsigned s=0;s<capacity;++s)out[s]=rows[s].fingerprint;
        for(unsigned s=0;s<capacity;++s) {
            const auto& x=rows[s]; byte* p=out.data()+fingerprint_bytes+s*Row;
            if constexpr(Row==6) {
                word v=(word(0xa5a5a5a5a5a5ULL)&(word(3)<<46))|x.suffix|
                       (word(x.tag)<<13)|((x.value&0xffffffffULL)<<14);
                std::uint32_t lo=v; std::uint16_t hi=v>>32;
                __builtin_memcpy(p,&lo,4); __builtin_memcpy(p+4,&hi,2);
            } else {
                std::uint16_t head=std::uint16_t((0xa5a5&0xc000)|x.suffix|(unsigned(x.tag)<<13));
                __builtin_memcpy(p,&head,2); __builtin_memcpy(p+2,&x.value,8);
            }
        }
        return out;
    }
    [[gnu::always_inline]] static word load7(const byte* p) {
        std::uint32_t lo; std::uint16_t hi;
        __builtin_memcpy(&lo,p,4); __builtin_memcpy(&hi,p+4,2);
        return word(lo)|(word(hi)<<32)|(word(p[6])<<48);
    }
#if defined(__aarch64__)
    using candidates_type=std::conditional_t<fingerprint_bits==8,word,unsigned>;
#else
    using candidates_type=std::conditional_t<fingerprint_bits==8 && HT_RICH_RAW_MASK_BITS==64,word,unsigned>;
#endif
    [[gnu::always_inline]] static candidates_type matches(const byte* p,byte key,unsigned active) {
        if constexpr(fingerprint_bits==8) {
#if defined(__aarch64__)
            const auto equal=vceqq_u8(vld1q_u8(p),vdupq_n_u8(key));
            const auto nibbles=vshrn_n_u16(vreinterpretq_u16_u8(equal),4);
            return vget_lane_u64(vreinterpret_u64_u8(nibbles),0)&mask(active*4);
#elif defined(__AVX2__)
            const auto bytes=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
#if defined(__AVX512BW__) && defined(__AVX512VL__)
            return unsigned(_mm_cmpeq_epi8_mask(bytes,_mm_set1_epi8(key)))&mask(active);
#else
            return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(bytes,_mm_set1_epi8(key))))&mask(active);
#endif
#else
#error Explicit NEON or AVX2 required
#endif
        }
        word x=key;
        x=(x|(x<<28))&0x0000000f0000000fULL;
        x=(x|(x<<14))&0x0003000300030003ULL;
        x=(x|(x<<7))&0x0101010101010101ULL;
        const word expanded=x*255;
        word a=load7(p)^expanded, b=load7(p+7)^expanded;
        a|=a>>32; a|=a>>16; a|=a>>8;
        b|=b>>32; b|=b>>16; b|=b>>8;
        return unsigned(((~a&255)|((~b&255)<<8))&mask(active));
    }
    [[gnu::always_inline]] static answer find(const byte* fp,const byte* rows,unsigned active,
                                               byte key,unsigned suffix) {
        if(!active) return {};
        auto selected=matches(fp,key,active);
        while(selected) {
            unsigned slot;
#if defined(__aarch64__)
            if constexpr(fingerprint_bits==8) {
                slot=std::countr_zero(selected)/4;selected&=~(word(15)<<(slot*4));
            } else
#endif
            {slot=std::countr_zero(selected);selected&=selected-1;}
            const byte* p=rows+slot*Row;
            if constexpr(Row==6) {
                std::uint32_t lo; std::uint16_t hi;
                __builtin_memcpy(&lo,p,4); __builtin_memcpy(&hi,p+4,2);
                word bits=word(lo)|(word(hi)<<32);
                if((bits&8191)!=suffix) continue;
                return {(bits>>14)&0xffffffffULL,slot,byte((bits>>13)&1)};
            } else {
                std::uint16_t h; __builtin_memcpy(&h,p,2);
                if((h&8191)!=suffix) continue;
                word v; __builtin_memcpy(&v,p+2,8);
                return {v,slot,byte((h>>13)&1)};
            }
        }
        return {};
    }
    [[gnu::always_inline]] static answer update(const byte* fp,byte* rows,unsigned active,
                                                 byte key,unsigned suffix) {
        auto a=find(fp,rows,active,key,suffix);
        if(a.slot!=capacity) {
            byte* p=rows+a.slot*Row; word v=a.value^mask(value_bits<Row>);
            if constexpr(Row==6) {
                // Exactly bytes 1..5 change. Preserve C13, tag, and spare bits.
                p[1]=byte((p[1]&63)|((v&3)<<6));
                p[2]=byte(v>>2); p[3]=byte(v>>10); p[4]=byte(v>>18);
                p[5]=byte((p[5]&192)|(v>>26));
            } else __builtin_memcpy(p+2,&v,8);
        }
        return a;
    }
};

[[gnu::always_inline]] word consume(answer a) {
    // Every value bit affects the result and thus the serial next-query index.
    return a.value^std::rotl(a.value,23)^(word(a.tag)<<53)^(word(a.slot)<<37);
}
#define RICH_WITNESS(ROW,ARM) \
extern "C" [[gnu::noinline,gnu::used,gnu::retain]] word rich_##ARM##ROW##_get( \
    const byte* f,const byte* r,unsigned n,byte k,unsigned s) {return consume(ARM<ROW>::find(f,r,n,k,s));} \
extern "C" [[gnu::noinline,gnu::used,gnu::retain]] word rich_##ARM##ROW##_update( \
    const byte* f,byte* r,unsigned n,byte k,unsigned s) {return consume(ARM<ROW>::update(f,r,n,k,s));}
RICH_WITNESS(6,composed) RICH_WITNESS(10,composed)
RICH_WITNESS(6,admitted) RICH_WITNESS(10,admitted)
RICH_WITNESS(6,split) RICH_WITNESS(10,split)
RICH_WITNESS(6,fused) RICH_WITNESS(10,fused)
#undef RICH_WITNESS

template<unsigned Row> answer oracle_find(const records& rows,unsigned active,byte fp,unsigned suffix) {
    for(unsigned s=0;s<active;++s) if(rows[s].fingerprint==fp&&rows[s].suffix==suffix)
        return {rows[s].value&mask(value_bits<Row>),s,rows[s].tag};
    return {};
}
records make_records(word seed,unsigned fingerprint_count) {
    records rows{};
    constexpr std::array<word,8> values{0,~word(0),1,word(1)<<31,word(1)<<63,
        0xaaaaaaaaaaaaaaaaULL,0x5555555555555555ULL,0xffffffff00000000ULL};
    for(unsigned s=0;s<capacity;++s) {
        auto r=ht::mix(seed+s*8713);
        rows[s]={s<8?values[(s+seed)%values.size()]:r,unsigned((seed*17+s*257)&8191),
                 byte((seed+s%fingerprint_count)&fingerprint_mask),byte((r>>29)&1)};
    }
    return rows;
}
struct guard {
    byte* base; std::size_t page;
    guard():page(std::size_t(sysconf(_SC_PAGESIZE))) {
        base=static_cast<byte*>(mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        require(base!=MAP_FAILED,"guard mapping");
        require(mprotect(base+page,page,PROT_READ|PROT_WRITE)==0,"guard protection");
    }
    byte* end() const{return base+2*page;}
    ~guard(){munmap(base,page*3);}
};
template<unsigned Row,class Arm> void guard_check(const records& rows,unsigned active) {
    auto encoded=Arm::encode(rows); guard f,r;
    byte* fp=f.end()-fingerprint_bytes; byte* data=r.end()-active*Row;
    std::memcpy(fp,encoded.data(),fingerprint_bytes);
    if(active)std::memcpy(data,encoded.data()+fingerprint_bytes,active*Row);
    else fp=f.end(); // Even the fingerprint pointer need not name storage.
    for(unsigned s=0;s<capacity;++s) {
        auto expected=oracle_find<Row>(rows,active,rows[s].fingerprint,rows[s].suffix);
        require(Arm::find(fp,data,active,rows[s].fingerprint,rows[s].suffix)==expected,"guard lookup");
        require(Arm::update(fp,data,active,rows[s].fingerprint,rows[s].suffix)==expected,"guard update");
        if(expected.slot!=capacity) {
            auto again=Arm::update(fp,data,active,rows[s].fingerprint,rows[s].suffix);
            expected.value^=mask(value_bits<Row>); require(again==expected,"guard changed update");
        }
    }
    if(active) require(std::memcmp(data,encoded.data()+fingerprint_bytes,active*Row)==0,"guard restored rows");
}
template<unsigned Row> void correctness() {
    constexpr std::array<unsigned,6> tails{0,1,7,8,15,16};
    for(unsigned active:tails)for(unsigned seed=0;seed<fingerprint_limit;++seed)for(unsigned groups:{1u,4u,16u}) {
        auto records=make_records(seed,groups);
        auto expected=oracle_encode<Row>(records), a=composed<Row>::encode(records),b=fused<Row>::encode(records),c=admitted<Row>::encode(records),d=split<Row>::encode(records);
        require(a==expected&&b==expected&&c==expected&&d==expected,"independent full encoding");
        for(unsigned s=0;s<capacity;++s) {
            auto key=records[s];
            auto want=oracle_find<Row>(records,active,key.fingerprint,key.suffix);
            require(composed<Row>::find(a.data(),a.data()+fingerprint_bytes,active,key.fingerprint,key.suffix)==want,"composed lookup");
            require(fused<Row>::find(b.data(),b.data()+fingerprint_bytes,active,key.fingerprint,key.suffix)==want,"fused lookup");
            require(admitted<Row>::find(c.data(),c.data()+fingerprint_bytes,active,key.fingerprint,key.suffix)==want,"admitted lookup");
            require(split<Row>::find(d.data(),d.data()+fingerprint_bytes,active,key.fingerprint,key.suffix)==want,"split lookup");
            require(composed<Row>::update(a.data(),a.data()+fingerprint_bytes,active,key.fingerprint,key.suffix)==want,"composed update result");
            require(fused<Row>::update(b.data(),b.data()+fingerprint_bytes,active,key.fingerprint,key.suffix)==want,"fused update result");
            require(admitted<Row>::update(c.data(),c.data()+fingerprint_bytes,active,key.fingerprint,key.suffix)==want,"admitted update result");
            require(split<Row>::update(d.data(),d.data()+fingerprint_bytes,active,key.fingerprint,key.suffix)==want,"split update result");
            if(want.slot!=capacity) {
                records[want.slot].value^=mask(value_bits<Row>);
                bit_write(expected.data()+fingerprint_bytes+want.slot*Row,value_start<Row>,value_bits<Row>,records[want.slot].value);
            }
            require(a==expected&&b==expected&&c==expected&&d==expected,"all neighboring bits preserved");
        }
        // Exhaustive fingerprint space and suffix-not-present, including false
        // positives against every slot when all 16 fingerprints are equal.
        unsigned absent=(records[0].suffix+1)&8191;
        for(unsigned fp=0;fp<fingerprint_limit;++fp) {
            // Also hold a real suffix fixed while changing every fingerprint:
            // suffix confirmation must not conceal false predicate matches.
            const auto same_suffix=records[0].suffix;
            const auto exact=oracle_find<Row>(records,active,byte(fp),same_suffix);
            require(composed<Row>::find(a.data(),a.data()+fingerprint_bytes,active,byte(fp),same_suffix)==exact,"composed full fingerprint domain");
            require(fused<Row>::find(b.data(),b.data()+fingerprint_bytes,active,byte(fp),same_suffix)==exact,"fused full fingerprint domain");
            require(admitted<Row>::find(c.data(),c.data()+fingerprint_bytes,active,byte(fp),same_suffix)==exact,"admitted full fingerprint domain");
            require(split<Row>::find(d.data(),d.data()+fingerprint_bytes,active,byte(fp),same_suffix)==exact,"split full fingerprint domain");
            auto want=oracle_find<Row>(records,active,byte(fp),absent);
            require(composed<Row>::update(a.data(),a.data()+fingerprint_bytes,active,byte(fp),absent)==want,"composed miss update");
            require(fused<Row>::update(b.data(),b.data()+fingerprint_bytes,active,byte(fp),absent)==want,"fused miss update");
            require(admitted<Row>::update(c.data(),c.data()+fingerprint_bytes,active,byte(fp),absent)==want,"admitted miss update");
            require(split<Row>::update(d.data(),d.data()+fingerprint_bytes,active,byte(fp),absent)==want,"split miss update");
        }
        require(a==expected&&b==expected&&c==expected&&d==expected,"misses preserve entire bucket");
    }
    for(unsigned active:tails) {
        auto rows=make_records(fingerprint_mask,1);
        guard_check<Row,composed<Row>>(rows,active); guard_check<Row,fused<Row>>(rows,active);
        guard_check<Row,admitted<Row>>(rows,active);
        guard_check<Row,split<Row>>(rows,active);
    }
    unsigned calls=0;
    auto address=[&](unsigned)->byte* {++calls;return nullptr;};
    require(tp::native::read_body<1>(plan<Row>.read.controls(),address,0)==0,"inactive reader zero");
    tp::native::write_body<1>(plan<Row>.write_value.controls(),address,0,0);
    require(tp::native::read_body<1>(recipe<Row>::read,address,0)==0,"inactive admitted reader zero");
    tp::native::write_body<1>(recipe<Row>::write,address,0,0);
    require(tp::native::read_transfer_body<1,2>(recipe<Row>::suffix_read,address,0)==0,"inactive split reader zero");
    tp::native::write_body<1>(recipe<Row>::low_write,address,0,0);
    tp::native::write_body<1>(recipe<Row>::high_write,address,0,0);
    require(calls==0,"inactive callback never requested");
}

enum class operation { hit, fingerprint_miss, suffix_miss, serial_hit, update };
constexpr std::array<const char*,5> operation_names{
    "independent_hit","fingerprint_miss","suffix_miss","serial_hit","changed_update"};
struct input {
    unsigned active;
    std::vector<records> buckets;
    std::array<std::vector<query>,3> queries;
    input(unsigned n,unsigned count,unsigned query_count):active(count),buckets(n) {
        require(std::has_single_bit(query_count),"power-of-two query count");
        for(unsigned i=0;i<n;++i)buckets[i]=make_records(i*19,4);
        for(auto& q:queries)q.reserve(query_count);
        for(unsigned i=0;i<query_count;++i) {
            word random=ht::mix(i+71813); unsigned bi=unsigned(random%n),slot=active?unsigned((random>>32)%active):0;
            auto e=buckets[bi][slot];
            queries[0].push_back({bi,e.suffix,e.fingerprint});
            // Four successive fingerprints: adding half the domain is absent.
            queries[1].push_back({bi,e.suffix,byte((e.fingerprint+fingerprint_limit/2)&fingerprint_mask)});
            queries[2].push_back({bi,unsigned((buckets[bi][0].suffix+1)&8191),buckets[bi][0].fingerprint});
        }
    }
};

template<unsigned Row,class Arm> struct physical {
    std::vector<bytes<Row>> data;
    explicit physical(const input& in) {data.reserve(in.buckets.size());for(const auto& r:in.buckets)data.push_back(Arm::encode(r));}
    [[gnu::always_inline]] answer get(const query& q,unsigned active) {
        const auto* p=data[q.bucket].data();return Arm::find(p,p+fingerprint_bytes,active,q.fingerprint,q.suffix);
    }
    [[gnu::always_inline]] answer update(const query& q,unsigned active) {
        auto* p=data[q.bucket].data();return Arm::update(p,p+fingerprint_bytes,active,q.fingerprint,q.suffix);
    }
};
template<unsigned Row> struct reference {
    std::vector<records> data;
    explicit reference(const input& in):data(in.buckets) {}
    answer get(const query& q,unsigned active) {return oracle_find<Row>(data[q.bucket],active,q.fingerprint,q.suffix);}
    answer update(const query& q,unsigned active) {
        auto a=get(q,active);if(a.slot!=capacity)data[q.bucket][a.slot].value^=mask(value_bits<Row>);return a;
    }
};
template<operation Op,class State> [[gnu::noinline]] word run(State& state,const input& in,unsigned rounds) {
    constexpr unsigned qi=Op==operation::fingerprint_miss?1:Op==operation::suffix_miss?2:0;
    const auto& queries=in.queries[qi];word sum=0,index=0;
    for(unsigned r=0;r<rounds;++r) {
        if constexpr(Op==operation::serial_hit) {
            for(unsigned i=0;i<queries.size();++i) {
                const auto& q=queries[index&(queries.size()-1)];
                word value=consume(state.get(q,in.active));sum+=value;index=value^std::rotl(index,7)^i;
            }
        } else {
            // Four independent consumers; no compiler auto-vectorization required.
            std::array<word,4> sums{};
            for(unsigned i=0;i<queries.size();i+=4)ht::lanes<4>([&](auto lane) {
                const auto& q=queries[i+lane];answer a;
                if constexpr(Op==operation::update)a=state.update(q,in.active);
                else a=state.get(q,in.active);
                sums[lane]+=consume(a);
            });
            sum+=sums[0]+sums[1]+sums[2]+sums[3];
        }
    }
    return sum;
}

template<unsigned Row,class Arm,operation Op>
void trial(const input& in,unsigned rounds,unsigned rep,word expected,const reference<Row>& final) {
    physical<Row,Arm> state(in);
    // Complete operation warmup, including all suffix checks/stores. The oracle
    // includes this same warmup, retaining the warmed allocation for timing.
    word warm=run<Op>(state,in,1);asm volatile("" : : "r"(warm) : "memory");
    asm volatile("" : : : "memory");auto start=ht::clock_type::now();
    word sum=run<Op>(state,in,rounds);
    asm volatile("" : : "r"(sum) : "memory");
    double ns=std::chrono::duration<double,std::nano>(ht::clock_type::now()-start).count();
    require(sum==expected,"timed full-value checksum");
    if constexpr(Op==operation::update)for(unsigned b=0;b<state.data.size();++b)
        require(state.data[b]==oracle_encode<Row>(final.data[b]),"timed final byte oracle");
    auto controls=sizeof(plan<Row>);
    std::printf("%s,%u,%u,%u,%zu,%zu,%s,%u,%u,%.4f,%zu,%zu,%llu\n",Arm::name,fingerprint_bits,Row,in.active,
        in.buckets.size(),in.queries[0].size(),operation_names[unsigned(Op)],rounds,rep,
        ns/(rounds*double(in.queries[0].size())),state.data.size()*sizeof(bytes<Row>),
        std::is_same_v<Arm,fused<Row>>?std::size_t(0):controls,(unsigned long long)sum);
    std::fflush(stdout);
}
template<unsigned Row,operation Op> void paired(const input& in,unsigned rounds,unsigned repeats) {
    reference<Row> model(in);(void)run<Op>(model,in,1);word expected=run<Op>(model,in,rounds);
    for(unsigned rep=0;rep<repeats;++rep)for(unsigned j=0;j<4;++j) {
        switch((j+rep)%4) {
            case 0:trial<Row,composed<Row>,Op>(in,rounds,rep,expected,model);break;
            case 1:trial<Row,admitted<Row>,Op>(in,rounds,rep,expected,model);break;
            case 2:trial<Row,fused<Row>,Op>(in,rounds,rep,expected,model);break;
            case 3:trial<Row,split<Row>,Op>(in,rounds,rep,expected,model);break;
        }
    }
}
template<unsigned Row> void campaign(const input& in,unsigned rounds,unsigned repeats) {
    paired<Row,operation::hit>(in,rounds,repeats);
    paired<Row,operation::fingerprint_miss>(in,rounds,repeats);
    paired<Row,operation::suffix_miss>(in,rounds,repeats);
    paired<Row,operation::serial_hit>(in,rounds,repeats);
    paired<Row,operation::update>(in,rounds,repeats);
}
} // namespace rich

int main(int argc,char** argv) {
    using namespace rich;
    recipe<6>::admit();recipe<10>::admit();
    std::string_view mode=argc>1?argv[1]:"check";
    if(mode=="check") {
        correctness<6>();correctness<10>();
        // The first driver checks the actual timing loops as well as kernels.
        // A small trace is sufficient; this is not retained performance evidence.
        for(unsigned n:{0u,1u,7u,8u,15u,16u}) {
            input in(7,n,64);
            reference<6> ref6(in);reference<10> ref10(in);
            physical<6,composed<6>> c6(in);physical<6,fused<6>> f6(in);
            physical<10,composed<10>> c10(in);physical<10,fused<10>> f10(in);
            physical<6,admitted<6>> a6(in);physical<10,admitted<10>> a10(in);
            physical<6,split<6>> s6(in);physical<10,split<10>> s10(in);
            auto check=[&]<operation Op>() {
                word e6=run<Op>(ref6,in,3),e10=run<Op>(ref10,in,3);
                require(run<Op>(c6,in,3)==e6&&run<Op>(f6,in,3)==e6,"6B driver trace");
                require(run<Op>(c10,in,3)==e10&&run<Op>(f10,in,3)==e10,"10B driver trace");
                require(run<Op>(a6,in,3)==e6&&run<Op>(a10,in,3)==e10,"admitted driver trace");
                require(run<Op>(s6,in,3)==e6&&run<Op>(s10,in,3)==e10,"split driver trace");
            };
            check.template operator()<operation::hit>();check.template operator()<operation::fingerprint_miss>();
            check.template operator()<operation::suffix_miss>();check.template operator()<operation::serial_hit>();
            check.template operator()<operation::update>();
        }
        std::printf("rich composition F%u: %u fingerprint symbols, independent encoding, tails, collisions, updates, guards, and full-value traces passed\n",fingerprint_bits,fingerprint_limit);
        return 0;
    }
    require(mode=="bench","usage: rich_composition check | bench [buckets=256] [rounds=16] [repeats=8]");
    unsigned buckets=argc>2?unsigned(std::strtoul(argv[2],nullptr,10)):256;
    unsigned rounds=argc>3?unsigned(std::strtoul(argv[3],nullptr,10)):16;
    unsigned repeats=argc>4?unsigned(std::strtoul(argv[4],nullptr,10)):8;
    require(buckets&&rounds&&repeats,"positive benchmark arguments");
    std::puts("arm,fingerprint_bits,row_bytes,active,buckets,queries,operation,rounds,rep,ns_per_op,payload_bytes,shared_prepared_bytes,checksum");
    // Supplied footprint is reported, never labelled as a cache residence.
    // Serial timing includes the identical full-value consumer and trace-index dependency.
    for(unsigned active:{0u,1u,7u,8u,15u,16u}) {
        input in(buckets,active,4096);campaign<6>(in,rounds,repeats);campaign<10>(in,rounds,repeats);
    }
}
