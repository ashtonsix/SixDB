#include "edge.h"
#include <cstdlib>

namespace tuple_probe {
template <unsigned Layout, unsigned Selected>
[[gnu::noinline]] packet read_entry(const byte* row) {
    return read_body<Layout, Selected>(row);
}
template <unsigned Layout, unsigned Selected>
[[gnu::noinline]] void write_entry(byte* row, packet value) {
    write_body<Layout, Selected>(row, value);
}
template <unsigned Layout> bound select(unsigned selected) {
#define ENTRY(S) case S: return {read_entry<Layout, S>, write_entry<Layout, S>}
    switch (selected) {
        ENTRY(1); ENTRY(2); ENTRY(3); ENTRY(4); ENTRY(5); ENTRY(6); ENTRY(7);
        default: std::abort();
    }
#undef ENTRY
}
bound bind(unsigned layout, unsigned selected) {
    switch (layout) {
        case 0: return select<0>(selected);
        case 1: return select<1>(selected);
        case 2: return select<2>(selected);
        case 3: return select<3>(selected);
        case 4: return select<4>(selected);
        case 5: return select<5>(selected);
        default: std::abort();
    }
}
} // namespace tuple_probe
