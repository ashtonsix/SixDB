#pragma once
#include <ikea/effects.h>
#include <type_traits>

namespace ikea::seriespack::detail {
// Physical drivers freeze placement metadata by value. Keep effects attached
// to the caller's named source while kernels operate on that private copy.
template <class Source, class Effects>
    requires ikea::effects<Effects, Source>
struct named_effects {
    const Source& source;
    Effects& effects;
    void before(const Source&, unsigned plane, std::size_t offset, std::size_t size) noexcept {
        effects.before(source, plane, offset, size);
    }
};
template <class Source, class Effects>
    requires ikea::effects<Effects, Source>
decltype(auto) name_effects(const Source& source, Effects& effects) {
    if constexpr (std::is_same_v<std::remove_cvref_t<Effects>, ikea::no_effects>)
        return (effects);
    else
        return named_effects<Source, Effects>{source, effects};
}
} // namespace ikea::seriespack::detail
