#pragma once
#include "../ikea-blocks/predictor/model.h"
#include <cstdint>

namespace ikea::heterogeneous {
enum class AnalyseModel { cheap, quadrants };
enum class AnalyseScan { full, sample32 };

inline constexpr unsigned analyse_tiles = 256;
inline constexpr unsigned analyse_plain_bytes = analyse_tiles * 32;
inline constexpr unsigned analyse_max_body_bytes = analyse_tiles * 47;
inline constexpr unsigned analyse_encode_writable_bytes = analyse_max_body_bytes + 64;

constexpr const char* analyse_model_id(AnalyseModel model) {
    return model == AnalyseModel::cheap ? bec_predictor::model_id : bec_predictor::quadrant_model_id;
}
constexpr const char* analyse_scan_name(AnalyseScan scan) {
    return scan == AnalyseScan::full ? "full" : "sample32";
}

// One tile per eight-tile stratum; successive strata cycle all eight offsets.
// This deterministic pattern can alias correlated data. It is not randomized
// sampling, a bound or a guarantee about an unobserved tile.
constexpr unsigned analyse_sample_tile(unsigned stratum) {
    return 8 * stratum + ((5 * stratum + 3) & 7);
}

// Trusted complete 65,536-position input: exactly 8,192 readable bytes, with no
// alignment requirement. Dispatch happens once, outside the tile scan. Full
// returns the sum of 256 individually rounded frozen-model byte estimates;
// sample32 returns eight times the sum over the fixed 32 sampled tiles.
// Metadata, allocation slack, savings thresholds and the final decision are
// external to this body-only estimate. Neither scan returns an upper bound.
unsigned predict_body_bytes(const std::uint8_t* plain8192, AnalyseModel,
                            AnalyseScan) noexcept;

// Body-only conversion using the provider's native encoder. Input and output
// are disjoint, with 8,192 readable input bytes and at least 12,096 writable
// output bytes. Returns the actual logical byte count (at most 12,032).
// Bodies concatenate densely; stores may extend beyond the returned count.
// No metadata or framing index is emitted, and output slack is not initialized.
unsigned encode_body_bytes(const std::uint8_t* plain8192,
                           std::uint8_t* writable12096) noexcept;

// Independent cold checks; zero means success. No datasets or training.
int check_analyser();
} // namespace ikea::heterogeneous
