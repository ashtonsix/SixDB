#pragma once
#include <ikea/hashtable/detail/hash.h>
#include <array>
#include <bit>
#ifdef SIXDB_SWISS_TRACE
#include <vector>
#endif

namespace ikea::hashtable::batch {
#ifdef SIXDB_SWISS_TRACE
inline std::vector<std::size_t> tuple_trace;
inline void record(std::size_t slot) { tuple_trace.push_back(slot); }
#endif
#if defined(__AVX2__)
[[gnu::always_inline]] inline __m256i pair(const byte* controls, std::size_t first,
                                          std::size_t second) {
    // Initial triangular groups are adjacent except at wrap. Use the actual
    // wide load there; later nonadjacent groups are assembled in probe order.
    if (second == first + 1)
        return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(controls + first * 16));
    auto bytes = _mm256_castsi128_si256(_mm_loadu_si128(
        reinterpret_cast<const __m128i*>(controls + first * 16)));
    return _mm256_inserti128_si256(bytes, _mm_loadu_si128(
        reinterpret_cast<const __m128i*>(controls + second * 16)), 1);
}
#endif
template <unsigned Groups> struct packet {
    static_assert(Groups == 2 || Groups == 4);
    std::array<std::size_t, Groups> groups;
    word candidates = 0, empties = 0, vacancies = 0;
    std::size_t next_group, next_step;
    std::size_t slot(unsigned lane) const { return groups[lane / 16] * 16 + lane % 16; }
};

// Packed mask lanes are logical probe order, never physical adjacency order.
template <unsigned Groups, bool Vacancies = false>
[[gnu::always_inline]] packet<Groups> scan(const byte* controls, std::size_t count,
                                          std::size_t group, std::size_t step, byte tag) {
    packet<Groups> out;
    for (unsigned i = 0; i < Groups; ++i) {
        out.groups[i] = group;
        group = (group + (++step)) & (count - 1);
    }
    out.next_group = group;
    out.next_step = step;
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    if constexpr (Groups == 4) {
        auto bytes = _mm512_castsi128_si512(_mm_loadu_si128(
            reinterpret_cast<const __m128i*>(controls + out.groups[0] * 16)));
        bytes = _mm512_inserti32x4(bytes, _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(controls + out.groups[1] * 16)), 1);
        bytes = _mm512_inserti32x4(bytes, _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(controls + out.groups[2] * 16)), 2);
        bytes = _mm512_inserti32x4(bytes, _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(controls + out.groups[3] * 16)), 3);
        out.candidates = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(tag));
        out.empties = _mm512_cmpeq_epi8_mask(bytes, _mm512_setzero_si512());
        if constexpr (Vacancies)
            out.vacancies = out.empties | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(1));
    } else {
        const auto bytes = pair(controls, out.groups[0], out.groups[1]);
        out.candidates = _mm256_cmpeq_epi8_mask(bytes, _mm256_set1_epi8(tag));
        out.empties = _mm256_cmpeq_epi8_mask(bytes, _mm256_setzero_si256());
        if constexpr (Vacancies)
            out.vacancies = out.empties | _mm256_cmpeq_epi8_mask(bytes, _mm256_set1_epi8(1));
    }
#elif defined(__AVX2__)
    for (unsigned i = 0; i < Groups; i += 2) {
        const auto bytes = pair(controls, out.groups[i], out.groups[i + 1]);
        out.candidates |= word(unsigned(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(tag))))) << (i * 16);
        const auto empty = unsigned(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(bytes, _mm256_setzero_si256())));
        out.empties |= word(empty) << (i * 16);
        if constexpr (Vacancies)
            out.vacancies |= word(empty | unsigned(_mm256_movemask_epi8(
                _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(1))))) << (i * 16);
    }
#else
    // Exact portable/NEON check route; only native x86 is timed by this study.
    for (unsigned i = 0; i < Groups; ++i) {
        const auto* p = controls + out.groups[i] * 16;
        out.candidates |= word(equal_controls(p, tag)) << (i * 16);
        out.empties |= word(equal_controls(p, 0)) << (i * 16);
        if constexpr (Vacancies)
            out.vacancies |= word(equal_controls(p, 0) | equal_controls(p, 1)) << (i * 16);
    }
#endif
    if constexpr (Groups == 2) {
        const word prefix = out.empties & 0xffff ? 0xffff : 0xffffffff;
        out.candidates &= prefix;
        if constexpr (Vacancies)
            out.vacancies &= prefix;
    }
#if defined(__BMI2__) && defined(__x86_64__)
    else {
        // No empty gives 80, which BZHI treats as an untruncated 64-bit mask.
        const unsigned bits = (unsigned(std::countr_zero(out.empties)) | 15u) + 1;
        out.candidates = _bzhi_u64(out.candidates, bits);
        if constexpr (Vacancies)
            out.vacancies = _bzhi_u64(out.vacancies, bits);
    }
