#pragma once
// First complete comparative consumer. Geometry is an experimental input.
#include <ikea/seriespack/author/native.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/native.h>
#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace hashtable_spike {
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
namespace oa = orbital::allocation;
using byte = std::uint8_t;
using word = std::uint64_t;
inline void require(bool v) { if (!v) { std::fputs("hashtable invariant failed\n", stderr); std::abort(); } }
constexpr word mask(unsigned n) { return n == 64 ? ~word{} : (word{1} << n) - 1; }
template<class T> T load(const byte* p) { T v; std::memcpy(&v,p,sizeof v); return v; }
template<class T> void store(byte* p,T v) { std::memcpy(p,&v,sizeof v); }

// A reversible full-width candidate, not a cryptographic security claim.
// Seed is injected before mixing; output-only whitening would merely relabel buckets.
inline word mix(word x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline word unmix(word x) {
    x ^= (x >> 31) ^ (x >> 62); x *= 0x319642b2d24d8ec3ULL;
    x ^= (x >> 27) ^ (x >> 54); x *= 0x96de1b173f119089ULL;
    return x ^ (x >> 30) ^ (x >> 60);
}

// Width-preserving permutations: truncating a 64-bit permutation would not
// preserve a smaller key domain. Affine is a measured cost/distribution control,
// not a selected hash policy. Its odd multiplier changes with the caller seed.
template<unsigned W,bool Affine> struct permutation {
    static_assert(W>=8 && W<=64 && W%8==0);
    word seed, multiplier, inverse;
    static constexpr unsigned s1=W*30/64,s2=W*27/64,s3=W*31/64;
    static word undo_shift(word x,unsigned shift) {
        for (unsigned n=shift;n<W;n*=2) x^=x>>n;
        return x;
    }
    explicit permutation(word entropy,unsigned =0) : seed(entropy & mask(W)),
        multiplier((mix(entropy ^ 0x6d756c7469706c79ULL)|1)&mask(W)), inverse(multiplier) {
        for (unsigned i=0;i<6;++i) inverse*=2-multiplier*inverse;
        inverse &= mask(W);
    }
    [[gnu::always_inline]] word operator()(word key) const {
        word x=(key^seed)&mask(W);
        if constexpr (Affine) return (x*multiplier)&mask(W);
        x^=x>>s1; x=(x*0xbf58476d1ce4e5b9ULL)&mask(W);
        x^=x>>s2; x=(x*0x94d049bb133111ebULL)&mask(W);
        return x^(x>>s3);
    }
    word invert(word x) const {
        if constexpr (Affine) return ((x*inverse)&mask(W))^seed;
        x=undo_shift(x,s3); x=(x*0x319642b2d24d8ec3ULL)&mask(W);
        x=undo_shift(x,s2); x=(x*0x96de1b173f119089ULL)&mask(W);
        return undo_shift(x,s1)^seed;
    }
};

// Geometry-specific experiment: one-round quotienting with the same strong
// bucket function evaluated either arithmetically or from a small cold table.
// The pair (bucket,remainder) is invertible even though H itself need not be.
// Exact narrow remainders compare every stored key bit, so raw r is appropriate;
// this is not a wide-key fingerprint policy or a cryptographic claim.
struct quotient16 {
    unsigned address_bits,remainder_bits;
    word input_seed;
    std::uint32_t hash_seed;
    std::array<std::uint16_t,256> table{};
    quotient16(word seed,unsigned address)
        : address_bits(address),remainder_bits(16-address),input_seed(seed&65535),
          hash_seed(std::uint32_t(mix(seed^0x6275636b65742d68ULL))) {
        require(address>0 && address<16);
        if (remainder_bits<=8) for (unsigned r=0;r<(1u<<remainder_bits);++r) table[r]=bucket_function(r);
    }
    std::uint16_t bucket_function(word r) const {
        std::uint32_t x=std::uint32_t(r)^hash_seed;
        x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;
        return std::uint16_t(x>>(32-address_bits));
    }
    [[gnu::always_inline]] word exact(word key) const {
        const word x=(key^input_seed)&65535,r=x>>address_bits;
        return (((x&mask(address_bits))^table[r])<<remainder_bits)|r;
    }
    [[gnu::always_inline]] word operator()(word key) const {
        if (remainder_bits<=8) return exact(key);
        const word x=(key^input_seed)&65535,r=x>>address_bits;
        return (((x&mask(address_bits))^bucket_function(r))<<remainder_bits)|r;
    }
    word invert(word h) const {
        const word r=h&mask(remainder_bits);
        const word b=(h>>remainder_bits)^(remainder_bits<=8?table[r]:bucket_function(r));
        return ((r<<address_bits)|b)^input_seed;
    }
};

// Description controls are per table geometry, never per bucket.
inline word direct_read(const byte* p,unsigned bytes) {
    switch (bytes) {
    case 1: return *p;
    case 2: return load<std::uint16_t>(p);
    case 3: return load<std::uint16_t>(p) | (word(p[2])<<16);
    case 4: return load<std::uint32_t>(p);
    case 5: return load<std::uint32_t>(p) | (word(p[4])<<32);
    case 6: return load<std::uint32_t>(p) | (word(load<std::uint16_t>(p+4))<<32);
    case 7: return load<std::uint32_t>(p) | (word(load<std::uint16_t>(p+4))<<32) | (word(p[6])<<48);
    case 8: return load<word>(p);
    default: std::abort();
    }
}
inline void direct_write(byte* p,unsigned bytes,word v) {
    switch (bytes) {
#define HT_STORE(N) case N: std::memcpy(p,&v,N); break
    HT_STORE(1); HT_STORE(2); HT_STORE(3); HT_STORE(4);
    HT_STORE(5); HT_STORE(6); HT_STORE(7); HT_STORE(8);
#undef HT_STORE
    default: std::abort();
    }
}
struct field_plan {
    unsigned offset_,bytes_;
    word mask_;
    tp::reader<8> read;
    tp::writer<8> write;
    const tp::reader<8>* row_word_read_{};
    static tp::layout format(unsigned bytes, unsigned offset, unsigned bits) {
        std::vector<tp::code> codes;
        for (unsigned b=0;b<bits;b+=8)
            codes.push_back({byte(offset+b/8),0,byte(std::min(8u,bits-b))});
        if (bits==0) { bytes=1; codes.push_back({0,0,1}); }
        auto p=tp::layout::make(bytes,codes); require(bool(p)); return *p;
    }
    explicit field_plan(unsigned bytes, unsigned offset, unsigned bits)
        : offset_(offset),bytes_((bits+7)/8),mask_(mask(bits)),read([&] { auto f=format(bytes,offset,bits); std::vector<byte> m(std::max(1u,(bits+7)/8));
              std::iota(m.begin(),m.end(),byte{}); auto p=tp::reader<8>::make(f,m); require(bool(p)); return *p; }()),
          write([&] { auto f=format(bytes,offset,bits); std::vector<byte> m(std::max(1u,(bits+7)/8));
              std::iota(m.begin(),m.end(),byte{}); auto p=tp::writer<8>::make(f,m); require(bool(p)); return *p; }()) {}
    template<bool Direct> [[gnu::always_inline]] word get(const byte* p) const {
        if (!mask_) return 0;
        if constexpr (Direct) return direct_read(p+offset_,bytes_) & mask_;
        else return tp::native::read_body(read.controls(),p);
    }
    void admit_row_word(unsigned row_bytes) {
        require(offset_==0 && bytes_<=8 && row_bytes>=8);
        // A read-only view of the first eight initialized row bytes. The exact
        // field reader and writer retain their original projection and effects.
        static const auto projection=[] {
            const auto f=format(8,0,64);
            const std::array<byte,8> ranks{0,1,2,3,4,5,6,7};
            auto p=tp::reader<8>::make(f,ranks); require(bool(p)); return *p;
        }();
        const auto& t=projection.controls().transfer;
        require(t.offset==0 && t.bytes==8 && t.shift==0 && t.mask==~word{} &&
                projection.read_bytes()==255);
        row_word_read_=&projection;
    }
    template<bool Direct> [[gnu::always_inline]] word get_row_word(const byte* p) const {
        // Requires admit_row_word() and the complete initialized row window.
        // The caller projects the logical suffix from this physical word.
        if constexpr (Direct) return load<word>(p);
        else {
            const auto& controls=row_word_read_->controls();
            __builtin_assume(controls.transfer.offset==0);
            __builtin_assume(controls.transfer.bytes==8);
            __builtin_assume(controls.transfer.shift==0);
            __builtin_assume(controls.transfer.mask==~word{});
            return tp::native::read_transfer_body<1,8>(controls,[&](unsigned) { return p; },1);
        }
    }
    template<unsigned Bytes> void admit_full_value() const {
        static_assert(Bytes>0 && Bytes<=8);
        const auto& t=read.controls().transfer;
        require(t.bytes==Bytes && t.shift==0 && t.mask==mask(Bytes*8));
        const auto& w=write.controls();
        require(w.transfer.bytes==Bytes && w.transfer.shift==0 && w.transfer.mask==mask(Bytes*8) && w.word.selected==Bytes);
    }
    template<bool Direct,unsigned Bytes> [[gnu::always_inline]] word get_full_value(const byte* p) const {
        static_assert(Bytes>0 && Bytes<=8);
        // Caller selected this family after admit_full_value<Bytes>(); retain
        // that semantic fact through the generic native transfer implementation.
        if constexpr (Direct) return direct_read(p+offset_,Bytes);
        else {
            const auto& controls=read.controls();
            __builtin_assume(controls.transfer.bytes==Bytes);
            __builtin_assume(controls.transfer.shift==0);
            constexpr word full_mask=mask(Bytes*8);
            __builtin_assume(controls.transfer.mask==full_mask);
            return tp::native::read_transfer_body<1,Bytes>(controls,[&](unsigned) { return p; },1);
        }
    }
    template<bool Direct,unsigned Bytes> [[gnu::always_inline]] void set_full_value(byte* p,word v) const {
        static_assert(Bytes>0 && Bytes<=8);
        if constexpr (Direct) direct_write(p+offset_,Bytes,v);
        else {
            const auto& controls=write.controls();
            constexpr word full_mask=mask(Bytes*8);
            __builtin_assume(controls.word.selected==Bytes);
            __builtin_assume(controls.transfer.bytes==Bytes);
            __builtin_assume(controls.transfer.shift==0);
            __builtin_assume(controls.transfer.mask==full_mask);
            tp::native::write_body(controls,p,v);
        }
    }
    template<bool Direct> [[gnu::always_inline]] word get_word(const byte* p) const {
        // This endpoint is selected only by the fixed word-value family.
        if constexpr (Direct) return load<word>(p+offset_);
        else return tp::native::read_body(read.controls(),p);
    }
    template<bool Direct> void set(byte* p,word v) const {
        if (!mask_) return;
        if constexpr (Direct) {
            p+=offset_;
            const byte high=byte(mask_>>((bytes_-1)*8));
            v |= word(p[bytes_-1] & byte(~high))<<((bytes_-1)*8);
            direct_write(p,bytes_,v);
        } else tp::native::write_body(write.controls(),p,v);
    }
};

template<unsigned KeyBits=64,unsigned ValueBits=64> struct geometry {
    unsigned slots, suffix_bits, suffix_bytes, fingerprint_bits, lanes, header, stride, entry_start,value_start;
    static constexpr unsigned value_bytes=(ValueBits+7)/8;
    bool planes;
    field_plan suffix, value;
    geometry(unsigned n,unsigned a,bool split,unsigned rounding,bool secondary,unsigned value_alignment=1)
        : slots(n),suffix_bits(KeyBits>a+8?KeyBits-a-8:0),suffix_bytes((suffix_bits+7)/8),fingerprint_bits(std::min(8u,KeyBits-a)),lanes((n+7)/8*8),
          header(secondary?12:8),stride(0),entry_start(header+lanes),value_start(split?(entry_start+n*suffix_bytes+value_alignment-1)/value_alignment*value_alignment:entry_start),planes(split),
          suffix(split?suffix_bytes:suffix_bytes+value_bytes,0,suffix_bits),
          value(split?value_bytes:suffix_bytes+value_bytes,split?0:suffix_bytes,ValueBits) {
        require(n>=1 && n<=32 && a<KeyBits && rounding>0);
        unsigned useful=split?value_start+n*value_bytes:entry_start+n*(suffix_bytes+value_bytes);
        // A set has no following value to complete the final suffix word.
        // Initialize and charge its exact tail window, before applying padding.
        if constexpr (ValueBits==0) if(!secondary) useful+=8-suffix_bytes;
        stride=(useful+rounding-1)/rounding*rounding;
        if constexpr (ValueBits && ValueBits%8==0) value.template admit_full_value<value_bytes>();
    }
    void admit_row_suffix_window() {
        require(!planes);
        require(entry_start+(slots-1)*(suffix_bytes+value_bytes)+8<=stride);
        suffix.admit_row_word(std::max(8u,suffix_bytes+value_bytes));
    }
    void admit_plane_suffix_window() {
        require(planes && value_start+slots*value_bytes>=entry_start+(slots-1)*suffix_bytes+8);
        // The last suffix's physical word may extend into initialized padding
        // or the value plane. Only its logical suffix bits are compared.
        suffix.admit_row_word(8);
    }
    const byte* suffix_at(const byte* b,unsigned i) const {
        return b+entry_start+i*(planes?suffix_bytes:suffix_bytes+value_bytes);
    }
    const byte* value_at(const byte* b,unsigned i) const {
        return planes?b+value_start+i*value_bytes:suffix_at(b,i);
    }
    byte* suffix_at(byte* b,unsigned i) const { return const_cast<byte*>(suffix_at(static_cast<const byte*>(b),i)); }
    byte* value_at(byte* b,unsigned i) const { return const_cast<byte*>(value_at(static_cast<const byte*>(b),i)); }
    template<bool Direct> [[gnu::always_inline]] word get_value(const byte* b,unsigned i) const {
        if constexpr (ValueBits && ValueBits%8==0) return value.template get_full_value<Direct,value_bytes>(value_at(b,i));
        else return value.template get<Direct>(value_at(b,i));
    }
    template<bool Direct> [[gnu::always_inline]] void set_value(byte* b,unsigned i,word v) const {
        if constexpr (ValueBits && ValueBits%8==0) value.template set_full_value<Direct,value_bytes>(value_at(b,i),v);
        else value.template set<Direct>(value_at(b,i),v);
    }
    byte fingerprint(word h) const { return byte((h>>suffix_bits)&mask(fingerprint_bits)); }
    [[gnu::always_inline]] unsigned candidates(const byte* b,byte fp) const {
        const byte* p=b+header;
        unsigned out=0;
        // SeriesPack Local<8> is the byte lane format. The eight-lane tail uses
        // an exact-width load; an occupancy mask never authorizes an overread.
        for (unsigned start=0;start<lanes;start+=16) {
            unsigned part;
            if (lanes-start>=16) {
                const auto v=sp::native::read16<sp::format<8>,true>(p+start,8,0).v[0];
#if defined(__aarch64__)
                const auto eq=vceqq_u8(v,vdupq_n_u8(fp));
                const word nibbles=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq),4)),0);
                part=0;
                // Sparse nibble mask -> dense slot mask, explicit bit compaction.
                word z=nibbles & 0x1111111111111111ULL;
                z=(z | (z>>3)) & 0x0303030303030303ULL;
                z=(z | (z>>6)) & 0x000f000f000f000fULL;
                z=(z | (z>>12)) & 0x000000ff000000ffULL;
                part=unsigned((z | (z>>24)) & 65535);
#elif defined(__AVX512BW__) && defined(__AVX512VL__)
                part=_mm_cmpeq_epi8_mask(v,_mm_set1_epi8(fp));
#elif defined(__AVX2__)
                part=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(v,_mm_set1_epi8(fp))));
