#pragma once
// Bounded composition experiment, not a hash table or an Abseil comparison.
#include <ikea/seriespack.h>
#include <ikea/seriespack/author/native.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/native.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <span>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
using byte = std::uint8_t;
using word = std::uint64_t;

constexpr word low(unsigned n) { return n == 64 ? ~word{} : (word{1} << n) - 1; }

template<unsigned K>
[[gnu::always_inline]] inline unsigned match_ikea(const byte* p, byte wanted) {
    using F = sp::format<K, sp::geometry::local>;
    auto values = sp::native::read16<F, true>(p, F::tile_bytes, 0).v[0];
#if defined(__aarch64__)
    static constexpr byte weights[16]{1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128};
    const auto equal = vandq_u8(vceqq_u8(values, vdupq_n_u8(wanted)), vld1q_u8(weights));
    return unsigned(vaddv_u8(vget_low_u8(equal))) | (unsigned(vaddv_u8(vget_high_u8(equal))) << 8);
#elif defined(__AVX512BW__) && defined(__AVX512VL__)
    return _mm_cmpeq_epi8_mask(values, _mm_set1_epi8(wanted));
#elif defined(__AVX2__)
    return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(values, _mm_set1_epi8(wanted))));
#else
#error This experiment requires explicit NEON or AVX2/AVX-512.
#endif
}

// Matched wire/access-window control: compare the bit planes directly instead
// of materializing fingerprint bytes. It is an experimental predicate, not a
// replacement SeriesPack format. K=8 uses exactly the same explicit SIMD path.
// The admitted query must fit K bits; high query bits are not inspected here.
template<unsigned K>
[[gnu::always_inline]] inline unsigned match_planes(const byte* p, byte wanted) {
    if constexpr (K == 8) return match_ikea<K>(p, wanted);
    else {
        unsigned answer = 65535;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((answer &= ((unsigned(p[I]) | (unsigned(p[K + I]) << 8)) ^
                (0u - unsigned(((wanted >> I) & 1) == 0)))), ...);
        }(std::make_index_sequence<K>{});
        return answer & 65535;
    }
}

template<unsigned K> void encode(byte* p, const std::array<byte,16>& fp) {
    std::fill(p, p + 2 * K, byte{});
    for (unsigned i = 0; i < 16; ++i) {
        if constexpr (K == 8) p[i] = fp[i];
        else for (unsigned b = 0; b < K; ++b)
            p[(i / 8) * K + b] |= byte(((fp[i] >> b) & 1u) << (i % 8));
    }
}

template<unsigned Bytes> inline word load(const byte* p) {
    word out = 0;
    if constexpr (Bytes) std::memcpy(&out, p, Bytes);
    return out;
}

struct plans {
    tp::layout layout;
    tp::reader<8> suffix, value;
    static tp::layout make_layout(unsigned bits) {
        const unsigned bytes = (bits + 7) / 8;
        std::vector<tp::code> codes;
        for (unsigned i = 0; i < bytes; ++i)
            codes.push_back({byte(i), 0, byte(std::min(8u, bits - 8 * i))});
        for (unsigned i = 0; i < 8; ++i) codes.push_back({byte(bytes + i), 0, 8});
        auto result = tp::layout::make(bytes + 8, codes); assert(result); return *result;
    }
    static tp::reader<8> reader(const tp::layout& layout, unsigned first, unsigned count) {
        std::vector<byte> map(count);
        std::iota(map.begin(), map.end(), byte(first));
        auto result = tp::reader<8>::make(layout, map); assert(result); return *result;
    }
    explicit plans(unsigned bits) : layout(make_layout(bits)),
        suffix(reader(layout, 0, (bits + 7) / 8)),
        value(reader(layout, (bits + 7) / 8, 8)) {
        // Same shape test as native_reader::admit_transfer, once per schema.
        // There is currently no sibling author endpoint accepting a raw row.
        assert(!bits || suffix.controls().transfer.bytes == (bits + 7) / 8);
        assert(value.controls().transfer.bytes == 8);
    }
};

// Probe the existing TuplePack body behind get_transfer_unchecked, keeping its
// dynamic offset/shift/mask. This is a composition experiment using an internal
// endpoint, not a proposed second codec or a production permission bypass.
template<unsigned Bytes>
[[gnu::always_inline]] inline word admitted_transfer(const tp::reader<8>& p, const byte* row) {
    return tp::detail::read_gpr_transfer<1,Bytes>(p.controls(),
        [&](unsigned) { return row; }, 1, 0);
}

template<unsigned K, unsigned S> struct bucket {
    static constexpr unsigned suffix_bytes = (S + 7) / 8;
    static constexpr unsigned entry_bytes = suffix_bytes + 8;
    static constexpr unsigned entry_offset = 8 + 2 * K;
    static constexpr unsigned bytes = entry_offset + 16 * entry_bytes;
    // Only initialized bytes in this declared span are readable. The experiment
    // supplies stable schema/state across a bounded, nonsuspending operation.
    // Tuple: 0 generic body, 1 fixed raw-load control, 2 admitted transfer body.
    template<bool Planes, unsigned Tuple>
    [[gnu::always_inline]] static inline word get(const plans& p, const byte* data,
                                                 byte fp, word suffix) {
        const unsigned used = data[0];
        if (!used) return ~word{};
        auto candidates = (Planes ? match_planes<K>(data + 8, fp) : match_ikea<K>(data + 8, fp)) &
            ((1u << used) - 1);
        while (candidates) {
            const auto i = std::countr_zero(candidates);
            const auto* entry = data + entry_offset + i * entry_bytes;
            word actual = 0;
            if constexpr (S) {
                if constexpr (Tuple == 1) actual = load<suffix_bytes>(entry) & low(S);
                else if constexpr (Tuple == 2) actual = admitted_transfer<suffix_bytes>(p.suffix, entry);
                else actual = tp::native::read_body(p.suffix.controls(), entry);
            }
            if (actual == suffix) {
                if constexpr (Tuple == 1) return load<8>(entry + suffix_bytes);
                else if constexpr (Tuple == 2) return admitted_transfer<8>(p.value, entry);
                else return tp::native::read_body(p.value.controls(), entry);
            }
            candidates &= candidates - 1;
        }
        return ~word{};
    }
};
