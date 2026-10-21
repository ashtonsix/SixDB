#include "boundlist.h"
#include "geometry.h"
#include "fused.h"
#include "scan-count.h"
#include "metadata.h"
#include <cassert>
#include <cstdio>
#include <ikea/streampack.h>
#include <random>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace bl = boundlist_spike;
namespace sp = ikea::streampack;

template <class T> struct guarded {
    std::size_t page = std::size_t(sysconf(_SC_PAGESIZE));
    void* allocation;
    T* data;
    explicit guarded(unsigned n) {
        assert(n * sizeof(T) <= page);
        allocation = mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(allocation != MAP_FAILED);
        assert(mprotect(static_cast<char*>(allocation) + page, page, PROT_NONE) == 0);
        data = reinterpret_cast<T*>(static_cast<char*>(allocation) + page) - n;
    }
    ~guarded() { assert(munmap(allocation, page * 2) == 0); }
};

template <bl::edge E, class T>
unsigned oracle(const std::vector<T>& ends, const std::vector<bool>& live, const T& p) {
    for (unsigned r = 0; r < ends.size(); ++r)
        if (live[r] && (E == bl::edge::after ? p < ends[r] : !(ends[r] < p))) return r;
    return ends.size();
}
template <bl::edge E, class V>
void methods(const V& v, const typename V::point_type& q, unsigned wanted) {
    assert((bl::seek<E, bl::method::binary>(v, q) == wanted));
    assert((bl::seek<E, bl::method::select_binary>(v, q) == wanted));
    assert((bl::seek<E, bl::method::scan>(v, q) == wanted));
    assert((bl::seek<E, bl::method::lerp>(v, q) == wanted));
    assert((bl::seek<E, bl::method::gallop>(v, q) == wanted));
    assert((bl::seek<E, bl::method::quad>(v, q) == wanted));
    assert((bl::seek<E, bl::method::hybrid>(v, q) == wanted));
    assert((bl::seek<E, bl::method::select_hybrid>(v, q) == wanted));
    std::array<V, 4> views{v, v, v, v};
    std::array<typename V::point_type, 4> queries{q, q, q, q};
    std::array<unsigned, 4> output{};
    bl::shotgun<E, 4>(views, queries, output);
    for (auto r : output) assert(r == wanted);
}

