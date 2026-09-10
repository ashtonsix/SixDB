#include "prepared.h"
#include "transport.h"
#include <cstdlib>
#include <limits>
#include <new>

namespace ikea::integers::composition {
void Storage::Free::operator()(std::uint8_t* p) const {std::free(p);}
Storage::Storage(std::size_t n):size(n) {
    if(n>std::numeric_limits<std::size_t>::max()-63) throw std::bad_alloc();
    bytes.reset(static_cast<std::uint8_t*>(std::aligned_alloc(64,n?(n+63)/64*64:64)));
    if(!bytes) throw std::bad_alloc();
}
Placement place(std::shared_ptr<const Storage> owner,ParentKind parent,std::uint64_t source_id,
                std::size_t offset,std::size_t tiles) {
    const bool local=parent==ParentKind::packets8,middle=parent==ParentKind::body32_tail32_body32;
    return {parent,source_id,offset,tiles,96,64,
            {owner,offset,local?12u:middle?64u:96u,local?8u:middle?32u:64u,local?8u:middle?32u:64u},
            {owner,offset+(local?8:middle?32:64),local?12u:96u,local?8u:64u,local?4u:32u}};
}
bool prepare_impl(TailKind kind,std::uint64_t source_id,Graph graph,const Placement& p,
                  unsigned cutoff,Execution execution,Prepared& out,std::string& error) {
    const auto fail=[&](const char* text){error=text;return false;};
    if(!p.body.owner || p.body.owner!=p.tail.owner) return fail("body and tail must share the retained enclosing storage");
    if(source_id!=p.source_id) return fail("source identity differs from actual placement");
    if(p.parent!=ParentKind::packets8 && p.parent!=ParentKind::body64_tail32 &&
       p.parent!=ParentKind::body32_tail32_body32) return fail("unknown enclosing placement");
    const bool local=p.parent==ParentKind::packets8,middle=p.parent==ParentKind::body32_tail32_body32;
    if(kind!=(local?TailKind::local4:TailKind::scan4)) return fail("tail format is incompatible with enclosing placement");
    if(p.values_per_tile!=64 || p.tile_stride!=96) return fail("selected read region requires an exact 64-value, 96-byte placement");
    if(cutoff>4096) return fail("cutoff must be in 0..4096");
    const auto& owner=p.body.owner;
    if(p.offset>owner->size || p.tiles>(owner->size-p.offset)/96) return fail("incomplete enclosing tile extent");
    if((reinterpret_cast<std::uintptr_t>(owner->bytes.get()+p.offset)&31)!=0)
        return fail("placement must preserve the admitted 0/32 cache-line phases");
    if(p.tiles>std::numeric_limits<std::uint64_t>::max()/(64*4095)) return fail("sum exceeds scalar capacity");
    const auto matches=[](const ChildPlacement& child,std::size_t offset,unsigned stride,unsigned grain,unsigned bytes) {
        return child.offset==offset && child.repeat_stride==stride &&
               child.values_per_repeat==grain && child.bytes_per_repeat==bytes;
    };
    if(!matches(p.body,p.offset,local?12:middle?64:96,local?8:middle?32:64,local?8:middle?32:64) ||
       !matches(p.tail,p.offset+(local?8:middle?32:64),local?12:96,local?8:64,local?4:32))
        return fail("nested children do not witness the selected enclosing placement");
#if defined(__x86_64__)
    if(!__builtin_cpu_supports("avx2")) return fail("compiled native carrier requires AVX2");
#endif
    // This exact expansion is the correspondence contract of the named
    // compiled program. A changed authored or supplied graph is rejected;
    // retaining a different graph beside the old executable is not permitted.
    const std::vector<Node> expected{
        {Op::body,{},Body8::contract,"values.body"},
        {Op::tail,{},local?Local4::contract:Scan4::contract,"values.tail"},
        {Op::join12,{0,1},"(u16(high)<<4)|low",""},
        {Op::less_than,{2},"u12<cutoff; position-mask16",""},
        {Op::masked_sum,{2,3},"sum-selected-u12-to-u64",""}};
    if(graph.source_id!=source_id || graph.result!=4 || graph.nodes!=expected)
        return fail("recorded expansion differs from the named compiled read/filter/sum program");
    // Explicit registry for three compiled parent read regions, not a general
    // graph lowerer. Each read region invokes the authored read12 body.
    auto program=std::make_shared<Program>(Program{{{
        {local?ikea_i12_read_local:middle?ikea_i12_read_scan_middle:ikea_i12_read_scan},{ikea_i12_filter},{ikea_i12_sum}}}});
    Prepared result;
    result.owner=owner;result.program=program;result.graph=std::move(graph);
    result.binding={owner->bytes.get()+p.offset,p.tiles,cutoff,program.get()};
    result.entry=execution==Execution::cps?ikea_i12_cps:
                 local?ikea_i12_inline_local:middle?ikea_i12_inline_scan_middle:ikea_i12_inline_scan;
    out=std::move(result);error.clear();return true;
}
void print_graph(std::ostream& out,const Graph& graph) {
    out<<"source "<<graph.source_id<<"; group input in {0, 16, 32, 48}; cutoff input in 0..4096\n";
    for(unsigned i=0;i<graph.nodes.size();++i) {
        const auto& node=graph.nodes[i];out<<'%'<<i<<" = "<<node.identity<<'(';
        for(auto input:node.inputs) out<<'%'<<input<<' ';
        out<<')';if(!node.path.empty())out<<" child="<<node.path;out<<'\n';
    }
    out<<"result %"<<graph.result<<"; %2 remains live across %3 for %4\n";
}
} // namespace ikea::integers::composition
