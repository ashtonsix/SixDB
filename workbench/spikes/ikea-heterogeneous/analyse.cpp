#include "analyse.h"
#include "../ikea-blocks/composition/native_features.h"
#if defined(__aarch64__)
#include "../ikea-blocks/native_neon.h"
#else
#include "../ikea-blocks/native_avx512.h"
#endif

namespace ikea::heterogeneous {
namespace {
template<AnalyseModel Model>
inline __attribute__((always_inline, flatten)) unsigned predict_tile(const std::uint8_t* p) {
    const auto bits = composition::load_native(p);
    if constexpr(Model == AnalyseModel::cheap)
        return bec_predictor::predict_size(composition::native_features_impl<false, false>(bits));
    else
        return bec_predictor::predict_size_quadrants(composition::native_features_impl<true, true>(bits));
}

template<AnalyseModel Model, AnalyseScan Scan>
inline __attribute__((always_inline, flatten)) unsigned predict_window(const std::uint8_t* p) {
    unsigned sum = 0;
    if constexpr(Scan == AnalyseScan::sample32) {
#pragma clang loop unroll(disable)
        for(unsigned stratum = 0; stratum != 32; ++stratum)
            sum += predict_tile<Model>(p + 32 * analyse_sample_tile(stratum));
        return sum * 8;
    } else if constexpr(Model == AnalyseModel::cheap) {
        // The provider's wider feature path preserves two separate tile sums.
        // Each tile still receives its own piece selection, rounding and clamp.
#pragma clang loop unroll(disable)
        for(unsigned tile = 0; tile != analyse_tiles; tile += 2) {
            const auto pair = composition::features_from_64_bytes(p + tile * 32);
            sum += bec_predictor::predict_size(pair.first);
            sum += bec_predictor::predict_size(pair.second);
        }
        return sum;
    } else {
#pragma clang loop unroll(disable)
        for(unsigned tile = 0; tile != analyse_tiles; ++tile)
            sum += predict_tile<Model>(p + tile * 32);
        return sum;
    }
}
} // namespace

unsigned predict_body_bytes(const std::uint8_t* plain, AnalyseModel model,
                            AnalyseScan scan) noexcept {
    if(model == AnalyseModel::cheap) {
        if(scan == AnalyseScan::full)
            return predict_window<AnalyseModel::cheap, AnalyseScan::full>(plain);
        return predict_window<AnalyseModel::cheap, AnalyseScan::sample32>(plain);
    }
    if(scan == AnalyseScan::full)
        return predict_window<AnalyseModel::quadrants, AnalyseScan::full>(plain);
    return predict_window<AnalyseModel::quadrants, AnalyseScan::sample32>(plain);
}

unsigned encode_body_bytes(const std::uint8_t* __restrict plain,
                           std::uint8_t* __restrict output) noexcept {
    unsigned bytes = 0;
#pragma clang loop unroll(disable)
    for(unsigned tile = 0; tile != analyse_tiles; ++tile) {
#if defined(__aarch64__)
        const auto bits = ikea_probe::neon::load256(plain + tile * 32);
        const auto lo = vcntq_u8(bits.val[0]), hi = vcntq_u8(bits.val[1]);
        const unsigned population = unsigned(vaddvq_u8(lo)) + vaddvq_u8(hi);
        if(population != 0 && population != 256)
            bytes += ikea_probe::neon::encode(bits, population, output + bytes);
#else
        const auto bits = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(plain + tile * 32));
        const unsigned population = unsigned(composition::sum_four(_mm256_popcnt_epi64(bits)));
        if(population != 0 && population != 256)
            bytes += ikea_probe::avx512::encode(bits, population, output + bytes);
#endif
    }
    return bytes;
}
} // namespace ikea::heterogeneous