template <class W, class O, unsigned G> void arithmetic() {
    std::mt19937_64 rng(0x51e9 + sizeof(W) * 17 + sizeof(O) * 3 + G);
    for (unsigned n : {0, 1, 7, 8, 15, 16, 17, 31, 63, 64, 65, 127, 255, 256, 257, 511}) {
        const auto groups = (n + G - 1) / G;
        std::vector<W> widths(std::max(1u, groups * G * 2));
        std::vector<O> checkpoints(std::max(1u, groups)), ends(n), starts(n);
        std::vector<bool> live(n);
        std::vector<std::uint64_t> bits((n + 63) / 64 + 1);
        O total = sizeof(O) == 8 ? O(0xf000000000000000ULL) : 0;
        const auto origin = total;
        for (unsigned i = 0; i < n; ++i) {
            if (!(i % G)) checkpoints[i / G] = total;
            const auto w = W(i % 13 == 0 ? 0 : rng() % 17);
            widths[i / G * G * 2 + i % G] = w;
            starts[i] = total;
            total += w;
            ends[i] = total;
            live[i] = rng() % 4 != 0;
            bits[i / 64] |= std::uint64_t(live[i]) << (i % 64);
        }
        bl::direct_view<O, G> direct{ends.data(), n, {bits.data()}, origin};
        bl::delta_view<W, O, G> delta{widths.data(), checkpoints.data(), n, G * 2, total, {bits.data()}};
        auto start_direct = bl::starts(direct);
        auto start_delta = bl::starts(delta);
        std::vector<O> queries{origin, total, O(total + 1)};
        for (auto e : ends) { queries.push_back(e); if (e) queries.push_back(e - 1); }
        for (unsigned i = 0; i < 64; ++i) queries.push_back(origin + rng() % (total - origin + 2));
        for (const auto p : queries) {
            const auto a = oracle<bl::edge::at_or_after>(ends, live, p);
            const auto b = oracle<bl::edge::after>(ends, live, p);
            methods<bl::edge::at_or_after>(direct, p, a); methods<bl::edge::after>(direct, p, b);
            methods<bl::edge::at_or_after>(delta, p, a); methods<bl::edge::after>(delta, p, b);
            methods<bl::edge::at_or_after>(start_direct, p, oracle<bl::edge::at_or_after>(starts, live, p));
            methods<bl::edge::at_or_after>(start_delta, p, oracle<bl::edge::at_or_after>(starts, live, p));
            assert(boundlist_fused::flat<true>(ends.data(), n, p, bits.data()) == a);
            assert(boundlist_fused::flat<false>(ends.data(), n, p, bits.data()) == b);
            const auto count_a = boundlist_count_control::scan<true>(ends.data(), n, p);
            const auto count_b = boundlist_count_control::scan<false>(ends.data(), n, p);
            assert(bl::eligible_from({bits.data()}, count_a, n) == a);
            assert(bl::eligible_from({bits.data()}, count_b, n) == b);
            if constexpr (G == 16) {
                assert(boundlist_fused::width<true>(widths.data(), checkpoints.data(), G * 2, n, total, p, bits.data()) == a);
                assert(boundlist_fused::width<false>(widths.data(), checkpoints.data(), G * 2, n, total, p, bits.data()) == b);
                const auto wanted = oracle<bl::edge::at_or_after>(starts, live, p);
                assert((boundlist_fused::width<true, true>(widths.data(), checkpoints.data(), G * 2, n, total, p, bits.data()) == wanted));
            }
        }
        for (unsigned trial = 0; trial < 128; ++trial) {
            O lo = O(origin + rng() % (total - origin + 2));
            O hi = O(origin + rng() % (total - origin + 2));
            if (hi < lo) std::swap(lo, hi);
            auto geometric_bits = bits;
            for (unsigned r = 0; r < n; ++r) if (starts[r] == ends[r])
                geometric_bits[r / 64] &= ~(std::uint64_t{1} << (r % 64));
            auto gd = direct; gd.eligible.words = geometric_bits.data();
            auto gw = delta; gw.eligible.words = geometric_bits.data();
            unsigned first = n, limit = n;
            if (lo < hi) for (unsigned r = 0; r < n; ++r)
                if (live[r] && starts[r] < ends[r] && lo < ends[r] && starts[r] < hi) {
                    if (first == n) first = r;
                    limit = r + 1;
                }
            const bl::rank_window wanted{first, limit};
            assert((bl::overlap<bl::edge::after, bl::edge::at_or_after, bl::method::binary>(
                gd, bl::starts(gd), {lo, hi}) == wanted));
            assert((bl::overlap<bl::edge::after, bl::edge::at_or_after, bl::method::scan>(
                gw, bl::starts(gw), {lo, hi}) == wanted));
            // Only caller liveness is supplied: no paid/free materialization
            // of a second bitmap to remove zero reservations.
            assert((bl::geometry_overlap<false>(direct, {lo, hi}) == wanted));
            assert((bl::geometry_overlap<true>(direct, {lo, hi}) == wanted));
            assert((bl::geometry_overlap<false>(delta, {lo, hi}) == wanted));
            assert((bl::geometry_overlap<true>(delta, {lo, hi}) == wanted));
            std::array<decltype(gw), 4> views{gw, gw, gw, gw};
            const auto minimum = bl::starts(gw);
            std::array<decltype(bl::starts(gw)), 4> minima{minimum, minimum, minimum, minimum};
            const std::array<bl::query_range<O>, 4> ranges{{{lo, hi}, {origin, total}, {hi, lo}, {total, total}}};
            const auto batch = bl::overlap_shotgun<bl::edge::after, bl::edge::at_or_after, 4>(views, minima, ranges);
            for (unsigned i = 0; i < 4; ++i)
                assert((batch[i] == bl::overlap<bl::edge::after, bl::edge::at_or_after, bl::method::binary>(gw, minimum, ranges[i])));
        }
    }
}

