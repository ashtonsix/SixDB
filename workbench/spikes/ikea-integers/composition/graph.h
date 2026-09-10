#pragma once
#include "authoring.h"
#include <array>
#include <ostream>
#include <string>
#include <vector>

namespace ikea::integers::composition {
enum class Op { body,tail,join12,less_than,masked_sum };
struct Node {
    Op op; std::vector<unsigned> inputs; std::string identity,path;
    bool operator==(const Node&) const=default;
};
struct Graph { std::uint64_t source_id; std::vector<Node> nodes; unsigned result; };
struct Value { unsigned id; };
struct Group16 {};
struct Cutoff {};
struct Recorder {
    Graph graph;
    Value add(Op op,std::vector<unsigned> inputs,const char* identity,const char* path="") {
        auto id=unsigned(graph.nodes.size());graph.nodes.push_back({op,std::move(inputs),identity,path});return {id};
    }
    Value read_body(Body8,Group16) {return add(Op::body,{},Body8::contract,"values.body");}
    template<class Tail> Value read_tail(Tail,Group16) {return add(Op::tail,{},Tail::contract,"values.tail");}
    Value join12(Value high,Value low) {return add(Op::join12,{high.id,low.id},"(u16(high)<<4)|low");}
    Value less_than(Value values,Cutoff) {return add(Op::less_than,{values.id},"u12<cutoff; position-mask16");}
    Value masked_sum(Value values,Value mask) {return add(Op::masked_sum,{values.id,mask.id},"sum-selected-u12-to-u64");}
};
template<class Tail> Graph record(const Column<Tail>& source) {
    Recorder recorder{{source.source_id,{},0}};
    recorder.graph.result=filtered_sum16(recorder,source,Group16{},Cutoff{}).id;
    return std::move(recorder.graph);
}
void print_graph(std::ostream&,const Graph&);
} // namespace ikea::integers::composition
