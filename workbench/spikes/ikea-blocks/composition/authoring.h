#pragma once

#include "contracts.h"
#include <cstdint>
#include <string>

namespace ikea::composition {

// A child edge refers to an actual physical description, not a live C++ object
// and not the implementation that happens to execute its operations.
struct SourceChild {
    std::uint64_t representation;
    std::string path;
};

// A. The author introduces a named analysis component, publishes its child and
// explicitly exposes the enclosing operation's loop and reduction.
template<class Source=SourceChild> struct NamedAnalysis {
    Source source;
    FixedModel model;
    const Source& child() const { return source; }

    template<class Ops> auto expose(Ops& ops) const {
        return ops.sum_tiles(source, [&](auto tile) {
            auto bits = ops.load(tile);
            auto fields = ops.features(bits);
            return ops.model(model, fields);
        });
    }
};

// B. The author writes an ordinary function over the supported operations. Its
// source argument keeps the connection to the same physical child identity.
template<class Ops,class Source>
auto analyse(Ops& ops, const Source& source, FixedModel model = {}) {
    return ops.sum_tiles(source, [&](auto tile) {
        auto bits = ops.load(tile);
        auto fields = ops.features(bits);
        return ops.model(model, fields);
    });
}

// A different estimator, authored by adding a feature operation and choosing
// its matching model. The lowerer has no prewritten table for this recipe.
template<class Ops,class Source>
auto analyse_transitions(Ops& ops,const Source& source) {
    return ops.sum_tiles(source,[&](auto tile) {
        auto fields=ops.features_transitions(ops.load(tile));
        return ops.model(TransitionModel{},fields);
    });
}

} // namespace ikea::composition