void current_stream() {
    for (auto family : {sp::geometry::direct, sp::geometry::width}) {
        const auto format = sp::layout::prepare({family, sp::placement::grouped, 255, 65280, {}, true, false});
        assert(format);
        std::vector<std::byte> payload(65280), directory(format->directory_bytes());
        alignas(64) std::array<std::byte, 512> lengths{};
        std::array<std::byte, 8> control{};
        auto result = sp::view::bind(*format, {payload, directory, lengths, control});
        assert(result);
        auto& view = *result;
        ikea::no_coverage effects;
        assert(sp::initialize(view, effects));
        std::vector<unsigned> ends;
        std::array<std::byte, 255> value{};
        unsigned total = 0;
        for (unsigned i = 0; i < 256; ++i) {
            const auto w = i % 11 == 0 ? 0 : i % 17;
            assert(sp::append(view, std::span(value).first(w), w, effects));
            total += w;
            ends.push_back(total);
            if (i % 7 == 0) assert(sp::retire(view, i, effects));
        }
        assert(view.validate());
        bl::two_format_dispatch(view, [&](auto a) {
            using A = decltype(a);
            const auto m = bl::metadata<A::family, typename A::width_type, typename A::offset_type>::from(view);
            assert(m.size() == view.size() && m.extent() == total);
            unsigned start = 0;
            for (unsigned r = 0; r < 256; ++r) {
                const auto p = m.locate(r);
                assert(p.offset == start && p.reservation == ends[r] - start && p.length == p.reservation);
                assert(m.live(r) == (r % 7 != 0));
                start = ends[r];
            }
            for (unsigned q = 0; q <= total; ++q) {
                unsigned rank = 0;
                while (rank < ends.size() && ends[rank] <= q) ++rank;
                assert(m.physical(q) == rank);
            }
        });
        for (unsigned p = 0; p <= total; ++p) {
            const auto actual = sp::reverse(view, p);
            unsigned wanted = 0;
            while (wanted < 256 && ends[wanted] <= p) ++wanted;
            assert(bool(actual) == (wanted != 256));
            if (actual) { assert(actual->rank == wanted); assert(actual->tombstone == (wanted % 7 == 0)); }
        }
    }
}

