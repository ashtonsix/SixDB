#pragma once

#include <cstdint>

namespace ikea::bec_predictor {

// Provisional analyser estimate, not a bound or an encode admission decision.
// Model identity and training provenance live in predictor/README.md.
inline constexpr char model_id[] = "bec256-distance-enum-q12-20260909";
inline constexpr char transition_model_id[] = "bec256-distance-enum-transitions-q12-20260909";
inline constexpr char quadrant_model_id[] = "bec256-distance-enum-transitions-quadrants-q12-20260909";
inline constexpr unsigned fraction_bits = 12;

// The middle byte remains available to the composition probe's optional
// structure feature. This model consumes only distance and enumerative cost.
[[nodiscard]] inline constexpr std::uint64_t pack_features(unsigned distance,
                                                         unsigned enum_bits) {
    return distance | (std::uint64_t{enum_bits} << 16);
}

struct Coefficients {
    std::int32_t bias;
    std::int32_t distance;
    std::int32_t enum_bits;
};

inline constexpr Coefficients coefficients[] = {
    {140613, -1093, 900}, // minority population 1..8
    {24948, -122, 856},  // 9..32
    {14140, 27, 804},    // 33..96
    {16023, 23, 789},    // 97..128
};

// Preconditions: distance is in [0,128], enumerative cost in [0,224].
// No error handling belongs in the feature/model handoff.
[[nodiscard]] inline unsigned predict_size(std::uint64_t packed) {
    const auto distance = static_cast<unsigned>(packed & 255);
    const auto enum_bits = static_cast<unsigned>((packed >> 16) & 255);
    if (distance == 128) return 0;
    const auto minority = 128 - distance;
    const auto piece = static_cast<unsigned>(minority > 8) +
                       static_cast<unsigned>(minority > 32) +
                       static_cast<unsigned>(minority > 96);
    const auto c = coefficients[piece];
    const auto acc = c.bias + c.distance * static_cast<std::int32_t>(distance) +
                    c.enum_bits * static_cast<std::int32_t>(enum_bits);
    // Signed right shift in C++23 is floor division. Adding half rounds ties up.
    const auto estimate = (acc + (1 << (fraction_bits - 1))) >> fraction_bits;
    return estimate < 0 ? 0 : estimate > 47 ? 47 : static_cast<unsigned>(estimate);
}

// Development-refined candidates: each adds one enum_bits==0 piece and uses
// an explicitly recorded 1% structural-development training augmentation.
// They are alternatives to measure, not an automatic dispatch policy.
struct StructuredCoefficients {
    std::int32_t bias;
    std::int32_t distance;
    std::int32_t transitions;
    std::int32_t enum_bits;
    std::int32_t quarters;
};

inline constexpr StructuredCoefficients transition_coefficients[] = {
    {144127, -1121, 61, 852, 0},
    {29483, -158, -27, 861, 0},
    {22590, -29, -44, 796, 0},
    {24612, 3, -13, 750, 0},
    {15941, -48, 2583, 0, 0},
};
inline constexpr StructuredCoefficients quadrant_coefficients[] = {
    {182208, -1416, 6, 812, -45},
    {75051, -501, -46, 751, -73},
    {50282, -188, -53, 691, -66},
    {50567, -49, -28, 636, -72},
    {27276, -117, 2178, 0, -24},
};

template<bool Quarters>
[[nodiscard]] inline unsigned predict_structure(std::uint64_t packed) {
    const auto distance = static_cast<unsigned>(packed & 255);
    const auto transitions = static_cast<unsigned>((packed >> 8) & 255);
    const auto enum_bits = static_cast<unsigned>((packed >> 16) & 255);
    if (distance == 128) return 0;
    const auto minority = 128 - distance;
    const auto piece = enum_bits == 0 ? 4u : static_cast<unsigned>(minority > 8) +
                        static_cast<unsigned>(minority > 32) +
                        static_cast<unsigned>(minority > 96);
    const auto c = Quarters ? quadrant_coefficients[piece] : transition_coefficients[piece];
    auto acc = c.bias + c.distance * static_cast<std::int32_t>(distance) +
               c.transitions * static_cast<std::int32_t>(transitions) +
               c.enum_bits * static_cast<std::int32_t>(enum_bits);
    if constexpr (Quarters) acc += c.quarters * static_cast<std::int32_t>((packed >> 24) & 1023);
    const auto estimate = (acc + 2048) >> 12;
    return estimate < 0 ? 0 : estimate > 47 ? 47 : static_cast<unsigned>(estimate);
}

// Additional packed fields: transitions [8,15], quarter dispersion [24,33].
[[nodiscard]] inline unsigned predict_size_transitions(std::uint64_t packed) {
    return predict_structure<false>(packed);
}
[[nodiscard]] inline unsigned predict_size_quadrants(std::uint64_t packed) {
    return predict_structure<true>(packed);
}

} // namespace ikea::bec_predictor
