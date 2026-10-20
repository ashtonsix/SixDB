#include "batch.h"
namespace ht = ikea::hashtable;
struct masks { ht::word candidates, empties; };
extern "C" masks scalar16(const ht::byte* controls, std::size_t, std::size_t group,
                             ht::byte tag) {
    const auto* p = controls + group * 16;
    return {ht::equal_controls(p, tag), ht::equal_controls(p, 0)};
}
extern "C" masks batch32(const ht::byte* controls, std::size_t count, std::size_t group,
                            ht::byte tag) {
    const auto p = ht::batch::scan<2>(controls, count, group, 0, tag);
    return {p.candidates, p.empties};
}
extern "C" masks batch64(const ht::byte* controls, std::size_t count, std::size_t group,
                            ht::byte tag) {
    const auto p = ht::batch::scan<4>(controls, count, group, 0, tag);
    return {p.candidates, p.empties};
}
