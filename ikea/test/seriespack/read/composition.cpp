#include "../support.h"
#include "../fixture.h"
#include "../../scope.h"

template <class Reader>
concept temporary_reader_erasable = requires(Reader&& operation) {
    std::move(operation).template erase<std::uint64_t>();
};
template <class View>
concept temporary_view_bindable = requires(View&& source) {
    sp::bind_reader<std::uint64_t>(std::move(source));
};
static_assert(!temporary_view_bindable<sp::const_view<sp::layout<7>>>);
static_assert(!temporary_reader_erasable<decltype(sp::bind_reader(
    std::declval<const sp::const_view<sp::layout<7>>&>()))>);
static_assert(temporary_reader_erasable<sp::reader<sp::dense_source<sp::layout<7>>>>);

template <class Expr, class Expected>
void check_expression(const Expr& expr, const Expected& expected, std::size_t count) {
#if defined(__aarch64__) || defined(__AVX2__)
    for (auto first : {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{256}, count})
        for (auto n : {std::size_t{0}, std::min(std::size_t{15}, count - first), count - first})
            for (auto seed : {0u, 1u, 0xffffu}) {
                auto mask = [=](std::size_t origin) -> std::uint16_t {
                    IKEA_CHECK(origin % 16 == 0);
                    return seed == 0        ? 0
                           : seed == 0xffff ? 0xffff
                                            : std::uint16_t(0xb6dbu ^ (origin * 0x2311u));
                };
                for (auto cutoff : {std::uint64_t{0}, std::uint64_t{123}, std::uint64_t{1234567},
                                    std::uint64_t(-1)}) {
                    ikea_test::scope scenario{"filtered range", first, n, seed};
                    std::uint64_t want = 0;
                    for (auto i = first; i < first + n; ++i)
                        if ((mask(i - i % 16) & (1u << (i % 16))) && expected[i] < cutoff)
                            want += expected[i];
                    IKEA_CHECK(cp::sum_regions(expr, first, n, cutoff, mask) == want);
                }
            }
#endif
}
template <class F> void check_placed() {
    ikea_test::format_scope<F> format_context{"placed read/composition"};
    placed<F> storage;
    const auto source = storage.source();
    const auto expr = cp::expression(source);
    for (std::size_t i = 0; i < storage.count; ++i)
        IKEA_CHECK(sp::get_unchecked(source, i) == storage.truth[i]);
    check_expression(expr, storage.truth, storage.count);
    auto typed_reader = sp::bind_reader(source);
    auto decoder = typed_reader.template erase<std::uint64_t>();
    std::vector<std::uint64_t> output(storage.count + 2, 0xbad);
    for (auto first :
         {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{513}, storage.count}) {
        const auto count = storage.count - first;
        decoder.read_unchecked(first, count, output.data() + 1);
        IKEA_CHECK(output.front() == 0xbad && output[count + 1] == 0xbad);
        for (std::size_t j = 0; j < count; ++j)
            IKEA_CHECK(output[j + 1] == storage.truth[first + j]);
        std::fill(output.begin(), output.end(), 0xbad);
    }
    for (std::size_t i = 0; i + 16 <= storage.count; i += 16) {
        decoder.read16_unchecked(i, output.data() + 1);
        for (unsigned j = 0; j < 16; ++j)
            IKEA_CHECK(output[j + 1] == storage.truth[i + j]);
    }
}

