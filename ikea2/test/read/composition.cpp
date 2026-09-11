#include "../support.h"
#include "../fixture.h"

template <class Expr, class Expected>
void check_expression(const Expr& expr, const Expected& expected, std::size_t count) {
#if defined(__aarch64__) || defined(__AVX2__)
    for (auto first : {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{256}, count})
        for (auto n : {std::size_t{0}, std::min(std::size_t{15}, count - first), count - first})
            for (auto seed : {0u, 1u, 0xffffu}) {
                auto mask = [=](std::size_t origin) -> std::uint16_t {
                    IKEA2_CHECK(origin % 16 == 0);
                    return seed == 0        ? 0
                           : seed == 0xffff ? 0xffff
                                            : std::uint16_t(0xb6dbu ^ (origin * 0x2311u));
                };
                for (auto cutoff : {std::uint64_t{0}, std::uint64_t{123}, std::uint64_t{1234567},
                                    std::uint64_t(-1)}) {
                    ikea2_test::scope scenario{"filtered range", first, n, seed};
                    std::uint64_t want = 0;
                    for (auto i = first; i < first + n; ++i)
                        if ((mask(i - i % 16) & (1u << (i % 16))) && expected[i] < cutoff)
                            want += expected[i];
                    IKEA2_CHECK(cp::sum_regions(expr, first, n, cutoff, mask) == want);
                }
            }
#endif
}
template <class F> void check_placed() {
    ikea2_test::format_scope<F> format_context{"placed read/composition"};
    placed<F> storage;
    const auto source = storage.source();
    const auto expr = cp::describe(source);
    for (std::size_t i = 0; i < storage.count; ++i)
        IKEA2_CHECK(sp::get_unchecked(source, i) == storage.truth[i]);
    check_expression(expr, storage.truth, storage.count);
    auto decoder = sp::bind_decoder<std::uint64_t>(source);
    std::vector<std::uint64_t> output(storage.count + 2, 0xbad);
    for (auto first :
         {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{513}, storage.count}) {
        const auto count = storage.count - first;
        decoder.read_unchecked(first, count, output.data() + 1);
        IKEA2_CHECK(output.front() == 0xbad && output[count + 1] == 0xbad);
        for (std::size_t j = 0; j < count; ++j)
            IKEA2_CHECK(output[j + 1] == storage.truth[first + j]);
        std::fill(output.begin(), output.end(), 0xbad);
    }
    for (std::size_t i = 0; i + 16 <= storage.count; i += 16) {
        decoder.read16_unchecked(i, output.data() + 1);
        for (unsigned j = 0; j < 16; ++j)
            IKEA2_CHECK(output[j + 1] == storage.truth[i + j]);
    }
}

void check_substitution() {
    // Replace a nested tail and the second head with sources having different
    // storage geometry/placement. Original row coordinates are retained.
    placed<sp::format<23, sp::geometry::local, 16>> a;
    placed<sp::format<7, sp::geometry::striped>> b;
    placed<sp::format<8, sp::geometry::local, 8>> c;
    auto av = a.source();
    const auto bv = b.source();
    const auto cv = c.source();
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const auto bytes = (a.planes[0].bytes.size() + page - 1) / page * page;
    auto* inaccessible = static_cast<std::uint8_t*>(
        mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    IKEA2_CHECK(inaccessible != MAP_FAILED);
    auto dead = sp::view<decltype(av)::format_type>::attach(
        a.count,
        std::array<sp::plane<const std::uint8_t>, 3>{{{{inaccessible, bytes}, a.planes[0].stride},
                                                      {a.planes[1].bytes, a.planes[1].stride},
                                                      {a.planes[2].bytes, a.planes[2].stride}}});
    IKEA2_CHECK(dead);
    av = *dead;
    const auto ax = cp::describe(av);
    const auto bx = cp::describe(bv);
    const auto cx = cp::describe(cv);
    using P = cp::payload_expression<7, decltype(ax.payload.body), decltype(bx.payload.tail)>;
    using X = cp::value_expression<23, 16, P, decltype(ax.head0), decltype(cx.head0)>;
    const X replacement{{ax.payload.body, bx.payload.tail}, ax.head0, cx.head0};
    std::vector<std::uint64_t> truth(a.count);
    for (std::size_t i = 0; i < a.count; ++i)
        truth[i] = ((a.truth[i] >> 15) << 15) | (c.truth[i] << 7) | b.truth[i];
    check_expression(replacement, truth, a.count);
    auto bound = cp::prepare(replacement, a.count);
    IKEA2_CHECK(bound);
    IKEA2_CHECK(!cp::prepare(replacement, a.count + 1));
#if defined(__aarch64__) || defined(__AVX2__)
    std::uint64_t total = 0;
    for (auto x : truth)
        total += x;
    auto all = [](std::size_t) { return 0xffff; };
    IKEA2_CHECK(bound->sum(0, a.count, std::uint64_t(-1), all) == total);
    IKEA2_CHECK(!bound->sum(a.count, 1, std::uint64_t(-1), all));
#endif
    cp::recorder record;
    const auto rows = record.rows();
    const auto active = record.active();
    const auto result = cp::selected_sum(record, replacement, rows, active, std::uint64_t{1234567});
    IKEA2_CHECK(record.nodes()[result.id].operation == cp::instruction::modulo_sum);
    unsigned loads = 0;
    for (const auto& node : record.nodes())
        if (node.operation == cp::instruction::load) {
            ++loads;
            IKEA2_CHECK(node.left == rows.id && node.selection == active.id);
            IKEA2_CHECK((node.source == &av && node.field == cp::field_kind::head0) ||
                        (node.source == &bv && node.field == cp::field_kind::tail) ||
                        (node.source == &cv && node.field == cp::field_kind::head0));
        }
    IKEA2_CHECK(loads == 3);
#if defined(__aarch64__) || defined(__AVX2__)
    // Empty prefilters must suppress even the unmodified unreadable source.
    IKEA2_CHECK(cp::sum_regions(ax, a.count, std::uint64_t(-1), [](std::size_t) { return 0; }) ==
                0);
#endif
    IKEA2_CHECK(munmap(inaccessible, bytes) == 0);
}

void check_composition() {
    sp::detail::each<64>([](auto k) {
        check_placed<sp::format<k + 1>>();
        if constexpr (k + 1 >= 8)
            check_placed<sp::format<k + 1, sp::geometry::local, 8>>();
        if constexpr (k + 1 >= 16)
            check_placed<sp::format<k + 1, sp::geometry::local, 16>>();
        if constexpr (sp::striped_width(k + 1))
            check_placed<sp::format<k + 1, sp::geometry::striped>>();
        if constexpr (k + 1 > 8 && sp::striped_width(k + 1 - 8))
            check_placed<sp::format<k + 1, sp::geometry::striped, 8>>();
        if constexpr (k + 1 > 16 && sp::striped_width(k + 1 - 16))
            check_placed<sp::format<k + 1, sp::geometry::striped, 16>>();
    });
    check_substitution();
}
