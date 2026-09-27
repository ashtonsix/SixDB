#include "kernels.h"
namespace v3_spike {
[[gnu::noinline]] std::uint64_t maintained(const bc::source& a, const bc::source& b,
                                          const bc::byte* qa, const bc::byte* qb) {
    return finish(bc::native::read_pair(a, b), qa, qb);
}
template <unsigned Method>
[[gnu::noinline]] std::uint64_t factored(const bc::source& a, const bc::source& b,
                                        const bc::byte* qa, const bc::byte* qb) {
    return finish(bc::native::join(decode_loaded<Method>(start(load_scratch(a), a)),
                                   decode_loaded<Method>(start(load_scratch(b), b))), qa, qb);
}
[[gnu::noinline]] std::uint64_t paired(const bc::source& a, const bc::source& b,
                                      const bc::byte* qa, const bc::byte* qb) {
    return finish(interleaved(start(load_scratch(a), a), start(load_scratch(b), b)), qa, qb);
}
const std::array<candidate, 7> candidates{{
    {"maintained", maintained, false}, {"factored", factored<0>, false},
    {"tbl2", factored<1>, false}, {"tbx4", factored<2>, false},
    {"interleaved", paired, false}, {"sve", consume_sve, true},
    {"sve_interleaved", consume_sve_interleaved, true}}};
}