void check_substitution() {
    // Replace a nested tail and the second head with sources having different
    // storage geometry/placement. Original row coordinates are retained.
    placed<sp::layout<23, sp::geometry::local, 16>> a;
    placed<sp::layout<7, sp::geometry::striped>> b;
    placed<sp::layout<8, sp::geometry::local, 8>> c;
    auto av = a.source();
    const auto bv = b.source();
    const auto cv = c.source();
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const auto bytes = (a.planes[0].bytes.size() + page - 1) / page * page;
    auto* inaccessible = static_cast<std::uint8_t*>(
        mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    IKEA_CHECK(inaccessible != MAP_FAILED);
    auto dead = sp::const_view<decltype(av)::layout_type>::attach(
        a.count,
        std::array<sp::plane<const ikea::byte>, 3>{{{std::as_bytes(std::span(inaccessible, bytes)), a.planes[0].stride},
                                                      {a.planes[1].bytes, a.planes[1].stride},
                                                      {a.planes[2].bytes, a.planes[2].stride}}});
    IKEA_CHECK(dead);
    av = *dead;
    const auto ax = cp::expression(av);
    const auto bx = cp::expression(bv);
    const auto cx = cp::expression(cv);
    using P = cp::payload_expression<7, decltype(ax.payload.body), decltype(bx.payload.tail)>;
    using X = cp::value_expression<23, 16, P, decltype(ax.head0), decltype(cx.head0)>;
    const X replacement{{ax.payload.body, bx.payload.tail}, ax.head0, cx.head0};
    std::vector<std::uint64_t> truth(a.count);
    for (std::size_t i = 0; i < a.count; ++i)
        truth[i] = ((a.truth[i] >> 15) << 15) | (c.truth[i] << 7) | b.truth[i];
    check_expression(replacement, truth, a.count);
    auto bound = sp::bind_reader(replacement, a.count);
    IKEA_CHECK(bound);
    IKEA_CHECK(!sp::bind_reader(replacement, a.count + 1));
    // The unused parent payload is PROT_NONE. Typed and erased whole reads,
    // scalar edges and searches must evaluate only the substituted leaves.
    const auto erased = bound->erase<std::uint64_t>();
    std::vector<std::uint64_t> output(a.count + 2, 0xbad);
    for (auto first : {std::size_t{0}, std::size_t{1}, std::size_t{17}, a.count}) {
        const auto n = a.count - first;
        const auto target = std::span(output).subspan(1, n);
        IKEA_CHECK(bound->read(first, target));
        IKEA_CHECK(output.front() == 0xbad && output[n + 1] == 0xbad);
        for (std::size_t j = 0; j < n; ++j)
            IKEA_CHECK(output[j + 1] == truth[first + j]);
        std::fill(output.begin(), output.end(), 0xbad);
        IKEA_CHECK(erased.read(first, target));
        for (std::size_t j = 0; j < n; ++j)
            IKEA_CHECK(output[j + 1] == truth[first + j]);
        std::fill(output.begin(), output.end(), 0xbad);
    }
    for (std::size_t i = 0; i < a.count; ++i)
        IKEA_CHECK(bound->get(i) == truth[i] && erased.get(i) == truth[i]);
    IKEA_CHECK(!bound->get(a.count));
    IKEA_CHECK(!bound->read(a.count, std::span(output).first(1)));
    for (auto first : {0u, 1u, 17u})
        for (auto n : {0u, 7u, 16u, 63u}) {
            constexpr std::uint64_t active = 0xb6db6db6db6db6db;
            const auto query = truth[first + n / 2];
            std::uint64_t matches = 0;
            unsigned index = n;
            for (unsigned j = 0; j < n; ++j)
                if ((active & (std::uint64_t{1} << j)) && truth[first + j] == query) {
                    matches |= std::uint64_t{1} << j;
                    index = std::min(index, j);
                }
            IKEA_CHECK(bound->filter<ikea::comparison::equal>(first, n, query, active) == matches);
            IKEA_CHECK(bound->indexof(first, n, query, active) == index);
        }
#if defined(__aarch64__) || defined(__AVX2__)
    std::uint64_t total = 0;
    for (auto x : truth)
        total += x;
    auto all = [](std::size_t) { return 0xffff; };
    IKEA_CHECK(bound->sum(0, a.count, std::uint64_t(-1), all) == total);
    IKEA_CHECK(!bound->sum(a.count, 1, std::uint64_t(-1), all));
#endif
    cp::recorder record;
    const auto rows = record.rows();
    const auto active = record.active();
    const auto result = cp::selected_sum(record, replacement, rows, active, std::uint64_t{1234567});
    IKEA_CHECK(record.nodes()[result.id].operation == cp::instruction::modulo_sum);
    unsigned loads = 0;
    for (const auto& node : record.nodes())
        if (node.operation == cp::instruction::load) {
            ++loads;
            IKEA_CHECK(node.left == rows.id && node.selection == active.id);
            IKEA_CHECK((node.source == &av && node.field == cp::field_kind::head0) ||
                        (node.source == &bv && node.field == cp::field_kind::tail) ||
                        (node.source == &cv && node.field == cp::field_kind::head0));
        }
    IKEA_CHECK(loads == 3);
#if defined(__aarch64__) || defined(__AVX2__)
    // Empty prefilters must suppress even the unmodified unreadable source.
    IKEA_CHECK(cp::sum_regions(ax, a.count, std::uint64_t(-1), [](std::size_t) { return 0; }) ==
                0);
#endif
    IKEA_CHECK(munmap(inaccessible, bytes) == 0);
}

void check_bounded_physical_erasure() {
    using L = sp::layout<8>;
    constexpr std::size_t leaf_count = 64, bound_count = 17;
    std::array<ikea::byte, 96> placed_bytes{};
    std::array<ikea::byte, leaf_count> dense_bytes{};
    for (std::size_t row = 0; row < leaf_count; ++row) {
        const auto value = ikea::byte((row * 13 + 7) & 255);
        placed_bytes[row / 8 * 12 + row % 8] = dense_bytes[row] = value;
    }
    const auto placed = *sp::const_view<L>::attach(
        leaf_count, {{{placed_bytes, 12}, {}, {}}});
    const sp::dense_source<L> dense{dense_bytes.data(), leaf_count};
    auto check = [&](const auto& source) {
        const auto typed = sp::bind_reader(cp::expression(source), bound_count);
        IKEA_CHECK(typed && typed->size() == bound_count);
        const auto erased = typed->template erase<std::uint64_t>();
        IKEA_CHECK(erased.size() == bound_count);
        IKEA_CHECK(typed->get(bound_count - 1) == ((16 * 13 + 7) & 255));
        IKEA_CHECK(erased.get(bound_count - 1) == ((16 * 13 + 7) & 255));
        IKEA_CHECK(!typed->get(bound_count) && !erased.get(bound_count));
        std::array<std::uint64_t, bound_count> output;
        IKEA_CHECK(erased.read(0, output));
        for (std::size_t row = 0; row < bound_count; ++row)
            IKEA_CHECK(output[row] == ((row * 13 + 7) & 255));
        std::array<std::uint64_t, 2> refused{0xbad, 0xbad};
        IKEA_CHECK(!erased.read(bound_count - 1, refused));
        IKEA_CHECK(refused[0] == 0xbad && refused[1] == 0xbad);
        IKEA_CHECK(erased.read(bound_count, std::span<std::uint64_t>{}));
        IKEA_CHECK(!erased.read(bound_count + 1, std::span<std::uint64_t>{}));
    };
    check(placed);
    check(dense);
}

void check_composition() {
    sp::detail::each<64>([](auto k) {
        if constexpr (ikea::test::sampled_width(k + 1)) {
            check_placed<sp::layout<k + 1>>();
            if constexpr (k + 1 >= 8)
                check_placed<sp::layout<k + 1, sp::geometry::local, 8>>();
            if constexpr (k + 1 >= 16)
                check_placed<sp::layout<k + 1, sp::geometry::local, 16>>();
            if constexpr (sp::striped_width(k + 1))
                check_placed<sp::layout<k + 1, sp::geometry::striped>>();
            if constexpr (k + 1 > 8 && sp::striped_width(k + 1 - 8))
                check_placed<sp::layout<k + 1, sp::geometry::striped, 8>>();
            if constexpr (k + 1 > 16 && sp::striped_width(k + 1 - 16))
                check_placed<sp::layout<k + 1, sp::geometry::striped, 16>>();
        }
    });
    check_substitution();
    check_bounded_physical_erasure();
}
