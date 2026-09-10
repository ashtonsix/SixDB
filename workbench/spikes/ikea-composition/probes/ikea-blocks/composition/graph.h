#pragma once

#include "authoring.h"
#include <ostream>
#include <vector>

namespace ikea::composition {

enum class Op { repeat256, tile_index, load, features, model, sum, load_features, removed };
struct Node {
    Op op;
    std::vector<unsigned> inputs;
    std::string identity;
};
struct Graph {
    SourceChild source;
    std::vector<Node> nodes;
    unsigned root = 0;
};
struct TileSymbol { unsigned node; };
struct BitsSymbol { unsigned node; };
struct FeaturesSymbol { unsigned node; };
struct SizeSymbol { unsigned node; };
struct SumSymbol { unsigned node; };

struct Recorder {
    Graph graph;
    unsigned emit(Op op, std::vector<unsigned> inputs = {}, std::string identity = {});
    template<class Body> SumSymbol sum_tiles(const SourceChild& source, Body body) {
        graph.source = source;
        auto loop = emit(Op::repeat256, {}, "source.tile_count");
        auto tile = emit(Op::tile_index, {loop}, "i in [0,tile_count)");
        auto value = body(TileSymbol{tile});
        // Sum is the explicit loop-carried reduction: initial state zero,
        // acc[i+1]=acc[i]+body(i). The body is recorded once with a symbolic i.
        auto sum = emit(Op::sum, {loop,value.node}, "u64: acc0=0; acc_next=acc+value");
        graph.root = sum;
        return {sum};
    }
    BitsSymbol load(TileSymbol tile) { return {emit(Op::load,{tile.node},"plain256-le-v1")}; }
    FeaturesSymbol features(BitsSymbol bits) { return {emit(Op::features,{bits.node},feature_id)}; }
    FeaturesSymbol features_transitions(BitsSymbol bits) { return {emit(Op::features,{bits.node},transition_feature_id)}; }
    FeaturesSymbol load_features(TileSymbol tile) { return {emit(Op::load_features,{tile.node},feature_id)}; }
    SizeSymbol model(FixedModel, FeaturesSymbol fields) { return {emit(Op::model,{fields.node},FixedModel::id)}; }
    SizeSymbol model(TransitionModel, FeaturesSymbol fields) { return {emit(Op::model,{fields.node},TransitionModel::id)}; }
};

Graph record_named(const NamedAnalysis<>&);
Graph record_function(const SourceChild&);
Graph record_transitions(const SourceChild&);
// One bounded execution rewrite. It does not edit Graph::source or model id.
bool fuse_load_features(Graph&);
bool same_graph(const Graph&, const Graph&);
void print_graph(std::ostream&, const Graph&);

} // namespace ikea::composition
