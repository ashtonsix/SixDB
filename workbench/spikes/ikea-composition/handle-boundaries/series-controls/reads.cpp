#include "controls.h"

namespace series_controls {
namespace {
template <class U, class L> constexpr void add(columns<U>& result, unsigned index) {
    if constexpr (L::width <= sizeof(U) * 8) {
        // These exact function templates are also selected by the maintained
        // erase_dense_reader. No extra point/range/native implementation.
        result.points[index] = sp::detail::dense_point<L, U>;
        result.ranges[index] = sp::detail::dense_range<L, U>;
        result.regions[index] = sp::detail::dense_read16<L, U>;
    }
}
template <class U, std::size_t... K> constexpr columns<U> make(std::index_sequence<K...>) {
    columns<U> result;
    ([&] {
        add<U, sp::layout<K + 1>>(result, K);
        if constexpr (sp::striped_width(K + 1))
            add<U, sp::layout<K + 1, sp::geometry::striped>>(result, K + 64);
    }(), ...);
    return result;
}
template <class L> constexpr extent extent_of() {
    static_assert(std::has_single_bit(L::tile_rows) && L::tile_bytes <= 65535);
    return {std::uint16_t(L::tile_bytes), std::uint8_t(std::countr_zero(L::tile_rows))};
}
template <std::size_t... K> constexpr auto extent_table(std::index_sequence<K...>) {
    std::array<extent, keys> result{};
    ([&] {
        result[K] = extent_of<sp::layout<K + 1>>();
        if constexpr (sp::striped_width(K + 1)) result[K + 64] = extent_of<sp::layout<K + 1, sp::geometry::striped>>();
    }(), ...);
    return result;
}
template <class L> word typed(operation op, const command& c, unsigned reuse, word epoch) {
    const auto read = sp::bind_reader(sp::dense_source<L>{c.data, c.rows});
    word answer = 0;
    std::array<word, 32> out;
    for (unsigned q = 0; q < reuse; ++q) {
        switch (op) {
        case operation::point: answer += read.get_unchecked(point_row(c, q, epoch)); break;
        case operation::range: {
            auto [first, take] = range_rows(c, q, epoch);
            read.read_unchecked(first, take, out.data()); answer += consume(std::span(out).first(take)); break;
        }
        case operation::read16: {
            const auto first = region_row(c, q, epoch);
            sp::detail::dense_read16<L, word>(c.data, first, out.data());
            answer += consume(std::span(out).first(16)); break;
        }
        }
    }
    return answer;
}
template <std::size_t... K> constexpr auto typed_table(std::index_sequence<K...>) {
    std::array<typed_function, keys> result{};
    ([&] {
        result[K] = typed<sp::layout<K + 1>>;
        if constexpr (sp::striped_width(K + 1)) result[K + 64] = typed<sp::layout<K + 1, sp::geometry::striped>>;
    }(), ...);
    return result;
}
}
template <class U> const columns<U> read_columns = make<U>(std::make_index_sequence<64>{});
template const columns<std::uint8_t> read_columns<std::uint8_t>;
template const columns<std::uint16_t> read_columns<std::uint16_t>;
template const columns<std::uint32_t> read_columns<std::uint32_t>;
template const columns<word> read_columns<word>;
const std::array<extent, keys> extent_columns = extent_table(std::make_index_sequence<64>{});
const std::array<typed_function, keys> typed_columns = typed_table(std::make_index_sequence<64>{});
}
