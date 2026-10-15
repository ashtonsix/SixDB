#include "support.h"

template <unsigned N, unsigned Rows, C Op> void field_geometry() {
    constexpr auto live = ~std::uint64_t{0} >> (64 - Rows);
    for (unsigned width = 1; width <= 8; ++width)
        for (unsigned shift = 0; shift + width <= 8; ++shift)
            for (unsigned stride : {1u, 3u}) {
                // Tight columns and a code in the last byte of strided units.
                const unsigned offset = stride - 1;
                auto layout = *tp::layout::make(
                    stride, std::array<tp::code, 1>{
                                {{tp::byte(offset), tp::byte(shift), tp::byte(width)}}});
                std::array<tp::byte, 195> bytes{};
                for (unsigned i = 0; i < bytes.size(); ++i)
                    bytes[i] = i * 71;
                auto source = *tp::view::bind(layout, bytes, 65, stride);
                auto plan = *tp::reader<N, Rows>::make(layout, std::array<tp::byte, 1>{0});
                auto read = *tp::bind_reader(plan, source);
                auto filter = *tp::bind_filter(read);
                const auto limit = (std::uint64_t{1} << width) - 1;
                for (auto rhs : {std::uint64_t{0}, limit / 2, limit, limit + 1, ~std::uint64_t{0}})
                    for (auto active :
                         {std::uint64_t{0}, live, live & std::uint64_t{0xa53c09180001800f}}) {
                        std::uint64_t expected = 0;
                        for (unsigned r = 0; r < Rows; ++r) {
                            const auto value = (bytes[(1 + r) * stride + offset] >> shift) & limit;
                            if ((active >> r & 1) && oracle(Op, value, rhs))
                                expected |= std::uint64_t{1} << r;
                        }
                        assert(filter.template filter<Op>(1, rhs, active) == expected);
                    }
                // Admission precedes the constant-result shortcut too.
                assert(!filter.template filter<Op>(65, limit + 1, 1));
                if constexpr (Rows < 64)
                    assert(!filter.template filter<Op>(0, limit + 1, std::uint64_t{1} << Rows));
            }
}

template <unsigned N, unsigned Rows, C Op> void packet_coordinates() {
    auto layout = *tp::layout::make(2, std::array<tp::code, 2>{{{0, 3, 5}, {1, 0, 8}}});
    std::array<tp::byte, 2 * Rows> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = i * 71;
    auto source = *tp::view::bind(layout, bytes, Rows, 2);
    auto plan = *tp::reader<N, Rows>::make(layout, std::array<tp::byte, 3>{1, tp::hole, 0},
                                           std::array<unsigned, 2>{1, 2});
    auto read = *tp::bind_reader(plan, source);
    assert(!tp::bind_filter(read));
    const auto native = tp::native_reader(read);
    for (auto active : {std::uint64_t{0}, std::uint64_t{1}, (std::uint64_t{1} << Rows) - 1}) {
        assert(native.admit(0, active));
        const auto packet = native.get_unchecked(0, active);
        // Group 0: B0 B1 ...; group 1: hole A0 hole A1 ... .
        // This independent expectation does not use the plan's offset function.
        std::uint64_t eligible = 0;
        for (unsigned r = 0; r < Rows; ++r)
            if (active >> r & 1)
                eligible |= (std::uint64_t{1} << r) | (std::uint64_t{1} << (Rows + 2 * r + 1));
        for (auto rhs : {0ull, 15ull, 31ull, 128ull, 255ull, 256ull, ~0ull}) {
            std::uint64_t expected = 0;
            for (unsigned r = 0; r < Rows; ++r)
                if (active >> r & 1) {
                    if (oracle(Op, bytes[2 * r + 1], rhs))
                        expected |= std::uint64_t{1} << r;
                    if (oracle(Op, bytes[2 * r] >> 3, rhs))
                        expected |= std::uint64_t{1} << (Rows + 2 * r + 1);
                }
            assert((tp::filter_values<Op, N>(packet, rhs, eligible) == expected));
        }
    }
    auto empty = *tp::reader<N, Rows>::make(layout, {});
    auto empty_read = *tp::bind_reader(empty, source);
    assert(!tp::bind_filter(empty_read));
}

void check_tuple_filters() {
    [&]<C... Ops>(operations<Ops...>) {
        (field_geometry<8, 1, Ops>(), ...);
        (field_geometry<8, 8, Ops>(), ...);
        (packet_coordinates<8, 2, Ops>(), ...);
#if defined(__aarch64__) || defined(__AVX2__)
        (field_geometry<64, 16, Ops>(), ...);
        (field_geometry<64, 32, Ops>(), ...);
        (field_geometry<64, 64, Ops>(), ...);
        (packet_coordinates<64, 4, Ops>(), ...);
#endif
    }(operations<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
}
