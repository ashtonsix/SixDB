// Runtime-width construction bodies; each writers_*.cpp compiles one value type.
#include <ikea/seriespack/writer.h>
#include <numeric>

namespace ikea::seriespack {
namespace writers_detail {
inline std::expected<unsigned, error> rows(const dense_output& target) {
    if (target.width == 0 || target.width > 64 ||
        (target.storage != geometry::local && target.storage != geometry::striped) ||
        (target.storage == geometry::striped && !striped_width(target.width)))
        return std::unexpected(error::description);
    return target.storage == geometry::local ? 8u : 32 * (8 / std::gcd(target.width % 8, 8u));
}
template <class L> auto attach(const dense_output& target) {
    return view<L>::attach(target.count, {{{target.bytes, L::tile_bytes}, {}, {}}});
}
// Re-names every record to the caller's dense_output, which outlives this call.
template <class L> struct named {
    const dense_output& target;
    journal& effects;
    void before(const view<L>&, unsigned plane, std::size_t offset, std::size_t size) noexcept {
        effects.before(target, plane, offset, size);
    }
};
template <class L, class U>
std::expected<void, error> construct(const dense_output& target, std::span<const U> input,
                                     journal& effects) {
    const auto destination = attach<L>(target);
    if (!destination)
        return std::unexpected(destination.error());
    if (input.size() != target.count)
        return std::unexpected(error::range);
    if constexpr (L::width < sizeof(U) * 8)
        for (const auto value : input)
            if (value >> L::width)
                return std::unexpected(error::value);
    std::size_t needed = 0;
    author::visit_storage(*destination, [&](ikea::detail::write_span) { ++needed; });
    if (effects.used > effects.storage.size() || needed > effects.storage.size() - effects.used)
        return std::unexpected(error::effects);
    named<L> coverage{target, effects};
    author::initialize_unchecked(*destination, input.data(), coverage);
    return {};
}
template <class Body> auto dispatch(const dense_output& target, unsigned limit, Body&& body)
    -> decltype(body.template operator()<layout<1>>()) {
    decltype(body.template operator()<layout<1>>()) result = std::unexpected(error::description);
    detail::each<64>([&](auto k) {
        constexpr unsigned K = k + 1;
        if (K > limit || target.width != K)
            return;
        if (target.storage == geometry::local)
            result = body.template operator()<layout<K>>();
        else if constexpr (striped_width(K))
            result = body.template operator()<layout<K, geometry::striped>>();
    });
    return result;
}
} // namespace writers_detail

template <class U>
std::expected<void, error> initialize(const dense_output& target, std::span<const U> input,
                                      journal& effects) {
    if (auto rows = writers_detail::rows(target); !rows || target.width > sizeof(U) * 8)
        return std::unexpected(error::description);
    return writers_detail::dispatch(target, sizeof(U) * 8, [&]<class L>() {
        return writers_detail::construct<L, U>(target, input, effects);
    });
}
} // namespace ikea::seriespack
