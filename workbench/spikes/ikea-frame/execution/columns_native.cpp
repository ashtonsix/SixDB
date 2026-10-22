#include "columns_native.h"
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace frame_columns {
namespace {
template<class F> void selected_rows(std::span<const Bits> selected, F&& f) {
    for (unsigned b = 0; b < selected.size(); ++b)
        for (unsigned w = 0; w < 4; ++w)
            for (auto bits = selected[b].words[w]; bits; bits &= bits - 1)
                f(b * 256 + w * 64 + unsigned(std::countr_zero(bits)));
}
template<class C> bool uniform_four(const C* codes) {
    if constexpr (sizeof(C) == 1) {
        std::uint32_t word; std::memcpy(&word, codes, 4);
        return word == std::uint32_t(codes[0]) * UINT32_C(0x01010101);
    } else {
        std::uint64_t word; std::memcpy(&word, codes, 8);
        return word == std::uint64_t(codes[0]) * UINT64_C(0x0001000100010001);
    }
}
template<class C, bool Coalesce> void group(const C* codes, const NativeRows& input, std::span<const Bits> selected,
                            std::span<const Bits> has_category, std::span<const std::uint8_t> allowed,
                            Word threshold, std::span<Total> totals) {
    if (threshold > UINT32_MAX) return;
    const unsigned null_code = unsigned(totals.size() - 1);
#if defined(__aarch64__)
    const uint32x4_t weights{1, 2, 4, 8};
    const auto compare = vdupq_n_u32(std::uint32_t(threshold));
#endif
    for (unsigned b = 0; b < selected.size(); ++b) {
        const auto& s = selected[b].words;
        if (!(s[0] | s[1] | s[2] | s[3])) continue;
        for (unsigned local = 0; local < 256; local += 4) {
            unsigned bits = unsigned(selected[b].words[local / 64] >> (local % 64)) & 15;
            if (!bits) continue;
            const unsigned first = b * 256 + local;
#if defined(__aarch64__)
            if (threshold) bits &= vaddvq_u32(vandq_u32(vcgeq_u32(vld1q_u32(input.x.data() + first), compare), weights));
#endif
            if (!bits) continue;
            if constexpr (Coalesce) {
                const unsigned valid = unsigned(has_category[b].words[local / 64] >> (local % 64)) & 15;
                if ((valid == 0 || valid == 15) && uniform_four(codes + first)) {
                    const auto value = input.x[first];
#if defined(__aarch64__)
                    const bool same_x = vminvq_u32(vceqq_u32(vld1q_u32(input.x.data() + first), vdupq_n_u32(value))) == UINT32_MAX;
#else
                    const bool same_x = value == input.x[first + 1] && value == input.x[first + 2] && value == input.x[first + 3];
#endif
                    if (same_x && value >= threshold) {
                        const auto c = valid ? unsigned(codes[first]) : null_code;
                        const auto n = unsigned(std::popcount(bits));
                        if (allowed[c]) { totals[c].count += n; totals[c].sum += Word(n) * value; }
                        continue;
                    }
                }
            }
            while (bits) {
                const unsigned lane = std::countr_zero(bits); bits &= bits - 1;
                const auto value = input.x[first + lane];
#if !defined(__aarch64__)
                if (value < threshold) continue;
#endif
                const auto c = has_category[b].test(local + lane) ? unsigned(codes[first + lane]) : null_code;
                if (allowed[c]) { ++totals[c].count; totals[c].sum += value; }
            }
        }
    }
}
template<class C, bool Coalesce> void histogram(const C* codes, std::span<const Bits> selected,
                                std::span<std::uint32_t> counts) {
    for (unsigned b = 0; b < selected.size(); ++b) {
        const auto& s = selected[b].words;
        if (!(s[0] | s[1] | s[2] | s[3])) continue;
        for (unsigned local = 0; local < 256; local += 4) {
            unsigned bits = unsigned(selected[b].words[local / 64] >> (local % 64)) & 15;
            const unsigned first = b * 256 + local;
            if constexpr (Coalesce) {
                if (bits && uniform_four(codes + first)) { counts[codes[first]] += std::popcount(bits); continue; }
            }
            if (bits == 15) {
                ++counts[codes[first]]; ++counts[codes[first + 1]];
                ++counts[codes[first + 2]]; ++counts[codes[first + 3]];
            } else while (bits) {
                const unsigned lane = std::countr_zero(bits); bits &= bits - 1;
                ++counts[codes[first + lane]];
            }
        }
    }
}
template<class C> Word projected(const C* codes, std::span<const std::uint32_t> x,
                                std::span<const Bits> selected, Word threshold) {
    Word sum = 0;
    selected_rows(selected, [&](unsigned row) {
        const auto value = x[codes[row]];
        if (value >= threshold) sum += value;
    });
    return sum;
}
} // namespace
void native_group(const NativeRows& input, std::span<const Bits> selected,
                  std::span<const Bits> has_category, std::span<const std::uint8_t> allowed,
                  Word threshold, std::span<Total> totals, bool coalesce) {
    if (!input.category.small.empty()) {
        if (coalesce) group<std::uint8_t, true>(input.category.small.data(), input, selected, has_category, allowed, threshold, totals);
        else group<std::uint8_t, false>(input.category.small.data(), input, selected, has_category, allowed, threshold, totals);
    } else {
        if (coalesce) group<std::uint16_t, true>(input.category.large.data(), input, selected, has_category, allowed, threshold, totals);
        else group<std::uint16_t, false>(input.category.large.data(), input, selected, has_category, allowed, threshold, totals);
    }
}
Word native_x_sum(const NativeRows& input, std::span<const Bits> selected, Word threshold) {
    if (threshold > UINT32_MAX) return 0;
    Word sum = 0;
#if defined(__aarch64__)
    const auto compare = vdupq_n_u32(std::uint32_t(threshold));
    uint64x2_t total = vdupq_n_u64(0);
    alignas(16) static constexpr std::uint32_t masks[16][4] = {
        {0,0,0,0},{~0u,0,0,0},{0,~0u,0,0},{~0u,~0u,0,0},
        {0,0,~0u,0},{~0u,0,~0u,0},{0,~0u,~0u,0},{~0u,~0u,~0u,0},
        {0,0,0,~0u},{~0u,0,0,~0u},{0,~0u,0,~0u},{~0u,~0u,0,~0u},
        {0,0,~0u,~0u},{~0u,0,~0u,~0u},{0,~0u,~0u,~0u},{~0u,~0u,~0u,~0u}};
    for (unsigned b = 0; b < selected.size(); ++b) {
        const auto& s = selected[b].words;
        if (!(s[0] | s[1] | s[2] | s[3])) continue;
        for (unsigned local = 0; local < 256; local += 4) {
            const unsigned bits = unsigned(selected[b].words[local / 64] >> (local % 64)) & 15;
            if (!bits) continue;
            auto values = vld1q_u32(input.x.data() + b * 256 + local);
            values = vandq_u32(values, vandq_u32(vcgeq_u32(values, compare), vld1q_u32(masks[bits])));
            total = vaddq_u64(total, vpaddlq_u32(values));
        }
    }
    sum = vaddvq_u64(total);
#else
    selected_rows(selected, [&](unsigned row) { if (input.x[row] >= threshold) sum += input.x[row]; });
#endif
    return sum;
}
void native_histogram(const NativeCodes& codes, std::span<const Bits> selected,
                      std::span<std::uint32_t> counts, bool coalesce) {
    if (!codes.small.empty()) {
        if (coalesce) histogram<std::uint8_t, true>(codes.small.data(), selected, counts);
        else histogram<std::uint8_t, false>(codes.small.data(), selected, counts);
    } else {
        if (coalesce) histogram<std::uint16_t, true>(codes.large.data(), selected, counts);
        else histogram<std::uint16_t, false>(codes.large.data(), selected, counts);
    }
}
Word native_projected_sum(const NativeCodes& codes, std::span<const std::uint32_t> x,
                          std::span<const Bits> selected, Word threshold) {
    if (!codes.small.empty()) return projected(codes.small.data(), x, selected, threshold);
    return projected(codes.large.data(), x, selected, threshold);
}
} // namespace frame_columns