#else
    else if (out.empties) {
        // Include every lane of the first terminating 16-byte group. Never
        // shift by 64, and never discard later candidates within that group.
        const unsigned shift = (63u - std::countr_zero(out.empties)) & ~15u;
        const word prefix = ~word{0} >> shift;
        out.candidates &= prefix;
        if constexpr (Vacancies)
            out.vacancies &= prefix;
    }
#endif
    return out;
}
struct line_packet {
    std::size_t base_group, next_group, next_step;
    word candidates = 0, empties = 0, vacancies = 0;
    std::size_t slot(unsigned lane) const { return base_group * 16 + lane; }
};

// First-packet lookahead only. With the default 64-aligned controls, this
// reads exactly the cache line already needed by the first scalar group.
// Caller supplies count>=4. Unaligned storage remains correct through loadu.
template <unsigned Bytes, bool Vacancies = false>
[[gnu::always_inline]] line_packet first_line(const byte* controls, std::size_t count,
                                              std::size_t group, byte tag) {
    static_assert(Bytes == 32 || Bytes == 64);
    const unsigned home = group & 3;
    const unsigned edge = home == 3;
    line_packet out;
    out.base_group = Bytes == 64 ? group & ~std::size_t{3} : group - edge;
    const auto* p = controls + out.base_group * 16;
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    if constexpr (Bytes == 64) {
        const auto bytes = _mm512_loadu_si512(p);
        out.candidates = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(tag));
        out.empties = _mm512_cmpeq_epi8_mask(bytes, _mm512_setzero_si512());
        if constexpr (Vacancies)
            out.vacancies = out.empties | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(1));
    } else {
        const auto bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        out.candidates = _mm256_cmpeq_epi8_mask(bytes, _mm256_set1_epi8(tag));
        out.empties = _mm256_cmpeq_epi8_mask(bytes, _mm256_setzero_si256());
        if constexpr (Vacancies)
            out.vacancies = out.empties | _mm256_cmpeq_epi8_mask(bytes, _mm256_set1_epi8(1));
    }
#elif defined(__AVX2__)
    for (unsigned i = 0; i < Bytes; i += 32) {
        const auto bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
        out.candidates |= word(unsigned(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(tag))))) << i;
        const auto empty = unsigned(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(bytes, _mm256_setzero_si256())));
        out.empties |= word(empty) << i;
        if constexpr (Vacancies)
            out.vacancies |= word(empty | unsigned(_mm256_movemask_epi8(
                _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(1))))) << i;
    }
#else
    for (unsigned i = 0; i < Bytes; i += 16) {
        out.candidates |= word(equal_controls(p + i, tag)) << i;
        out.empties |= word(equal_controls(p + i, 0)) << i;
        if constexpr (Vacancies)
            out.vacancies |= word(equal_controls(p + i, 0) | equal_controls(p + i, 1)) << i;
    }
#endif
    const word eligible = [&] {
        if constexpr (Bytes == 32)
            return word{0xffffffff} & (word{0xffffffff} << (edge * 16));
        else {
            // Logical order within the first line is monotonically physical:
            // [0,1,3], [1,2], [2,3], or [3]. Physical group 2 is not a probe
            // after home 0, so even its empty controls must be ignored.
            constexpr word masks[4] = {0xffff0000ffffffffULL, 0x0000ffffffff0000ULL,
                                       0xffffffff00000000ULL, 0xffff000000000000ULL};
            return masks[home];
        }
    }();
    out.candidates &= eligible;
    out.empties &= eligible;
    out.vacancies &= eligible;
    if constexpr (Bytes == 32) {
        const word prefix = out.empties & 0xffff ? 0xffff : 0xffffffff;
        out.candidates &= prefix;
        out.vacancies &= prefix;
    } else {
#if defined(__BMI2__) && defined(__x86_64__)
        const unsigned bits = (unsigned(std::countr_zero(out.empties)) | 15u) + 1;
        out.candidates = _bzhi_u64(out.candidates, bits);
        out.vacancies = _bzhi_u64(out.vacancies, bits);
#else
        if (out.empties) {
            const unsigned shift = (63u - std::countr_zero(out.empties)) & ~15u;
            out.candidates &= ~word{0} >> shift;
            out.vacancies &= ~word{0} >> shift;
        }
#endif
    }
    out.next_step = Bytes == 64 ? 3 - ((home + 1) >> 1) : 2 - edge;
    out.next_group = (group + out.next_step * (out.next_step + 1) / 2) & (count - 1);
    return out;
}
} // namespace ikea::hashtable::batch
