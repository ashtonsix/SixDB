#include "native_features.h"

namespace ikea::composition {
extern "C" std::uint64_t ikea_comp_features256(const void* p) { return features_from_bytes(p); }
extern "C" FeaturePair ikea_comp_features512(const void* p) { return features_from_64_bytes(p); }
extern "C" unsigned ikea_comp_predict256(const void* p) { return bec_predictor::predict_size(features_from_bytes(p)); }
extern "C" std::uint64_t ikea_comp_features_transitions(const void* p) { return features_with_transitions(p); }
extern "C" std::uint64_t ikea_comp_features_quadrants(const void* p) { return features_with_quadrants(p); }
extern "C" unsigned ikea_comp_predict_transitions(const void* p) { return bec_predictor::predict_size_transitions(features_with_transitions(p)); }
extern "C" unsigned ikea_comp_predict_quadrants(const void* p) { return bec_predictor::predict_size_quadrants(features_with_quadrants(p)); }
}
