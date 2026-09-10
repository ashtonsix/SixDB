#pragma once

// Target-independent operation contracts. Recording a composition does not
// require native vector types, intrinsics, a transport ABI, or model arithmetic.
namespace ikea::composition {
inline constexpr char feature_id[]="bec256-distance-enum-v1";
inline constexpr char transition_feature_id[]="bec256-distance-enum-transitions-v1";
struct FixedModel {
    static constexpr char id[]="bec256-distance-enum-q12-20260909";
};
struct TransitionModel {
    static constexpr char id[]="bec256-distance-enum-transitions-q12-20260909";
};
} // namespace ikea::composition