#else
#error Requires NEON or AVX2
#endif
            } else {
#if defined(__aarch64__)
                const auto eq=vceq_u8(vld1_u8(p+start),vdup_n_u8(fp));
                const byte weights[8]{1,2,4,8,16,32,64,128};
                part=vaddv_u8(vand_u8(eq,vld1_u8(weights)));
#elif defined(__AVX2__)
                const auto v=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p+start));
                part=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(v,_mm_set1_epi8(fp)))) & 255;
#endif
            }
            out |= part<<start;
        }
        // Inactive prefix-tail lanes contain 255. Only that query needs count.
        return fp==255?out & unsigned(mask(b[0])):out;
    }
    template<bool Direct> [[gnu::always_inline]] int find(const byte* b,word h) const {
#if defined(__aarch64__)
        // Keep NEON's native sparse mask. Only the first candidate index is
        // needed; converting it to a dense movemask adds a serial bit shuffle.
        const byte needle=fingerprint(h);
        for (unsigned base=0;base<lanes;base+=16) {
            const byte* p=b+header+base;
            const auto bytes=lanes-base>=16?
                sp::native::read16<sp::format<8>,true>(p,8,0).v[0]:
                vcombine_u8(vld1_u8(p),vdup_n_u8(0));
            const auto equal=vceqq_u8(bytes,vdupq_n_u8(needle));
            word c=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0) & 0x1111111111111111ULL;
            while (c) {
                unsigned i=base+std::countr_zero(c)/4;
                if (needle==255 && i>=b[0]) return -1;
                if (i>=lanes) return -1; // exact eight-byte tail is zero-filled in its upper half
                if (suffix.template get<Direct>(suffix_at(b,i))==(h & mask(suffix_bits))) return int(i);
                c &= c-1;
            }
        }
