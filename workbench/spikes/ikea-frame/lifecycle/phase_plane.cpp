#include "phase_plane.h"
#include <ikea/seriespack/author/store.h>
#include <array>
#include <cstdlib>
#include <utility>

namespace frame_lifecycle::phase {
namespace {
namespace sp = ikea::seriespack;
using Replace = void(*)(unsigned,std::uint8_t*,std::span<const PlaneEdit>);
template<unsigned K> void replace(unsigned rows, std::uint8_t* body, std::span<const PlaneEdit> edits) {
    using F=sp::layout<K>;
    const auto bytes=(rows+7)/8*K;
    // The admitted source and exact private copy already prove this placement.
    auto view=sp::view<F>::attach_unchecked(rows,{{{std::as_writable_bytes(std::span(body,bytes)),F::tile_bytes},{},{}}});
    for (const auto& e:edits) sp::author::store_unchecked(view,e.row,e.value);
}
template<std::size_t... I> constexpr auto entries(std::index_sequence<I...>) {
    return std::array<Replace,sizeof...(I)>{replace<I+1>...};
}
constexpr auto table=entries(std::make_index_sequence<64>{});
}
void replace_private_plane(unsigned width,unsigned rows,std::uint8_t* body,std::span<const PlaneEdit> edits) {
    if (edits.empty()) return;
    if (!width || width>64) std::abort();
    table[width-1](rows,body,edits);
}
}
