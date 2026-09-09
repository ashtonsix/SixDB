#include "lower.h"
#include "stage.h"

namespace ikea::composition {
namespace {
enum class Value { invalid,loop,tile,bits,features2,features3,size,sum };
struct Lowerer {
    const Graph& graph;
    std::string& error;
    Program program;
    std::vector<unsigned> users,state;
    std::vector<Value> types;
    unsigned loop;

    Value fail(const char* text) {error=text;return Value::invalid;}
    void emit(Stage stage,const std::string& identity) {
        program.instructions.push_back({stage});program.operation_ids.push_back(identity);
    }
    Value visit(unsigned id) {
        if(id>=graph.nodes.size()) return fail("dependency outside graph");
        if(state[id]==1) return fail("cycle in tile body");
        if(state[id]==2) return types[id];
        if(users[id]>1) return fail("reused value requires an unsupported carrier lifetime");
        state[id]=1;
        const auto& n=graph.nodes[id];
        if(n.inputs.size()!=1) return fail("linear tile operation requires one input");
        auto input=visit(n.inputs[0]);
        if(input==Value::invalid) return input;
        auto result=Value::invalid;
        switch(n.op) {
            case Op::tile_index:
                if(input!=Value::loop || n.inputs[0]!=loop || n.identity!="i in [0,tile_count)")
                    return fail("tile index must belong to the admitted loop");
                result=Value::tile;break;
            case Op::load:
                if(input!=Value::tile || n.identity!="plain256-le-v1") return fail("load needs plain256 tile addressing");
                emit(ikea_comp_load,n.identity);result=Value::bits;break;
            case Op::features:
            case Op::load_features: {
                const bool fused=n.op==Op::load_features;
                if(input!=(fused?Value::tile:Value::bits)) return fail("feature operation has the wrong input carrier");
                if(n.identity==feature_id) {
                    emit(fused?ikea_comp_load_features:ikea_comp_features,n.identity);
                    result=Value::features2;
                } else if(n.identity==transition_feature_id) {
                    emit(fused?ikea_comp_load_transition_features_stage:ikea_comp_transition_features_stage,n.identity);
                    result=Value::features3;
                } else return fail("unknown feature contract");
                break;
            }
            case Op::model:
                if(n.identity==FixedModel::id) {
                    if(input!=Value::features2 && input!=Value::features3) return fail("fixed model requires distance and enum fields");
                    emit(ikea_comp_model,n.identity);
                } else if(n.identity==TransitionModel::id) {
                    if(input!=Value::features3) return fail("transition model requires its additional field");
                    emit(ikea_comp_transition_model_stage,n.identity);
                } else return fail("unknown numeric model contract");
                result=Value::size;break;
            default:return fail("operation is outside the supported linear tile body");
        }
        state[id]=2;types[id]=result;return result;
    }
};
}

bool lower_linear(const Graph& graph,Program& out,std::string& error) {
    const auto fail=[&](const char* text){error=text;return false;};
    if(graph.nodes.empty() || graph.nodes.size()>64 || graph.root>=graph.nodes.size()) return fail("invalid bounded graph size/root");
    const auto& root=graph.nodes[graph.root];
    if(root.op!=Op::sum || root.inputs.size()!=2 || root.identity!="u64: acc0=0; acc_next=acc+value")
        return fail("root must be the supported u64 zero-initialized sum");
    const auto loop=root.inputs[0];
    if(loop>=graph.nodes.size() || graph.nodes[loop].op!=Op::repeat256 ||
       !graph.nodes[loop].inputs.empty() || graph.nodes[loop].identity!="source.tile_count")
        return fail("sum must own a source-count Repeat256 loop");
    Lowerer lowering{graph,error,{},std::vector<unsigned>(graph.nodes.size()),
                     std::vector<unsigned>(graph.nodes.size()),std::vector<Value>(graph.nodes.size()),loop};
    for(const auto& node:graph.nodes) if(node.op!=Op::removed)
        for(auto input:node.inputs) {
            if(input>=graph.nodes.size()) return fail("dependency outside graph");
            ++lowering.users[input];
        }
    lowering.state[loop]=2;lowering.types[loop]=Value::loop;
    lowering.state[graph.root]=1;
    const auto body_type=lowering.visit(root.inputs[1]);
    if(body_type==Value::invalid) return false;
    if(body_type!=Value::size) return fail("sum input must be one predicted size");
    lowering.state[graph.root]=2;
    for(std::size_t i=0;i<graph.nodes.size();++i)
        if(graph.nodes[i].op!=Op::removed && lowering.state[i]!=2) return fail("unreachable/branching work is not silently discarded");
    lowering.emit(ikea_comp_done,root.identity);
    out=std::move(lowering.program);error.clear();return true;
}

bool mapped_inline_recipe(const Program& program) {
    const auto& entries=program.instructions;
    return entries.size()==4 && entries[0].execute==ikea_comp_load &&
           entries[1].execute==ikea_comp_features && entries[2].execute==ikea_comp_model &&
           entries[3].execute==ikea_comp_done;
}
} // namespace ikea::composition