#else
        auto c=candidates(b,fingerprint(h));
        while (c) {
            unsigned i=std::countr_zero(c);
            if (suffix.template get<Direct>(suffix_at(b,i))==(h & mask(suffix_bits))) return int(i);
            c &= c-1;
        }
#endif
        return -1;
    }
    template<unsigned N> [[gnu::always_inline]] int find_exact(const byte* b,word h) const {
        static_assert(N>0 && N<=16);
        // Cold admission proves an exact remainder below 128 and an inactive
        // value of 255. Keep the compare's native mask, with no lane numbering
        // or occupancy dependency. Padding lanes also contain 255.
        const byte needle=fingerprint(h)&127;
#if defined(__aarch64__)
        if constexpr (N<=8) {
            const auto eq=vceq_u8(vld1_u8(b+header),vdup_n_u8(needle));
            const word c=vget_lane_u64(vreinterpret_u64_u8(eq),0);
            return c?int(std::countr_zero(c)/8):-1;
        } else {
            const auto bytes=sp::native::read16<sp::format<8>,true>(b+header,8,0).v[0];
            const auto eq=vceqq_u8(bytes,vdupq_n_u8(needle));
            const word c=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq),4)),0);
            return c?int(std::countr_zero(c)/4):-1;
        }
#else
        const auto bytes=N>8?sp::native::read16<sp::format<8>,true>(b+header,8,0).v[0]:
            _mm_loadl_epi64(reinterpret_cast<const __m128i*>(b+header));
        unsigned c=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(bytes,_mm_set1_epi8(needle))));
        if constexpr (N<=8) c &= 255; // The exact-width load zeroes its upper half.
        return c?int(std::countr_zero(c)):-1;
