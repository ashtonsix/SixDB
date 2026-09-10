#include "graph.h"

namespace ikea::composition {
unsigned Recorder::emit(Op op, std::vector<unsigned> inputs, std::string identity) {
    auto result = static_cast<unsigned>(graph.nodes.size());
    graph.nodes.push_back({op,std::move(inputs),std::move(identity)});
    return result;
}
Graph record_named(const NamedAnalysis<>& analysis) {
    Recorder ops; analysis.expose(ops); return std::move(ops.graph);
}
Graph record_function(const SourceChild& source) {
    Recorder ops; analyse(ops,source); return std::move(ops.graph);
}
Graph record_transitions(const SourceChild& source) {
    Recorder ops; analyse_transitions(ops,source); return std::move(ops.graph);
}
bool fuse_load_features(Graph& graph) {
    for (auto& node : graph.nodes) {
        if (node.op != Op::features ||
            (node.identity != feature_id && node.identity != transition_feature_id) || node.inputs.size()!=1) continue;
        auto index=node.inputs[0];
        if (index>=graph.nodes.size()) continue;
        auto& load=graph.nodes[index];
        if (load.op!=Op::load || load.identity!="plain256-le-v1" || load.inputs.size()!=1) continue;
        unsigned uses=0;
        for (const auto& user:graph.nodes) for (auto input:user.inputs) uses+=input==index;
        if (uses!=1) continue;
        node.op=Op::load_features; node.inputs=load.inputs; load.op=Op::removed;
        return true;
    }
    return false;
}
bool same_graph(const Graph& a,const Graph& b) {
    if(a.source.representation!=b.source.representation || a.source.path!=b.source.path ||
       a.root!=b.root || a.nodes.size()!=b.nodes.size()) return false;
    for(std::size_t i=0;i<a.nodes.size();++i) {
        const auto& x=a.nodes[i]; const auto& y=b.nodes[i];
        if(x.op!=y.op || x.inputs!=y.inputs || x.identity!=y.identity) return false;
    }
    return true;
}
void print_graph(std::ostream& out,const Graph& graph) {
    constexpr const char* names[]={"Repeat256","TileIndex","Load","Features","Model","Sum",
                                   "LoadFeatures","Removed"};
    out<<"actual representation "<<graph.source.representation<<" child "<<graph.source.path<<'\n';
    for(std::size_t i=0;i<graph.nodes.size();++i) {
        const auto& n=graph.nodes[i]; if(n.op==Op::removed) continue;
        out<<'%'<<i<<" = "<<names[static_cast<unsigned>(n.op)]<<'(';
        for(auto input:n.inputs) out<<'%'<<input<<' ';
        out<<") ["<<n.identity<<"]\n";
    }
    out<<"result %"<<graph.root<<'\n';
}
} // namespace ikea::composition
