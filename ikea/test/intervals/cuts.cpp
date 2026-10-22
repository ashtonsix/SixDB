#include "cuts_model.h"
#include <array>
#include <string_view>
namespace {
namespace bl = ikea::intervals;
void exhaustive() {
    for (unsigned encoded = 0; encoded < 81; ++encoded) {
        unsigned value = encoded;
        std::vector<unsigned> cuts{0};
        for (unsigned r = 0; r < 4; ++r) { cuts.push_back(cuts.back() + value % 3); value /= 3; }
        const auto borrowed = bl::from_cuts(std::span(cuts));
        cuts_model::check(borrowed, cuts, 0);
        for (unsigned mask = 0; mask < 16; ++mask) {
            const std::array bits{std::byte(mask)};
            const bl::selected_source chosen{borrowed, bl::rank_selection{bits, true}};
            cuts_model::check(chosen, cuts, mask);
        }
    }
}
void domains() {
    const std::array<std::uint64_t, 5> cuts{UINT64_MAX - 8, UINT64_MAX - 6,
                                         UINT64_MAX - 6, UINT64_MAX - 2, UINT64_MAX};
    const auto g = bl::from_cuts(std::span(cuts));
    const auto ends = bl::from_ends(bl::direct_source{cuts.data() + 1, 4u}, cuts[0]);
    for (auto q : cuts) assert(bl::reverse(g, q) == bl::reverse(ends, q));
    assert(bl::reverse(g, UINT64_MAX - 7) == 0);
    assert(bl::reverse(g, UINT64_MAX - 6) == 2);
    assert(bl::reverse(g, UINT64_MAX) == 4);
    assert(bl::reverse(g, -1) == 4);
    assert((bl::overlap(g, UINT64_MAX - 7, UINT64_MAX) == bl::rank_window{0, 4}));
    const std::array<std::string_view, 5> words{"aa", "ab", "ab", "az", "zz"};
    const auto strings = bl::from_cuts(std::span(words));
    assert(bl::reverse(strings, std::string_view("ab")) == 2);
    assert(bl::reverse(strings, std::string_view("a")) == 4);
    assert(bl::reverse(strings, std::string_view("zz")) == 4);
    assert((bl::overlap(strings, std::string_view("ac"), std::string_view("b")) == bl::rank_window{2, 4}));
    struct wide { std::uint64_t hi, lo; auto operator<=>(const wide &) const = default; };
    const std::array<wide, 4> values{{{0, UINT64_MAX}, {1, 0}, {1, 0}, {UINT64_MAX, UINT64_MAX}}};
    const auto w = bl::from_cuts(std::span(values));
    assert(bl::reverse(w, wide{1, 0}) == 2);
    assert((bl::overlap(w, wide{1, 0}, values.back()) == bl::rank_window{2, 3}));
    // A compact origin type need not force the endpoint/query domain to narrow.
    struct projected {
        unsigned size() const { return 2; }
        unsigned origin() const { return 0; }
        std::uint64_t at(unsigned rank) const { return UINT64_C(1) << (40 + rank); }
        unsigned owner_rank(std::uint64_t point) const { return point < at(0) ? 0 : 1; }
    };
    const projected p;
    assert(bl::reverse(p, UINT64_C(1) << 40) == 1);
    assert((bl::overlap(p, 0u, UINT64_C(1) << 40) == bl::rank_window{0, 1}));
    assert(bl::locate(p, 1)->first == (UINT64_C(1) << 40));
    const std::array<std::uint64_t, 1> one{123};
    const auto empty = bl::from_cuts(std::span(one));
    assert(bl::reverse(empty, 123) == 0 && bl::overlap(empty, 122, 124).empty());
    assert(!bl::locate(empty, 0));
    // The geometry API is independent of the packed representation's capacity.
    std::vector<std::uint64_t> many(1026);
    for (unsigned i = 0; i < many.size(); ++i) many[i] = (UINT64_C(1) << 48) + i * 7;
    const auto large = bl::from_cuts(std::span(many));
    assert(large.size() == 1025 && bl::reverse(large, many[1024]) == 1024);
    assert((bl::overlap(large, many[1000] + 1, many[1025]) == bl::rank_window{1000, 1025}));
    // Nested explicit selections intersect; neither can re-enable a rank.
    const std::array<std::byte, 1> a{std::byte{0x5}}, b{std::byte{0x6}};
    const auto small = bl::from_cuts(std::span(cuts));
    const bl::selected_source first{small, bl::rank_selection{a}};
    const bl::selected_source both{first, bl::rank_selection{b}};
    assert(bl::lower_bound(both, cuts[0]) == 2);
    assert(bl::reverse(both, UINT64_MAX - 7) == both.size());
    assert(bl::reverse(both, UINT64_MAX - 6) == 2);
    assert((bl::overlap(both, cuts[0], cuts.back()) == bl::rank_window{2, 3}));
    assert(both.selection_mask(0, 4) == 4);
    const auto all = bl::rank_selection::all();
    assert(all.next(0, bl::maximum_count) == 0 && all.next(bl::maximum_count, bl::maximum_count) == bl::maximum_count);
    assert(all.previous(0, bl::maximum_count) == bl::maximum_count && all.previous(bl::maximum_count, bl::maximum_count) == bl::maximum_count - 1);
    assert(bl::rank_selection{}.next(0, bl::maximum_count) == bl::maximum_count);
    assert(bl::rank_selection{}.previous(bl::maximum_count, bl::maximum_count) == bl::maximum_count);
    const bl::selected_source universal{large, all};
    assert(bl::reverse(universal, many[1024]) == 1024);
    const bl::selected_source none{large, bl::rank_selection{}};
    assert(bl::reverse(none, many[1024]) == none.size() && bl::overlap(none, many[0], many.back()).empty());
}
}
int main() { exhaustive(); domains(); }