#endif
    }
    template<bool Direct> void append(byte* b,word h,word v) const {
        unsigned i=b[0]; require(i<slots);
        sp::detail::put_payload<sp::format<8>>(b+header+(i/8)*8,i%8,fingerprint(h));
        suffix.template set<Direct>(suffix_at(b,i),h & mask(suffix_bits)); set_value<Direct>(b,i,v); ++b[0];
    }
    [[gnu::always_inline]] word candidates16(const byte* b,word h) const {
        const auto bytes=sp::native::read16<sp::format<8>,true>(b+header,8,0).v[0];
#if defined(__aarch64__)
        const auto equal=vceqq_u8(bytes,vdupq_n_u8(fingerprint(h)));
        word c=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0) & 0x1111111111111111ULL;

#elif defined(__AVX512BW__) && defined(__AVX512VL__)
        word c=_mm_cmpeq_epi8_mask(bytes,_mm_set1_epi8(fingerprint(h)));

#elif defined(__AVX2__)
        word c=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(bytes,_mm_set1_epi8(fingerprint(h)))));

#endif
        if (fingerprint(h)==255) {
#if defined(__aarch64__)
            c &= mask(b[0]*4);
#else
            c &= mask(b[0]);
#endif
        }
        return c;
    }
    [[gnu::always_inline]] word candidates8(const byte* b,word h) const {
        const auto fp=fingerprint(h);
#if defined(__aarch64__)
        word c=vget_lane_u64(vreinterpret_u64_u8(vceq_u8(vld1_u8(b+header),vdup_n_u8(fp))),0)&0x0101010101010101ULL;
        if (fp==255) c&=mask(b[0]*8);
#else
        word c=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(b+header)),_mm_set1_epi8(char(fp)))))&255;
        if (fp==255) c&=mask(b[0]);
#endif
        return c;
    }
    template<bool Direct,bool Planes=false> [[gnu::always_inline]] int find8(const byte* b,word h,const byte* entries) const {
        auto c=candidates8(b,h);
        while (c) {
#if defined(__aarch64__)
            const unsigned i=std::countr_zero(c)/8;
#else
            const unsigned i=std::countr_zero(c);
#endif
            c &= c-1;
            const auto* row=entries+i*(Planes?suffix_bytes:suffix_bytes+value_bytes);
            if ((suffix.template get_row_word<Direct>(row)&mask(suffix_bits))==(h&mask(suffix_bits))) return int(i);
        }
        return -1;
    }
    template<bool Direct,bool Planes=false> [[gnu::always_inline]] int find8(const byte* b,word h) const {
        return find8<Direct,Planes>(b,h,b+entry_start);
    }
    template<bool Direct,bool Planes,bool Wide=false> [[gnu::always_inline]] int find16(const byte* b,word h,const byte* entries) const {
        static_assert(!Wide || !Planes);
        word c=candidates16(b,h);
#if defined(__aarch64__)
        constexpr unsigned spacing=4;
#else
        constexpr unsigned spacing=1;
#endif
        while (c) {
            unsigned i=std::countr_zero(c)/spacing;
            const auto* p=entries+i*(Planes?suffix_bytes:suffix_bytes+value_bytes);
            word actual;
            // Whole-word load is within the complete row; this fused control
            // may read the following value bytes, then masks them out.
            if constexpr (Wide) actual=suffix.template get_row_word<Direct>(p) & mask(suffix_bits);
            else if constexpr (Direct && !Planes) actual=load<word>(p) & mask(suffix_bits);
            else actual=suffix.template get<Direct>(p);
            if (actual==(h & mask(suffix_bits))) return int(i);
            c &= c-1;
        }
        return -1;
    }
    template<bool Direct,bool Planes,bool Wide=false> [[gnu::always_inline]] int find16(const byte* b,word h) const {
        return find16<Direct,Planes,Wide>(b,h,b+entry_start);
    }
    template<bool Direct> void erase(byte* b,unsigned i) const {
        unsigned last=--b[0];
        if (i!=last) {
            b[header+i]=b[header+last];
            suffix.template set<Direct>(suffix_at(b,i),suffix.template get<Direct>(suffix_at(b,last)));
            set_value<Direct>(b,i,get_value<Direct>(b,last));
        }
        b[header+last]=255;
    }
};

