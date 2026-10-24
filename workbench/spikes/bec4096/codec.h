#pragma once
// Inline bodies behind bec4096.h, visible to consumers that compose them.
#include "native.h"

namespace bec4096::detail {
using native::block;
using native::pair;
// The head and length table fit in 146 + 15*9 bits; 64 readable bytes cover
// every 64-bit field load. Short exact spans are copied to zeroed scratch.
// Scalar on purpose: NEON and AVX-512 versions of these four dependent levels
// (32-bit field windows, table lookups for the length widths) measured slower
// on both V2 and Zen 5; see README.md.
[[gnu::always_inline]] inline void read_frame_scalar(const std::uint8_t *w, unsigned population,
                                                     frame &out) {
    unsigned pos = 0;
    auto take = [&](unsigned width) {
        std::uint64_t word;
        std::memcpy(&word, w + pos / 8, 8);
        const unsigned value = unsigned(word >> (pos % 8)) & ((1u << width) - 1);
        pos += width;
        return value;
    };
    std::array<unsigned, 32> tree;
    tree[1] = population;
    for (unsigned i = 1; i < 16; ++i) {
        const unsigned half = 2048u >> (std::bit_width(i) - 1), p = tree[i];
        tree[2 * i] = take(split_width(p, half)) + lower(p, half);
        tree[2 * i + 1] = p - tree[2 * i];
    }
    unsigned table = 0;
    for (unsigned k = 0; k < 15; ++k)
        table += length_width[tree[16 + k]];
    unsigned start = pos + table;
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned c = tree[16 + k];
        out.population[k] = std::uint16_t(c);
        out.start[k] = std::uint16_t(start);
        if (k < 15)
            start += bounds.min[c] + take(length_width[c]);
    }
}

[[gnu::always_inline]] inline void read_frame(std::span<const byte> storage, unsigned bytes,
                                              unsigned population, frame &out) {
    alignas(64) std::uint8_t scratch[64];
    const auto *w = reinterpret_cast<const std::uint8_t *>(storage.data());
    if (storage.size() < 64) {
        std::memset(scratch, 0, 64);
        std::memcpy(scratch, w, std::min<unsigned>(bytes, 64));
        w = scratch;
    }
    read_frame_scalar(w, population, out);
}

// A block stream starts at byte `at`, and needs a 64-byte window from there.
// A readable suffix makes every window direct; otherwise adapt the short tail.
#if defined(IKEA_BEC256_AVX512)
struct source {
    __m512i body;
    unsigned first;
};
[[gnu::always_inline]] inline source window(const reader &input, unsigned at) {
    const auto *p = input.storage().data() + at;
    const auto first = [&] {
        if (at + 2 <= input.storage().size()) {
            std::uint16_t v;
            std::memcpy(&v, p, 2);
            return unsigned(v);
        }
        return std::to_integer<unsigned>(*p);
    }();
    if (input.storage().size() >= at + 64)
        return {_mm512_loadu_si512(p), first};
    return {_mm512_maskz_loadu_epi8((std::uint64_t{1} << (input.bytes() - at)) - 1, p), first};
}
[[gnu::always_inline]] inline block decode_one(const reader &input, unsigned k) {
    const unsigned s = input.frame().start[k];
    const auto w = window(input, s / 8);
    return native::decode_at(w.body, w.first, s % 8, input.frame().population[k]);
}
[[gnu::always_inline]] inline pair decode_two(const reader &input, unsigned k, unsigned j) {
    const unsigned s = input.frame().start[k], t = input.frame().start[j];
    const auto a = window(input, s / 8), b = window(input, t / 8);
    return native::decode2_at(a.body, a.first, s % 8, input.frame().population[k], b.body, b.first,
                              t % 8, input.frame().population[j]);
}
[[gnu::always_inline]] inline __m256i tree_one(const reader &input, unsigned k, unsigned &pos) {
    const unsigned s = input.frame().start[k];
    const auto w = window(input, s / 8);
    return native::tree(w.body, w.first, s % 8, input.frame().population[k], pos);
}
#else
[[gnu::always_inline]] inline const std::uint8_t *window(const reader &input, unsigned at,
                                                         std::uint8_t *scratch64) {
    const auto *p = reinterpret_cast<const std::uint8_t *>(input.storage().data()) + at;
    if (input.storage().size() >= at + 64)
        return p;
    std::memset(scratch64, 0, 64);
    std::memcpy(scratch64, p, input.bytes() - at);
    return scratch64;
}
[[gnu::always_inline]] inline unsigned first16(const std::uint8_t *w) {
    std::uint16_t v;
    std::memcpy(&v, w, 2);
    return v;
}
[[gnu::always_inline]] inline block decode_one(const reader &input, unsigned k) {
    alignas(64) std::uint8_t scratch[64];
    const unsigned s = input.frame().start[k];
    const auto *w = window(input, s / 8, scratch);
    return native::decode_at(w, first16(w), s % 8, input.frame().population[k]);
}
[[gnu::always_inline]] inline pair decode_two(const reader &input, unsigned k, unsigned j) {
    alignas(64) std::uint8_t sa[64], sb[64];
    const unsigned s = input.frame().start[k], t = input.frame().start[j];
    const auto *a = window(input, s / 8, sa), *b = window(input, t / 8, sb);
    return native::decode2_at(a, first16(a), s % 8, input.frame().population[k], b, first16(b),
                              t % 8, input.frame().population[j]);
}
[[gnu::always_inline]] inline block tree_one(const reader &input, unsigned k, unsigned &pos) {
    alignas(64) std::uint8_t scratch[64];
    const unsigned s = input.frame().start[k];
    const auto *w = window(input, s / 8, scratch);
    return native::tree(w, first16(w), s % 8, input.frame().population[k], pos);
}
#endif

