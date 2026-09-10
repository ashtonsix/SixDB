#include "inline_ops.h"
#include "stage.h"

namespace ikea::composition {
extern "C" std::uint64_t ikea_comp_run_cps(const void* erased_binding) {
    const auto& binding=*static_cast<const Binding*>(erased_binding);
    auto remaining=binding.count;
    if(!remaining) return 0;
    auto* tile=binding.first;
    const auto stride=binding.stride;
    const auto* first=binding.program->instructions.data();
    const auto entry=first->execute;
    const auto empty=zero_native();
    std::uint64_t accumulator=0;
    for(;;) {
        accumulator=entry(first+1,tile,accumulator,0,IKEA_COMP_BITS_UNPACK(empty));
        if(--remaining==0) return accumulator;
        tile+=stride;
    }
}
extern "C" std::uint64_t ikea_comp_run_named(const void* erased_binding) {
    const auto& binding=*static_cast<const Binding*>(erased_binding);
    InlineOps ops{binding};
    // Child identity was matched at prepare. The hot inline executor consumes
    // the attached binding; no string/schema lookup takes place in this loop.
    return NamedAnalysis<AttachedSource>{}.expose(ops);
}
extern "C" std::uint64_t ikea_comp_run_function(const void* erased_binding) {
    const auto& binding=*static_cast<const Binding*>(erased_binding);
    InlineOps ops{binding};
    return analyse(ops,AttachedSource{});
}
} // namespace ikea::composition
