#include "inline_ops.h"
#include "stage.h"
#include "../predictor/reference.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <numeric>
#include <random>

using namespace ikea::composition;
namespace {
std::uint64_t feature_cases=0, analysis_cases=0;
void check_features(const std::uint8_t* bytes) {
    const auto basic=ikea::bec_predictor::reference_features(bytes);
    const auto all=ikea::bec_predictor::reference_features_all(bytes);
    assert(ikea_comp_features256(bytes)==basic);
    assert(ikea_comp_features_transitions(bytes)==(all&0xffffff));
    assert(ikea_comp_features_quadrants(bytes)==all);
    assert(ikea_comp_predict256(bytes)==ikea::bec_predictor::predict_size(basic));
    assert(ikea_comp_predict_transitions(bytes)==ikea::bec_predictor::predict_size_transitions(all));
    assert(ikea_comp_predict_quadrants(bytes)==ikea::bec_predictor::predict_size_quadrants(all));
    ++feature_cases;
}
std::shared_ptr<Segment> make_segment(Layout layout,std::size_t count,std::mt19937_64& random) {
    auto segment=std::make_shared<Segment>();
    auto stride=layout==Layout::contiguous?32u:48u;
    segment->description={{layout==Layout::contiguous?101u:202u,"field.membership.child.tiles"},layout,256,3,stride,count};
    segment->bytes.resize(count?3+(count-1)*stride+32:3);
    for(auto& byte:segment->bytes) byte=static_cast<std::uint8_t>(random());
    return segment;
}
void check_segment(std::shared_ptr<Segment> segment) {
    const auto& d=segment->description;
    NamedAnalysis named{d.child,{}};
    auto a=record_named(named),b=record_function(d.child);
    assert(same_graph(a,b));
    auto fused=a;
    assert(fuse_load_features(fused));
    assert(!fuse_load_features(fused));
    assert(fused.source.representation==a.source.representation);
    std::uint64_t reference=0;
    for(std::size_t i=0;i<d.tile_count;++i)
        reference+=ikea::bec_predictor::predict_size(ikea::bec_predictor::reference_features(
            segment->bytes.data()+d.offset+i*d.stride));
    for(auto execution:{Execution::inline_named,Execution::inline_function,Execution::cps}) {
        for(const auto* graph:{&a,&b,&fused}) {
            Prepared prepared;
            std::string error;
            if(graph==&fused && execution!=Execution::cps) {
                assert(!prepare(segment,*graph,execution,prepared,error));
                continue;
            }
            assert(prepare(segment,*graph,execution,prepared,error));
            assert(prepared()==reference);
            ++analysis_cases;
        }
    }
    auto different_estimator=record_transitions(d.child);
    std::uint64_t transition_reference=0;
    for(std::size_t i=0;i<d.tile_count;++i)
        transition_reference+=ikea::bec_predictor::predict_size_transitions(ikea::bec_predictor::reference_features_all(
            segment->bytes.data()+d.offset+i*d.stride));
    for(bool fuse:{false,true}) {
        if(fuse) assert(fuse_load_features(different_estimator));
        // Node storage order is not the execution order. The lowerer follows
        // dependencies, so a new authoring body needs no whole-pipeline table.
        auto reordered=different_estimator;
        std::reverse(reordered.nodes.begin(),reordered.nodes.end());
        for(auto& node:reordered.nodes) for(auto& input:node.inputs)
            input=static_cast<unsigned>(reordered.nodes.size()-1)-input;
        reordered.root=static_cast<unsigned>(reordered.nodes.size()-1)-reordered.root;
        Prepared prepared;std::string error;
        assert(prepare(segment,reordered,Execution::cps,prepared,error));
        assert(prepared()==transition_reference);
        assert(!prepare(segment,reordered,Execution::inline_function,prepared,error));
        ++analysis_cases;
    }
    Prepared retained; std::string error;
    assert(prepare(segment,fused,Execution::cps,retained,error));
    segment.reset();
    assert(retained()==reference);
}
}

