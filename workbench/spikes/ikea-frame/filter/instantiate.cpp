// CMake compiles eight widths per TU, bounding native filter compile memory.
#include "catalogue.h"
#include "implementation.h"
namespace frame_filter {
template <> const std::array<operations, 8> catalogue<FRAME_HEADS, FRAME_FIRST>::entries =
    detail::table<FRAME_HEADS, FRAME_FIRST>(std::make_index_sequence<8>{});
}