void engine_ranges() {
    auto fixture = [](std::vector<std::vector<unsigned>> records) {
        const unsigned n = records.size();
        std::vector<unsigned> minima, maxima;
        for (const auto& r : records) { minima.push_back(r.front()); maxima.push_back(r.back()); }
        bl::direct_view<unsigned> low{minima.data(), n, {}}, high{maxima.data(), n, {}};
        for (unsigned lo = 0; lo <= 7; ++lo) for (unsigned hi = 0; hi <= 7; ++hi)
            for (bool left_closed : {false, true}) for (bool closed : {false, true}) {
                const bl::query_range<unsigned> query{lo, hi, left_closed, closed};
                const auto window = bl::extrema_overlap<bl::method::binary>(high, low, query);
                bl::rank_window extrema{n, n};
                if (lo < hi || (lo == hi && closed && left_closed)) for (unsigned r = 0; r < n; ++r)
                    if ((left_closed ? maxima[r] >= lo : maxima[r] > lo) && (closed ? minima[r] <= hi : minima[r] < hi)) {
                        if (extrema.empty()) extrema.first = r;
                        extrema.limit = r + 1;
                    }
                assert(window == extrema);
                std::vector<unsigned> actual, expected;
                for (const auto& r : records) for (auto value : r)
                    if ((left_closed ? value >= lo : value > lo) && (closed ? value <= hi : value < hi)) expected.push_back(value);
                if (!window.empty()) for (unsigned rank = window.first; rank < window.limit; ++rank)
                    for (auto value : records[rank])
                        if ((left_closed ? value >= lo : value > lo) && (closed ? value <= hi : value < hi)) actual.push_back(value);
                assert(actual == expected);
            }
    };
    fixture({{1, 2}, {2, 2}, {2, 3}});
    fixture({{1, 2}, {4, 5}});
    fixture({{1, 5}}); // Extrema can overlap [2,3) without containing a matching record.
    fixture({{2}, {2}, {2}});
    fixture({{1}, {1, 1}, {3, 3}, {3, 4}, {6}});

    using T = bl::wide_key;
    const std::vector<T> keys{{1, 5}, {1, 5}, {1, UINT64_MAX}, {2, 0}, {UINT64_MAX, UINT64_MAX}};
    const std::vector<bool> live(keys.size(), true);
    bl::direct_view<T, 2> v{keys.data(), unsigned(keys.size()), {}};
    for (auto p : keys) {
        methods<bl::edge::at_or_after>(v, p, oracle<bl::edge::at_or_after>(keys, live, p));
        methods<bl::edge::after>(v, p, oracle<bl::edge::after>(keys, live, p));
    }
}

template <class T> void guarded_tails() {
    for (unsigned n : {0, 1, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 256, 257, 511}) {
        guarded<T> end(n), checkpoints((n + 15) / 16);
        guarded<std::uint8_t> widths((n + 15) / 16 * 16);
        guarded<std::uint64_t> bits((n + 63) / 64);
        std::vector<T> expected(n);
        std::vector<bool> live(n);
        for (unsigned i = 0; i < (n + 63) / 64; ++i) bits.data[i] = 0;
        for (unsigned i = 0; i < (n + 15) / 16 * 16; ++i) widths.data[i] = 0;
        T position = 0;
        for (unsigned i = 0; i < n; ++i) {
            if (!(i % 16)) checkpoints.data[i / 16] = position;
            widths.data[i] = i % 7;
            position += widths.data[i];
            end.data[i] = expected[i] = position;
            live[i] = i % 5 != 0;
            bits.data[i / 64] |= std::uint64_t(live[i]) << (i % 64);
        }
        bl::direct_view<T> d{end.data, n, {bits.data}};
        bl::delta_view<std::uint8_t, T> w{widths.data, checkpoints.data, n, 16, position, {bits.data}};
        for (unsigned value = 0; value <= unsigned(position) + 1; ++value) {
            const T q = T(value);
            const auto wanted = oracle<bl::edge::at_or_after>(expected, live, q);
            methods<bl::edge::at_or_after>(d, q, wanted);
            methods<bl::edge::at_or_after>(w, q, wanted);
            assert(boundlist_fused::flat<true>(end.data, n, q, bits.data) == wanted);
            assert(bl::eligible_from({bits.data}, boundlist_count_control::scan<true>(end.data, n, q), n) == wanted);
            assert(boundlist_fused::width<true>(widths.data, checkpoints.data, 16, n, position, q, bits.data) == wanted);
        }
    }
}