int main() {
    constexpr std::uint64_t seed=0x256bec09ULL;
    std::mt19937_64 random(seed);
    std::array<std::uint8_t,64> bytes{};
    check_features(bytes.data());
    bytes.fill(255); check_features(bytes.data());
    for(unsigned bit=0;bit<256;++bit) {
        bytes.fill(0); bytes[bit/8]=static_cast<std::uint8_t>(1u<<(bit%8)); check_features(bytes.data());
        for(auto& byte:bytes) byte^=255;
        check_features(bytes.data());
    }
    for(unsigned offset:{7u,15u,23u}) {
        bytes.fill(0);
        for(unsigned value=0;value<65536;++value) {
            bytes[offset]=static_cast<std::uint8_t>(value);
            bytes[offset+1]=static_cast<std::uint8_t>(value>>8);
            check_features(bytes.data());
        }
    }
    std::array<unsigned,256> positions;
    std::iota(positions.begin(),positions.end(),0);
    for(unsigned pop=0;pop<=256;++pop) {
        for(unsigned sample=0;sample<4;++sample) {
            std::shuffle(positions.begin(),positions.end(),random);
            bytes.fill(0);
            for(unsigned i=0;i<pop;++i) bytes[positions[i]/8]|=static_cast<std::uint8_t>(1u<<(positions[i]%8));
            check_features(bytes.data());
            for(unsigned i=32;i<64;++i) bytes[i]=static_cast<std::uint8_t>(random());
            const auto pair=ikea_comp_features512(bytes.data());
            assert(pair.first==ikea::bec_predictor::reference_features(bytes.data()));
            assert(pair.second==ikea::bec_predictor::reference_features(bytes.data()+32));
        }
    }
    for(auto layout:{Layout::contiguous,Layout::strided})
        for(auto count:{0u,1u,2u,3u,4u,8u,16u,127u,256u}) check_segment(make_segment(layout,count,random));

    auto segment=make_segment(Layout::strided,3,random);
    auto graph=record_named({segment->description.child,{}});
    std::cout<<"A and B record the same graph:\n";
    print_graph(std::cout,graph);
    auto fused=graph;
    assert(fuse_load_features(fused));
    std::cout<<"Execution rewrite, same source and model:\n";
    print_graph(std::cout,fused);
    Prepared result; std::string error;
    auto transition_graph=record_transitions(segment->description.child);
    assert(prepare(segment,transition_graph,Execution::cps,result,error));
    std::cout<<"Different estimator, lowered without a prewritten pipeline table:\n";
    print_graph(std::cout,transition_graph);
    for(const auto& identity:result.code->operation_ids) std::cout<<"stage ["<<identity<<"]\n";
    auto wrong_model=graph;
    for(auto& node:wrong_model.nodes) if(node.op==Op::model) node.identity+="-different-weights";
    assert(!prepare(segment,wrong_model,Execution::cps,result,error));
    auto insufficient_features=graph;
    for(auto& node:insufficient_features.nodes) if(node.op==Op::model) node.identity=TransitionModel::id;
    assert(!prepare(segment,insufficient_features,Execution::cps,result,error));
    auto cycle=graph;cycle.nodes[3].inputs={4};
    assert(!prepare(segment,cycle,Execution::cps,result,error));
    auto reuse=graph;reuse.nodes.push_back({Op::model,{3},FixedModel::id});
    assert(!prepare(segment,reuse,Execution::cps,result,error));
    auto wrong_source=graph; ++wrong_source.source.representation;
    assert(!prepare(segment,wrong_source,Execution::cps,result,error));
    result={}; // Release the admitted snapshot before mutating test descriptions.
    segment->description.positions_per_tile=512;
    assert(!prepare(segment,graph,Execution::cps,result,error));
    segment->description.positions_per_tile=256;
    segment->description.stride=31;
    assert(!prepare(segment,graph,Execution::cps,result,error));
    segment->description.stride=48;
    segment->bytes.pop_back();
    assert(!prepare(segment,graph,Execution::cps,result,error));
    assert(!prepare({},graph,Execution::cps,result,error));

    std::cout<<"feature_cases,"<<feature_cases<<"\nanalysis_cases,"<<analysis_cases<<"\nstatus,pass\n";
}