[[gnu::always_inline]] inline bool terminal(unsigned population) {
    return population == 0 || population == 256;
}
[[gnu::always_inline]] inline void fill(std::uint64_t *words, unsigned population) {
    std::memset(words, population ? 255 : 0, 32);
}
[[gnu::always_inline]] inline void decode_block(const reader &input, unsigned k,
                                                std::uint64_t *words) {
    const unsigned c = input.frame().population[k];
    if (terminal(c))
        return fill(words, c);
    native::store_block(words, decode_one(input, k));
}
[[gnu::always_inline]] inline void decode_all(const reader &input, bitset4096 &output) {
    auto *words = output.words.data();
    for (unsigned k = 0; k < 16; k += 2) {
        const unsigned a = input.frame().population[k], b = input.frame().population[k + 1];
        if (!terminal(a) && !terminal(b))
            native::store_pair(words + 4 * k, decode_two(input, k, k + 1));
        else {
            decode_block(input, k, words + 4 * k);
            decode_block(input, k + 1, words + 4 * k + 4);
        }
    }
}
[[gnu::always_inline]] inline void block_counts(const reader &input, unsigned k,
                                                std::uint8_t *out) {
    const unsigned c = input.frame().population[k];
    if (terminal(c))
        return void(std::memset(out, c ? 8 : 0, 32));
    unsigned pos;
    native::store_counts(out, tree_one(input, k, pos));
}
// Whole blocks from the frame; within a block, byte counts from its tree and
// at most one byte rank. No other rank fields are read.
[[gnu::always_inline]] inline unsigned rank(const reader &input, unsigned position) {
    const unsigned k = position / 256, within = position % 256;
    unsigned result = 0;
    for (unsigned i = 0; i < k; ++i)
        result += input.frame().population[i];
    if (!within)
        return result;
    const unsigned c = input.frame().population[k];
    if (terminal(c))
        return result + (c ? within : 0);
    unsigned pos;
    const auto counts = native::prefix_counts(tree_one(input, k, pos), within / 8);
    result += counts.population;
    if (const unsigned bit = within % 8) {
        // The tree cursor is relative to the window's first byte.
        const unsigned at = input.frame().start[k] / 8 * 8 + pos + counts.width;
        const auto *body = reinterpret_cast<const std::uint8_t *>(input.storage().data());
        const unsigned width = ikea::bec256::detail::byte_width[counts.count];
        // Empty and full bytes have no rank field, possibly at the body's end.
        unsigned value = 0;
        if (width) {
            value = body[at / 8] >> (at % 8);
            if (at % 8 + width > 8)
                value |= unsigned(body[at / 8 + 1]) << (8 - at % 8);
            value &= (1u << width) - 1;
        }
        const unsigned pattern =
            ikea::bec256::detail::codes.value[ikea::bec256::detail::byte_base[counts.count] + value];
        result += std::popcount(pattern & ((1u << bit) - 1));
    }
    return result;
}