struct fence {
    std::uint64_t value;
    bool top;
    std::strong_ordering operator<=>(const fence& other) const {
        if (top != other.top) return top <=> other.top;
        return top ? std::strong_ordering::equal : value <=> other.value;
    }
    bool operator==(const fence& other) const { return (*this <=> other) == 0; }
};
void order_only() {
    const std::vector<std::string_view> text{"a", "ab", "b", "b", "z"};
    const std::vector<bool> live(text.size(), true);
    bl::direct_view<std::string_view, 2> v{text.data(), unsigned(text.size()), {}};
    for (std::string_view q : {"", "a", "aa", "ab", "b", "ba", "z", "zz"}) {
        methods<bl::edge::at_or_after>(v, q, oracle<bl::edge::at_or_after>(text, live, q));
        methods<bl::edge::after>(v, q, oracle<bl::edge::after>(text, live, q));
    }
    const std::vector<fence> end{{UINT64_MAX, false}, {0, true}};
    const std::vector<bool> finite_live{true, true};
    bl::direct_view<fence> f{end.data(), 2, {}};
    methods<bl::edge::at_or_after>(f, {UINT64_MAX, false}, 0);
    methods<bl::edge::after>(f, {UINT64_MAX, false}, 1);
    methods<bl::edge::after>(f, {0, true}, 2);
}
void engine_unique_points() {
    auto fixture = [](const std::vector<std::vector<unsigned>>& records, const std::vector<unsigned>& fences) {
        const auto n = unsigned(records.size());
        std::uint64_t bits = 0;
        for (unsigned i = 0; i < n; ++i) bits |= std::uint64_t(!records[i].empty()) << i;
        const bl::direct_view<unsigned> v{fences.data(), n, {&bits}};
        for (unsigned q = 0; q <= 13; ++q) {
            unsigned expected_rank = n;
            bool exists = false;
            for (unsigned i = 0; i < n; ++i) if (!records[i].empty()) {
                if (expected_rank == n && records[i].back() >= q) expected_rank = i;
                exists |= std::find(records[i].begin(), records[i].end(), q) != records[i].end();
            }
            methods<bl::edge::at_or_after>(v, q, expected_rank);
            const auto rank = bl::seek<bl::edge::at_or_after, bl::method::hybrid>(v, q);
            const bool found = rank < n && std::find(records[rank].begin(), records[rank].end(), q) != records[rank].end();
            assert(found == exists);
        }
    };
    fixture({{1, 3}, {6, 8}}, {3, 8});
    fixture({{1, 3}, {}, {6, 8}, {}, {11}}, {3, 3, 8, 8, 11});
    fixture({{1, 4}, {6, 8}}, {4, 8}); // Boundary edit is visible to the new directory only.
    fixture({{3}}, {3});
    const std::array<std::string_view, 2> exact{"aa1", "aa2"}, prefix{"aa", "aa"};
    const bl::direct_view<std::string_view> full{exact.data(), 2, {}}, abbreviated{prefix.data(), 2, {}};
    assert((bl::seek<bl::edge::at_or_after, bl::method::binary>(full, "aa2") == 1));
    assert((bl::seek<bl::edge::at_or_after, bl::method::binary>(abbreviated, "aa") == 0));
    // The latter is a conservative candidate, not an exact separator for aa2.
}
template <class T> void full_words() {
    constexpr unsigned n = 129;
    const T maximum = std::numeric_limits<T>::max(), half = T(maximum / 2 + 1);
    guarded<T> source(n);
    std::vector<T> ends(n);
    std::vector<bool> live(n, true);
    for (unsigned i = 0; i < n; ++i) source.data[i] = ends[i] = T(__uint128_t(maximum) * i / (n - 1));
    const bl::direct_view<T> v{source.data, n, {}};
    for (T q : {T(0), T(1), T(half - 1), half, T(half + 1), T(maximum - 1), maximum}) {
        const auto ge = oracle<bl::edge::at_or_after>(ends, live, q), gt = oracle<bl::edge::after>(ends, live, q);
        methods<bl::edge::at_or_after>(v, q, ge); methods<bl::edge::after>(v, q, gt);
        assert(boundlist_fused::flat<true>(source.data, n, q) == ge);
        assert(boundlist_fused::flat<false>(source.data, n, q) == gt);
        assert(boundlist_count_control::scan<true>(source.data, n, q) == ge);
        assert(boundlist_count_control::scan<false>(source.data, n, q) == gt);
    }
}
template <class T> void full_deltas(T base) {
    constexpr unsigned n = 256;
    std::array<std::uint16_t, n> widths;
    std::array<T, n / 16> checkpoints;
    std::vector<T> ends(n);
    std::vector<bool> live(n, true);
    T total = base;
    for (unsigned i = 0; i < n; ++i) {
        if (!(i % 16)) checkpoints[i / 16] = total;
        widths[i] = 65535 - i % 3;
        ends[i] = total += widths[i];
    }
    const bl::delta_view<std::uint16_t, T> v{widths.data(), checkpoints.data(), n, 16, total, {}};
    for (auto e : ends) for (T q : {T(e - 1), e, T(e + 1)}) {
        const auto ge = oracle<bl::edge::at_or_after>(ends, live, q), gt = oracle<bl::edge::after>(ends, live, q);
        methods<bl::edge::at_or_after>(v, q, ge); methods<bl::edge::after>(v, q, gt);
        assert(boundlist_fused::width<true>(widths.data(), checkpoints.data(), 16, n, total, q) == ge);
        assert(boundlist_fused::width<false>(widths.data(), checkpoints.data(), 16, n, total, q) == gt);
    }
}
template <class T> void shotgun_windows() {
    constexpr unsigned Q = 8;
    std::array<std::vector<T>, Q> data;
    std::array<const T*, Q> pointers;
    const std::array<unsigned, Q> sizes{0, 1, 7, 16, 255, 256, 1024, 16384};
    for (unsigned i = 0; i < Q; ++i) {
        data[i].resize(sizes[i]);
        for (unsigned r = 0; r < sizes[i]; ++r) data[i][r] = T(__uint128_t(std::numeric_limits<T>::max()) * (r / 3) / std::max(1u, sizes[i] / 3));
        pointers[i] = data[i].data();
    }
    std::mt19937_64 rng(713);
    for (unsigned trial = 0; trial < 257; ++trial) {
        std::array<T, Q> queries;
        for (unsigned i = 0; i < Q; ++i) queries[i] = trial == 0 ? T(0) : trial == 1 ? std::numeric_limits<T>::max() : T(rng());
        auto check = [&]<bl::edge E, bool Lerp>() {
            std::array<unsigned, Q> actual;
            bl::shotgun_array<E, Q, Lerp>(pointers, sizes, queries, actual);
            for (unsigned i = 0; i < Q; ++i) {
                unsigned expected = 0;
                while (expected < sizes[i] && (E == bl::edge::at_or_after ? data[i][expected] < queries[i] : !(queries[i] < data[i][expected]))) ++expected;
                assert(actual[i] == expected);
            }
        };
        check.template operator()<bl::edge::at_or_after, false>();
        check.template operator()<bl::edge::at_or_after, true>();
        check.template operator()<bl::edge::after, false>();
        check.template operator()<bl::edge::after, true>();
    }
}
int main() {
    arithmetic<std::uint8_t, std::uint16_t, 16>();
    arithmetic<std::uint8_t, std::uint32_t, 8>();
    arithmetic<std::uint16_t, std::uint32_t, 16>();
    arithmetic<std::uint16_t, std::uint64_t, 32>();
    arithmetic<std::uint16_t, std::uint64_t, 16>();
    current_stream();
    engine_ranges();
    guarded_tails<std::uint16_t>();
    guarded_tails<std::uint32_t>();
    guarded_tails<std::uint64_t>();
    order_only();
    engine_unique_points();
    full_words<std::uint16_t>(); full_words<std::uint32_t>(); full_words<std::uint64_t>();
    full_deltas<std::uint32_t>(0xf0000000u);
    full_deltas<std::uint64_t>(0xf000000000000000ULL);
    shotgun_windows<std::uint16_t>(); shotgun_windows<std::uint32_t>(); shotgun_windows<std::uint64_t>();
    std::puts("Independent endpoint/range/record oracles, exact guard-page tails, order-only/wide/sentinel domains and current StreamPack reverse passed.");
}
