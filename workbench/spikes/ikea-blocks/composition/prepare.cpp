#include "prepared.h"
#include "lower.h"
#include "stage.h"
#include <limits>

namespace ikea::composition {
namespace {
bool native_applicable() {
#if defined(__x86_64__)
    if(!__builtin_cpu_supports("avx2")) return false;
#if defined(__AVX512BITALG__) && defined(__AVX512VL__)
    if(!__builtin_cpu_supports("avx512bitalg") || !__builtin_cpu_supports("avx512vl")) return false;
#endif
#endif
    return true;
}
}
bool prepare(std::shared_ptr<const Segment> segment,const Graph& graph,Execution execution,
             Prepared& out,std::string& error) {
    const auto fail=[&](const char* text){error=text;return false;};
    if(!segment) return fail("missing immutable segment owner");
    const auto& d=segment->description;
    if(d.positions_per_tile!=256) return fail("model requires independent 256-position tiles");
    if(d.stride<32 || (d.layout==Layout::contiguous && d.stride!=32)) return fail("unsupported tile stride");
    if(d.offset>segment->bytes.size()) return fail("source offset outside retained bytes");
    if(d.tile_count) {
        const auto available=segment->bytes.size()-d.offset;
        if(available<32 || d.tile_count-1>(available-32)/d.stride) return fail("each tile needs 32 readable bytes");
    }
    if(d.tile_count>std::numeric_limits<std::uint64_t>::max()/47) return fail("sum would exceed scalar accumulator");
    if(!native_applicable()) return fail("compiled native carrier/features unavailable on this target");
    if(graph.source.representation!=d.child.representation || graph.source.path!=d.child.path)
        return fail("graph refers to a different actual source child");
    auto program=std::make_shared<Program>();
    if(!lower_linear(graph,*program,error)) return false;
    if(execution!=Execution::cps && !mapped_inline_recipe(*program))
        return fail("graph has no selected inline implementation; use its lowered CPS program");
    Entry entry=nullptr;
    switch(execution) {
        case Execution::inline_named:entry=ikea_comp_run_named;break;
        case Execution::inline_function:entry=ikea_comp_run_function;break;
        case Execution::cps:entry=ikea_comp_run_cps;break;
    }
    Prepared result;
    const auto* first=segment->bytes.data();
    if(d.offset) first+=d.offset;
    result.binding={first,d.tile_count,d.stride,program.get()};
    result.owner=std::move(segment); result.code=std::move(program);result.entry=entry;
    out=std::move(result); error.clear(); return true;
}
} // namespace ikea::composition
