#pragma once

#include "prepared.h"
#include "native_features.h"

namespace ikea::composition {
struct InlineTile { const std::uint8_t* bytes; };
struct AttachedSource {};
struct InlineOps {
    const Binding& binding;
    template<class Source,class Body> std::uint64_t sum_tiles(const Source&, Body body) const {
        std::uint64_t accumulator=0;
        for(std::size_t i=0;i<binding.count;++i)
            accumulator+=body(InlineTile{binding.first+i*binding.stride});
        return accumulator;
    }
    Native256 load(InlineTile tile) const { return load_native(tile.bytes); }
    std::uint64_t features(Native256 bits) const { return native_features(bits); }
    std::uint64_t load_features(InlineTile tile) const { return features_from_bytes(tile.bytes); }
    unsigned model(FixedModel,std::uint64_t fields) const { return bec_predictor::predict_size(fields); }
};
} // namespace ikea::composition