// Encoding: blocks are packed first, so the length table and exact size are
// known before any destination store.
struct plan {
    std::array<std::uint16_t, 16> population;
    std::array<std::uint16_t, 16> length;
    alignas(64) std::uint64_t words[16][8];
    unsigned bits;
};
[[gnu::always_inline]] inline unsigned prepare(const bitset4096 &input, plan &out) {
    unsigned total = 0;
    for (unsigned k = 0; k < 16; ++k) {
        const native::counted block(input.words.data() + 4 * k);
        const unsigned c = block.population();
        out.population[k] = std::uint16_t(c);
        total += c;
        out.length[k] =
            terminal(c) ? 0 : std::uint16_t(block.encode(c, native::pack_sink{out.words[k]}));
    }
    std::array<unsigned, 32> tree;
    for (unsigned k = 0; k < 16; ++k)
        tree[16 + k] = out.population[k];
    for (unsigned i = 15; i; --i)
        tree[i] = tree[2 * i] + tree[2 * i + 1];
    unsigned bits = 0;
    for (unsigned i = 1; i < 16; ++i)
        bits += split_width(tree[i], 2048u >> (std::bit_width(i) - 1));
    for (unsigned k = 0; k < 16; ++k)
        bits += (k < 15 ? length_width[out.population[k]] : 0) + out.length[k];
    out.bits = bits;
    return total;
}
// Exact emission: full words while all 64 bits belong to the body, then the
// final partial word's bytes. Field values are below 2^width.
struct bit_writer {
    std::uint8_t *out;
    unsigned at = 0, fill = 0;
    std::uint64_t acc = 0;
    [[gnu::always_inline]] void put(std::uint64_t value, unsigned width) {
        acc |= value << fill;
        fill += width;
        if (fill >= 64) {
            std::memcpy(out + at, &acc, 8);
            at += 8;
            fill -= 64;
            acc = value >> (width - fill);
        }
    }
    // A complete word: always stores. (v >> 1) >> (63 - fill) is v >> (64 - fill),
    // and zero when fill is zero.
    [[gnu::always_inline]] void put64(std::uint64_t value) {
        acc |= value << fill;
        std::memcpy(out + at, &acc, 8);
        at += 8;
        acc = (value >> 1) >> (63 - fill);
    }
    [[gnu::always_inline]] void append(const std::uint64_t *words, unsigned bits) {
        unsigned i = 0;
        for (; i < bits / 64; ++i)
            put64(words[i]);
        if (bits % 64)
            put(words[i] & ((std::uint64_t{1} << (bits % 64)) - 1), bits % 64);
    }
    [[gnu::always_inline]] void finish() {
        const unsigned n = (fill + 7) / 8;
        for (unsigned i = 0; i < n; ++i)
            out[at + i] = std::uint8_t(acc >> (8 * i));
    }
};
// The head and length table: thirty fields, independent given the counts,
// in eight groups of four.
[[gnu::always_inline]] inline void head_groups(const plan &in, std::uint64_t *group,
                                               std::uint64_t *group_width) {
    native::head_groups(in.population.data(), in.length.data(), group, group_width);
}
#if defined(IKEA_BEC256_AVX512)
// Exact emission straight into the destination. Each chunk (the head, then
// each block's assembled words) is shifted into place in one register; its
// first word merges the carry from the previous chunk's register, never a
// reload, and a byte-masked store keeps every write inside the body.
// BZHI reads only the low eight bits of its index, so clamp first.
[[gnu::always_inline]] inline std::uint64_t first_bytes(unsigned n) {
    return n >= 64 ? ~std::uint64_t{0} : _bzhi_u64(~std::uint64_t{0}, n);
}
[[gnu::always_inline]] inline void emit(const plan &in, std::uint8_t *out) {
    const unsigned n = (in.bits + 7) / 8;
    alignas(64) std::uint64_t group[8], group_width[8];
    head_groups(in, group, group_width);
    const auto widths = _mm512_load_si512(group_width);
    auto last = ikea::bec256::detail::assemble(_mm512_load_si512(group), widths);
    unsigned at = unsigned(_mm512_reduce_add_epi64(widths)), base = 0;
    _mm512_mask_storeu_epi8(out, first_bytes(n), last);
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned length = in.length[k];
        if (!length)
            continue;
        const unsigned j = at / 64;
        const auto v = _mm512_load_si512(in.words[k]);
        auto shifted = _mm512_shldv_epi64(v, _mm512_alignr_epi64(v, _mm512_setzero_si512(), 7),
                                          _mm512_set1_epi64(at % 64));
        const auto carry = _mm512_permutexvar_epi64(_mm512_set1_epi64(j - base), last);
        shifted = _mm512_mask_or_epi64(shifted, 1, shifted, carry);
        _mm512_mask_storeu_epi8(out + 8 * j, first_bytes(n - 8 * j), shifted);
        last = shifted;
        base = j;
        at += length;
    }
}
#else
// The eight head groups go through one bit chain into staging words. Each
// block's packed words then shift into place independently: only its first
// word merges with what precedes it. The exact body is copied out last.
[[gnu::always_inline]] inline void emit(const plan &in, std::uint8_t *out) {
    alignas(64) std::uint64_t staging[max_bytes / 8 + 8];
    auto *bytes = reinterpret_cast<std::uint8_t *>(staging);
    bit_writer bits{bytes};
    alignas(64) std::uint64_t group[8], group_width[8];
    head_groups(in, group, group_width);
    for (unsigned g = 0; g < 8; ++g)
        bits.put(group[g], unsigned(group_width[g]));
    std::memcpy(bytes + bits.at, &bits.acc, 8);
    unsigned at = bits.at * 8 + bits.fill;
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned length = in.length[k];
        if (!length)
            continue;
        // A fixed seven words (a block is at most 374 bits): no data-dependent
        // trip count to mispredict. Words past the block are rewritten by the
        // next block or fall in staging slack; selects stand in for the
        // packed words the block never wrote.
        const unsigned j = at / 64, s = at % 64, m = (length + 63) / 64;
        const auto *w = in.words[k];
        std::uint64_t word[7];
        for (unsigned i = 0; i < 7; ++i)
            word[i] = i < m ? w[i] : 0;
        staging[j] |= word[0] << s;
        for (unsigned i = 1; i < 7; ++i)
            staging[j + i] = (word[i] << s) | ((word[i - 1] >> 1) >> (63 - s));
        at += length;
    }
    std::memcpy(out, bytes, (in.bits + 7) / 8);
}
#endif
} // namespace bec4096::detail