struct buffer_entry { word key,value; };
struct overflow_buffer { oa::Allocation storage; std::size_t used{}; };
struct table_counts { std::size_t primary{},secondary{},overflow{},buffers{}; };
struct memory_counts { std::size_t payload{},capacity{},catalog{},plans{}; oa::Statistics pool{}; };

// Bound=0 is the generic consumer; 1/2 admit one16-lane FP group, allowing
// 9..16 logical slots. Physical SIMD lanes do not fix logical bucket capacity.
template<bool Direct,unsigned Bound=0,unsigned KeyBits=64,unsigned ValueBits=64,bool Affine=false,unsigned BoundSlots=0,unsigned BoundStride=0,bool QuotientTable=false,bool SplitPrimary=false,unsigned EarlyHint=0> class basic_map {
    static_assert(!QuotientTable || (KeyBits==16 && !Affine));
    static_assert(!SplitPrimary || (Bound==1 || Bound==4));
    static_assert(!SplitPrimary || ValueBits==64);
    static_assert(!SplitPrimary || !EarlyHint);
    static_assert(EarlyHint<=2 && (!EarlyHint || (Bound==1 || Bound==4 || Bound==5)));
    oa::Pool* pool_;
    unsigned a_,b_,rounding_;
    using hash_type=std::conditional_t<QuotientTable,quotient16,permutation<KeyBits,Affine>>;
    hash_type hash1_,hash2_;
    geometry<KeyBits,ValueBits> p_,q_;
    oa::Allocation primary_,secondary_;
    std::vector<overflow_buffer> buffers_;
    std::size_t size_{};
    static unsigned address_bits(std::size_t n,unsigned slots) {
        // Start near 80% of primary capacity, then round bucket count to pow2.
        return std::bit_width(std::max<std::size_t>(1,(n*5+slots*4-1)/(slots*4))-1);
    }
    byte* bucket(const oa::Allocation& a,const geometry<KeyBits,ValueBits>& g,word h,unsigned bits) const {
        return reinterpret_cast<byte*>(a.address)+(h>>(KeyBits-bits))*g.stride;
    }
    [[gnu::always_inline]] byte* primary_bucket(word h) const {
        if constexpr (SplitPrimary) return reinterpret_cast<byte*>(primary_.address)+(h>>(KeyBits-a_))*p_.entry_start;
        else if constexpr (BoundStride) return reinterpret_cast<byte*>(primary_.address)+(h>>(KeyBits-a_))*BoundStride;
        else return bucket(primary_,p_,h,a_);
    }
    // Both arrangements use one Orbital allocation and the same number of
    // bytes. Splitting transposes all headers/fingerprints ahead of all rows;
    // it adds neither a pointer per bucket nor a shadow copy of metadata.
    [[gnu::always_inline]] byte* primary_entries(word h,const byte* pb) const {
        if constexpr (SplitPrimary) return reinterpret_cast<byte*>(primary_.address)+
            (std::size_t{1}<<a_)*p_.entry_start+(h>>(KeyBits-a_))*(p_.stride-p_.entry_start);
        else return const_cast<byte*>(pb)+p_.entry_start;
    }
    [[gnu::always_inline]] byte* primary_row(word h,const byte* pb,unsigned i) const {
        return primary_entries(h,pb)+i*(p_.suffix_bytes+geometry<KeyBits,ValueBits>::value_bytes);
    }
    void initialize_primary() {
        if constexpr (SplitPrimary) {
            for (std::size_t i=0;i<(std::size_t{1}<<a_);++i)
                std::memset(reinterpret_cast<byte*>(primary_.address)+i*p_.entry_start+p_.header,255,p_.lanes);
        } else initialize_fingerprints(primary_,p_,a_);
    }
    [[gnu::always_inline]] void primary_assign(byte* pb,word h,unsigned i,word v) {
        if constexpr (SplitPrimary) p_.value.template set_full_value<Direct,8>(primary_row(h,pb,i),v);
        else p_.template set_value<Direct>(pb,i,v);
    }
    [[gnu::always_inline]] void primary_append(byte* pb,word h,word v) {
        if constexpr (SplitPrimary) {
            const unsigned i=pb[0];require(i<p_.slots);
            sp::detail::put_payload<sp::format<8>>(pb+p_.header+(i/8)*8,i%8,p_.fingerprint(h));
            p_.suffix.template set<Direct>(primary_row(h,pb,i),h&mask(p_.suffix_bits));
            primary_assign(pb,h,i,v);++pb[0];
        } else p_.template append<Direct>(pb,h,v);
    }
    [[gnu::always_inline]] void primary_erase(byte* pb,word h,unsigned i) {
        if constexpr (SplitPrimary) {
            const unsigned last=--pb[0];
            if (i!=last) {
                pb[p_.header+i]=pb[p_.header+last];
                const byte* old=primary_row(h,pb,last);
                p_.suffix.template set<Direct>(primary_row(h,pb,i),p_.suffix.template get<Direct>(old));
                primary_assign(pb,h,i,p_.value.template get_full_value<Direct,8>(old));
            }
            pb[p_.header+last]=255;
        } else p_.template erase<Direct>(pb,i);
    }
    [[gnu::always_inline]] word primary_hash(word key) const {
        if constexpr (QuotientTable && Bound==3) return hash1_.exact(key);
        else return hash1_(key);
    }
    static std::uint32_t descendants(const byte* b) { return load<std::uint32_t>(b+4); }
    static void delta(byte* b,int n) {
        auto v=descendants(b); require(n>0?v<UINT32_MAX:v>0); store(b+4,std::uint32_t(v+n));
    }
    oa::Allocation alloc(std::size_t bytes) {
        auto a=oa::allocate(*pool_,{bytes,0,64,0,oa::Initialization::zero}); require(bool(a)); return std::move(*a);
    }
    void initialize_fingerprints(oa::Allocation& a,const geometry<KeyBits,ValueBits>& g,unsigned bits) {
        for (std::size_t i=0;i<(std::size_t{1}<<bits);++i)
            std::memset(reinterpret_cast<byte*>(a.address)+i*g.stride+g.header,255,g.lanes);
    }
    buffer_entry* entries(overflow_buffer& b) const { return reinterpret_cast<buffer_entry*>(b.storage.address); }
    const buffer_entry* entries(const overflow_buffer& b) const { return reinterpret_cast<const buffer_entry*>(b.storage.address); }
    overflow_buffer* chain(byte* qb) {
        auto id=load<std::uint32_t>(qb+8); return id?&buffers_[id-1]:nullptr;
    }
    const overflow_buffer* chain(const byte* qb) const {
        auto id=load<std::uint32_t>(qb+8); return id?&buffers_[id-1]:nullptr;
    }
public:
    static constexpr unsigned key_bits=KeyBits,value_bits=ValueBits;
    basic_map(oa::Pool& pool,std::size_t n,unsigned slots,bool planes,unsigned rounding,word seed)
        : pool_(&pool),a_(std::max(1u,address_bits(n,slots))),b_(std::max(1u,a_>4?a_-4:1u)),rounding_(rounding),
          hash1_(seed,a_),hash2_(mix(seed^0x7365636f6e646172ULL),b_),
          p_(slots,a_,planes,rounding,false,Bound==5?64:1),q_(slots,b_,planes,rounding,true),
          primary_(alloc((std::size_t{1}<<a_)*p_.stride)),
          secondary_(alloc((std::size_t{1}<<b_)*q_.stride)) {
        if constexpr (Bound==1 || Bound==2) {
            static_assert(KeyBits==64 && (ValueBits==64 || (Bound==1 && ValueBits==0)));
            require(slots>=9 && slots<=16 && planes==(Bound==2));
        }
        if constexpr (Bound==1 || Bound==4) p_.admit_row_suffix_window();
        if constexpr (Bound==4) { static_assert(KeyBits==64 && (ValueBits==64 || ValueBits==0));require(slots>=1 && slots<=8 && !planes); }
        if constexpr (Bound==5) {
            static_assert(KeyBits==64 && ValueBits==64);require(slots==8 && planes);
            require(p_.stride%64==0);
            p_.admit_plane_suffix_window();
        }
        if constexpr (Bound==3) {
            static_assert(ValueBits>0 && ValueBits%8==0);
            require(p_.suffix_bits==0 && p_.fingerprint_bits<8 && slots==BoundSlots);
            p_.value.template admit_full_value<ValueBits/8>();
        }
        if constexpr (BoundStride) require(p_.stride==BoundStride);
        initialize_primary(); initialize_fingerprints(secondary_,q_,b_);
    }
    std::size_t size() const { return size_; }
    [[gnu::always_inline]] int primary_find(const byte* pb,word h) const {
        if constexpr (Bound==3) return p_.template find_exact<BoundSlots>(pb,h);
        else if constexpr (Bound==4) return p_.template find8<Direct>(pb,h,primary_entries(h,pb));
        else if constexpr (Bound==5) return p_.template find8<Direct,true>(pb,h,primary_entries(h,pb));
        else if constexpr (Bound) return p_.template find16<Direct,Bound==2,Bound==1>(pb,h,primary_entries(h,pb));
        else return p_.template find<Direct>(pb,h);
    }
    [[gnu::always_inline]] bool get(word key,word& out) const {
        word h=primary_hash(key); auto* pb=primary_bucket(h);
        // Mechanism control: fixed native L1 read hint, not a production policy.
        // A selected implementation must bind Loom's caller heat/pressure policy.
        // Every target is in initialized primary storage, even on a miss.
        if constexpr (EarlyHint==1) {
            const auto* first=Bound==5?pb+p_.value_start:primary_entries(h,pb);
            __builtin_prefetch(first,0,3);
        }
        if constexpr (EarlyHint==2) {
            __builtin_prefetch(pb,0,3);
            // One address per touched64B line, all inside this actual window.
            // 128B-line targets still pay duplicates in this fixed64B control;
            // final caller binding must use Loom's actual geometry/policy.
            const auto first=reinterpret_cast<std::uintptr_t>(pb),last=first+p_.stride-1;
            for (auto next=(first&~std::uintptr_t{63})+64;next<=last;next+=64)
                __builtin_prefetch(reinterpret_cast<const void*>(next),0,3);
        }
        int i=primary_find(pb,h);
        if (i>=0) {
            if constexpr (ValueBits==0) out=0;
            else if constexpr (Bound==3) {
                out=p_.value.template get_full_value<Direct,ValueBits/8>(pb+p_.entry_start+unsigned(i)*(ValueBits/8));
            } else if constexpr (Bound==1 || Bound==2 || Bound==4 || Bound==5) {
                const byte* v=(Bound==2 || Bound==5)?pb+p_.value_start+unsigned(i)*8:
                    primary_row(h,pb,unsigned(i));
                out=p_.value.template get_full_value<Direct,8>(v);
            } else out=p_.template get_value<Direct>(pb,unsigned(i));
            return true;
        }
        if (!descendants(pb)) return false;
        auto result=get_downstream(key);
        if (result.found) out=result.value;
        return result.found;
    }
    // Stable read epoch: caller retains this map, its geometry and allocations
    // unchanged until every staged probe completes. No ownership moves here.
    struct primary_probe { word key,hash; const byte* bucket; };
    [[gnu::always_inline]] primary_probe prepare_primary(word key) const {
        const word h=primary_hash(key); return {key,h,primary_bucket(h)};
    }
    template<class Emit> [[gnu::always_inline]] void primary_window(const primary_probe& p,Emit&& emit) const {
        static_assert(Bound==1 || Bound==4); emit(p.bucket,p_.header+(Bound==4?8:16));
    }
    template<class Emit> [[gnu::always_inline]] void primary_bucket_window(const primary_probe& p,Emit&& emit) const {
        static_assert(Bound==1 || Bound==4);
        if constexpr (SplitPrimary) {
            emit(p.bucket,p_.entry_start);
            emit(primary_entries(p.hash,p.bucket),p_.stride-p_.entry_start);
        } else emit(p.bucket,p_.stride);
    }
    [[gnu::always_inline]] word match_primary(const primary_probe& p) const {
        static_assert(Bound==1 || Bound==4);
        if constexpr (Bound==4) return p_.candidates8(p.bucket,p.hash);
        else return p_.candidates16(p.bucket,p.hash);
    }
    template<class Emit> [[gnu::always_inline]] void candidate_windows(const primary_probe& p,word candidates,Emit&& emit) const {
        static_assert(Bound==1 || Bound==4);
        while (candidates) {
            const unsigned i=std::countr_zero(candidates)/candidate_spacing;
            if constexpr (ValueBits==0) emit(primary_row(p.hash,p.bucket,i),8);
            else emit(primary_row(p.hash,p.bucket,i),p_.suffix_bytes+geometry<KeyBits,ValueBits>::value_bytes);
            candidates &= candidates-1;
        }
    }
    [[gnu::always_inline]] bool finish_primary(const primary_probe& p,word candidates,word& out) const {
        static_assert(Bound==1 || Bound==4);
        while (candidates) {
            const unsigned i=std::countr_zero(candidates)/candidate_spacing;
            const byte* row=primary_row(p.hash,p.bucket,i);
            const word suffix=p_.suffix.template get_row_word<Direct>(row)&mask(p_.suffix_bits);
            if (suffix==(p.hash&mask(p_.suffix_bits))) {
                if constexpr (ValueBits==0) out=0;
                else out=p_.value.template get_full_value<Direct,8>(row);
                return true;
            }
            candidates &= candidates-1;
        }
        if (!descendants(p.bucket)) return false;
        const auto result=get_downstream(p.key);
        if (result.found) out=result.value;
        return result.found;
    }
#if defined(__aarch64__)
    static constexpr unsigned candidate_spacing=Bound==4?8:4;
#else
    static constexpr unsigned candidate_spacing=1;
#endif
    struct lookup_result { word value; bool found; };
    // No mutation, diagnostics or escaping output reference: callers can retain
    // immutable execution facts across this rare branch. Corrupt metadata is
    // outside the admitted lookup contract; audit() checks it independently.
    [[gnu::noinline,gnu::pure]] lookup_result get_downstream(word key) const {
        word j=hash2_(key); auto* qb=bucket(secondary_,q_,j,b_);
        int i=q_.template find<Direct>(qb,j);
        if (i>=0) { return {q_.template get_value<Direct>(qb,unsigned(i)),true}; }
        if (!descendants(qb)) return {0,false};
        auto* c=chain(qb); if (!c) std::unreachable();
        for (std::size_t k=0;k<c->used;++k) if (entries(*c)[k].key==key) { return {entries(*c)[k].value,true}; }
        return {0,false};
    }
    [[gnu::always_inline]] bool insert_or_assign(word key,word value) {
        word h=primary_hash(key); auto* pb=primary_bucket(h);
        int i=primary_find(pb,h);
        if (i>=0) { primary_assign(pb,h,unsigned(i),value); return false; }
        // A primary vacancy alone is insufficient: an older copy may reside
        // downstream. Only zero descendants authorizes this append shortcut.
        if (!descendants(pb) && pb[0]<p_.slots) {
            primary_append(pb,h,value); ++size_; return true;
        }
        return insert_missing(key,value,h,pb);
    }
    [[gnu::noinline]] bool insert_missing(word key,word value,word h,byte* pb) {
        int i;
        word j=0; byte* qb=nullptr;
        if (descendants(pb)) {
            j=hash2_(key); qb=bucket(secondary_,q_,j,b_); i=q_.template find<Direct>(qb,j);
            if (i>=0) { q_.template set_value<Direct>(qb,unsigned(i),value); return false; }
            if (descendants(qb)) {
                auto* c=chain(qb); require(c!=nullptr);
                for (std::size_t k=0;k<c->used;++k) if (entries(*c)[k].key==key) { entries(*c)[k].value=value; return false; }
            }
        }
        if (pb[0]<p_.slots) primary_append(pb,h,value);
        else {
            if (!qb) { j=hash2_(key); qb=bucket(secondary_,q_,j,b_); }
            if (qb[0]<q_.slots) q_.template append<Direct>(qb,j,value);
            else {
                auto* c=chain(qb);
                if (!c) {
                    require(buffers_.size()<UINT32_MAX); buffers_.push_back({alloc(4*sizeof(buffer_entry)),0});
                    store(qb+8,std::uint32_t(buffers_.size())); c=&buffers_.back();
                }
                if (c->used*sizeof(buffer_entry)==c->storage.size) {
                    auto r=oa::grow(c->storage,{c->storage.size*2,0,64,0}); require(bool(r));
                }
                // Fresh body bytes are initialized before any read.
                entries(*c)[c->used++]={key,value}; delta(qb,1);
            }
            delta(pb,1);
        }
        ++size_; return true;
    }
    [[gnu::always_inline]] bool erase(word key) {
        word h=primary_hash(key); auto* pb=primary_bucket(h);
        int i=primary_find(pb,h);
        if (i>=0) { primary_erase(pb,h,unsigned(i)); --size_; return true; }
        if (!descendants(pb)) return false;
        return erase_missing(key,pb);
    }
    [[gnu::noinline]] bool erase_missing(word key,byte* pb) {
        word j=hash2_(key); auto* qb=bucket(secondary_,q_,j,b_); int i=q_.template find<Direct>(qb,j);
        if (i>=0) { q_.template erase<Direct>(qb,unsigned(i)); delta(pb,-1); --size_; return true; }
        if (!descendants(qb)) return false;
        auto* c=chain(qb); require(c!=nullptr);
        for (std::size_t k=0;k<c->used;++k) if (entries(*c)[k].key==key) {
            entries(*c)[k]=entries(*c)[--c->used]; delta(qb,-1); delta(pb,-1); --size_; return true;
        }
        return false;
    }
    void clear() {
        std::memset(primary_.address,0,primary_.size); std::memset(secondary_.address,0,secondary_.size);
        initialize_primary(); initialize_fingerprints(secondary_,q_,b_);
        buffers_.clear(); size_=0;
    }
    template<class F> void each(F&& f) const {
        auto visit=[&](const oa::Allocation& a,const geometry<KeyBits,ValueBits>& g,unsigned bits,const auto& hash) {
            for (std::size_t b=0;b<(std::size_t{1}<<bits);++b) {
                auto* p=reinterpret_cast<const byte*>(a.address)+b*g.stride;
                for (unsigned i=0;i<p[0];++i) {
                    word h=(word(b)<<(KeyBits-bits)) | (word(p[g.header+i])<<g.suffix_bits) | g.suffix.template get<Direct>(g.suffix_at(p,i));
                    f(hash.invert(h),g.template get_value<Direct>(p,i));
                }
            }
        };
        if constexpr (SplitPrimary) {
            for (std::size_t b=0;b<(std::size_t{1}<<a_);++b) {
                const word prefix=word(b)<<(KeyBits-a_);
                const auto* pb=primary_bucket(prefix);
                for (unsigned i=0;i<pb[0];++i) {
                    const auto* row=primary_row(prefix,pb,i);
                    const word h=prefix|(word(pb[p_.header+i])<<p_.suffix_bits)|p_.suffix.template get<Direct>(row);
                    f(hash1_.invert(h),p_.value.template get_full_value<Direct,8>(row));
                }
            }
        } else visit(primary_,p_,a_,hash1_);
        visit(secondary_,q_,b_,hash2_);
        for (const auto& b:buffers_) for (std::size_t i=0;i<b.used;++i) f(entries(b)[i].key,entries(b)[i].value);
    }
    // Caller supplies fresh entropy; build candidate completely before install.
    void rebuild(std::size_t n,word seed) {
        basic_map next(*pool_,n,p_.slots,p_.planes,rounding_,seed);
        each([&](word k,word v){ require(next.insert_or_assign(k,v)); });
        *this=std::move(next);
    }
    table_counts counts() const {
        table_counts c;
        for (std::size_t i=0;i<(std::size_t{1}<<a_);++i) c.primary+=primary_bucket(word(i)<<(KeyBits-a_))[0];
        for (std::size_t i=0;i<(std::size_t{1}<<b_);++i) c.secondary+=byte(secondary_.address[i*q_.stride]);
        for (const auto& b:buffers_) c.overflow+=b.used;
        c.buffers=buffers_.size(); return c;
    }
    void audit() const {
        std::vector<std::uint32_t> pc(std::size_t{1}<<a_),qc(std::size_t{1}<<b_);
        for (std::size_t b=0;b<qc.size();++b) {
            auto* p=reinterpret_cast<const byte*>(secondary_.address)+b*q_.stride;
            require(p[0]<=q_.slots);
            for (unsigned i=p[0];i<q_.lanes;++i) require(p[q_.header+i]==255);
            for (unsigned i=0;i<p[0];++i) {
                word h=(word(b)<<(KeyBits-b_)) | (word(p[q_.header+i])<<q_.suffix_bits) | q_.suffix.template get<Direct>(q_.suffix_at(p,i));
                word key=hash2_.invert(h); ++pc[hash1_(key)>>(KeyBits-a_)];
            }
            if (const auto* c=chain(p)) for (std::size_t i=0;i<c->used;++i) {
                word key=entries(*c)[i].key;
                require((hash2_(key)>>(KeyBits-b_))==b);
                ++pc[hash1_(key)>>(KeyBits-a_)]; ++qc[b];
            }
            require(qc[b]==descendants(p));
        }
        for (std::size_t b=0;b<pc.size();++b) {
            auto* p=primary_bucket(word(b)<<(KeyBits-a_));
            require(p[0]<=p_.slots && descendants(p)==pc[b]);
            for (unsigned i=p[0];i<p_.lanes;++i) require(p[p_.header+i]==255);
        }
        auto c=counts(); require(c.primary+c.secondary+c.overflow==size_);
    }
    memory_counts memory() const {
        // Charge the shared read-only row projection in each isolated table's
        // control total. Simultaneous tables share those bytes; this is not RSS.
        memory_counts m{primary_.size+secondary_.size,oa::capacity(primary_)+oa::capacity(secondary_),buffers_.capacity()*sizeof(overflow_buffer),sizeof(*this)+((Bound==1 || Bound==4 || Bound==5)?sizeof(tp::reader<8>):0),oa::statistics(*pool_)};
        for (const auto& b:buffers_) { m.payload+=b.storage.size; m.capacity+=oa::capacity(b.storage); }
        return m;
    }
    unsigned primary_bits() const { return a_; }
    std::span<const byte> primary_storage() const {
        return {reinterpret_cast<const byte*>(primary_.address),primary_.size};
    }
    unsigned primary_stride() const { return p_.stride; }
    unsigned secondary_stride() const { return q_.stride; }
};
using packed_map=basic_map<false>;
using direct_map=basic_map<true>;
} // namespace hashtable_spike
