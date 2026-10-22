#include <ikea/intervals.h>
#include <ikea/intervals/author/search.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

namespace bl = ikea::intervals;
template <class Source, class Q> void check(const Source &s, const Q &q) {
    unsigned lo = s.size(), hi = s.size();
    for (unsigned r = 0; r < s.size(); ++r) {
        const bool live = [&] {
            if constexpr (requires { s.next_eligible(r, s.size()); })
                return s.next_eligible(r, s.size()) == r;
            else
                return true;
        }();
        if (!live)
            continue;
        const auto comparisons = [&] {
            if constexpr (std::integral<decltype(s.at(r))> && std::integral<Q>) {
                const __int128_t a = s.at(r), b = q;
                return std::pair{a >= b, a > b};
            } else
                return std::pair{s.at(r) >= q, s.at(r) > q};
        }();
        if (lo == s.size() && comparisons.first)
            lo = r;
        if (hi == s.size() && comparisons.second)
            hi = r;
    }
    assert(bl::lower_bound(s, q) == lo && bl::upper_bound(s, q) == hi);
    const auto lower = bl::author::prepare_lower_bound(s, q);
    const auto upper = bl::author::prepare_upper_bound(s, q);
    assert(lower.finish() == lo && upper.finish() == hi);
    if (lower.has_local_read())
        assert(!lower.local_window().empty());
    if (upper.has_local_read())
        assert(!upper.local_window().empty());
}
template <class T> void native() {
    for (unsigned n = 0; n <= 513; ++n) {
        std::vector<T> values(n);
        for (unsigned r = 0; r < n; ++r)
            values[r] = T(r / 3);
        const bl::direct_source<T> source{values.data(), n};
        for (int q = -2; q < 260; ++q)
            check(source, q);
        check(source, std::numeric_limits<std::uint64_t>::max());
        check(source, std::numeric_limits<std::int64_t>::min());
        std::vector<std::byte> bits((n + 7) / 8);
        for (unsigned r = 0; r < n; ++r)
            if (r % 11 == 3)
                bits[r / 8] |= std::byte(1u << (r % 8));
        const bl::selected_source sparse{source, bl::rank_selection{bits}};
        for (int q = -1; q < 260; q += 7)
            check(sparse, q);
        const bl::selected_source inverted{source, bl::rank_selection{bits, true}};
        for (int q = -1; q < 260; q += 17)
            check(inverted, q);
    }
    const T top = std::numeric_limits<T>::max();
    const std::array<T, 4> a{0, T(top - 1), top, top};
    const bl::direct_source<T> s{a.data(), a.size()};
    check(s, top);
    check(s, std::numeric_limits<std::uint64_t>::max());
    check(s, -1);
    if constexpr (sizeof(T) < 8)
        check(s, std::uint64_t(top) + 1);
}
template <class W, class Point> void widths() {
    for (unsigned n = 0; n <= 513; ++n) {
        const unsigned padded = (n + 15) / 16 * 16;
        std::vector<W> w(padded * 2);
        std::vector<Point> checkpoints((n + 15) / 16);
        const Point origin = sizeof(Point) == 8 ? Point(1ull << 62) : Point(17);
        Point position = origin;
        std::vector<Point> ends;
        for (unsigned r = 0; r < n; ++r) {
            if (!(r % 16))
                checkpoints[r / 16] = position;
            const auto width = W(r % 5 ? std::numeric_limits<W>::max() : 0);
            w[(r / 16) * 32 + r % 16] = width;
            position += width;
            ends.push_back(position);
        }
        const bl::delta_source<W, Point> s{w.data(), checkpoints.data(), n, 32, position};
        for (unsigned r = 0; r < n; r += 7) {
            check(s, ends[r]);
            check(s, ends[r] - 1);
            check(s, ends[r] + 1);
        }
        check(s, origin - 1);
        check(s, position + 1);
        check(s, -1);
        check(s, std::numeric_limits<std::uint64_t>::max());
    }
    // Count arithmetic is not restricted by the size of this tiny fixture.
    bl::delta_source<W, Point> high{nullptr, nullptr, bl::maximum_count, 16, 0};
    assert(high.groups() == bl::maximum_count / 16 + 1);
}
void batches() {
    std::array<std::uint16_t, 257> values;
    for (unsigned r = 0; r < values.size(); ++r)
        values[r] = r / 3;
    std::array<bl::direct_source<std::uint16_t>, 8> sources;
    std::array<int, 8> points{-1, 0, 1, 2, 32, 85, 65535, 65536};
    for (unsigned lane = 0; lane < 8; ++lane)
        sources[lane] = {values.data(), unsigned(lane ? lane * 31 : 0)};
    auto lows = bl::author::lower_bounds(sources, points),
         highs = bl::author::upper_bounds(sources, points);
    for (unsigned lane = 0; lane < 8; ++lane) {
        assert(lows[lane] == bl::lower_bound(sources[lane], points[lane]));
        assert(highs[lane] == bl::upper_bound(sources[lane], points[lane]));
    }
    for (unsigned n = 0; n <= 257; ++n) {
        const bl::author::scan_source<std::uint16_t> source(values.data(), n);
        for (int q = -1; q < 90; q += 3)
            check(source, q);
    }
    static_assert(
        sizeof(decltype(bl::delta_source<std::uint8_t, std::uint64_t>{}.prepare<bl::edge::after>(
            0))) == 16);
}
void ordered_domains() {
    const std::array<std::string_view, 4> strings{"a", "aa", "aa", "z"};
    const bl::direct_source s{strings.data(), 4u};
    for (auto q : {"", "a", "aa", "b", "zz"})
        check(s, std::string_view(q));
    struct wide {
        std::uint64_t high, low;
        auto operator<=>(const wide &) const = default;
    };
    const std::array<wide, 4> keys{{{0, UINT64_MAX}, {1, 0}, {1, 1}, {UINT64_MAX, UINT64_MAX}}};
    const bl::direct_source w{keys.data(), 4u};
    for (auto q : keys)
        check(w, q);
    // A projected domain with a virtual top beyond all ordinary u64 points.
    struct projected {
        std::uint64_t value;
        bool top;
        auto operator<=>(const projected &b) const {
            if (top != b.top)
                return top <=> b.top;
            return value <=> b.value;
        }
    };
    struct source {
        unsigned size() const { return 2; }
        projected at(unsigned r) const { return {r ? 0u : UINT64_MAX, bool(r)}; }
    };
    assert(bl::lower_bound(source{}, projected{UINT64_MAX, false}) == 0);
    assert(bl::lower_bound(source{}, projected{0, true}) == 1);
}
template <class T> void tails() {
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto *raw = static_cast<std::byte *>(
        mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(raw != MAP_FAILED && !mprotect(raw + page, page, PROT_NONE));
    for (unsigned n = 0; n < 100; ++n) {
        auto *p = reinterpret_cast<T *>(raw + page - n * sizeof(T));
        for (unsigned r = 0; r < n; ++r) {
            const T v = T(r / 2);
            std::memcpy(p + r, &v, sizeof(v));
        }
        const bl::direct_source s{p, n};
        const bl::author::scan_source<T> scan{p, n};
        for (unsigned q = 0; q < 55; ++q) {
            check(s, q);
            check(scan, q);
        }
        // The unsigned adapter also admits unaligned native payloads.
        if (n) {
            std::memmove(raw + page - n * sizeof(T) - 1, p, n * sizeof(T));
            const bl::direct_source u{reinterpret_cast<const T *>(raw + page - n * sizeof(T) - 1),
                                      n};
            const bl::author::scan_source<T> unaligned{u.data, n};
            for (unsigned q = 0; q < 55; ++q) {
                check(u, q);
                check(unaligned, q);
            }
        }
    }
    for (unsigned n = 1; n < 90; ++n) {
        const auto bytes = (n + 7) / 8;
        auto *p = raw + page - bytes;
        std::memset(p, 0, bytes);
        p[(n - 1) / 8] |= std::byte(1u << ((n - 1) % 8));
        bl::rank_selection pop{{p, bytes}};
        assert(pop.next(0, n) == n - 1 && pop.previous(n, n) == n - 1);
    }
    assert(!munmap(raw, page * 2));
}

template <class T> void prepared_sources() {
    for (unsigned n : {4096u, 4103u, 16384u})
        for (unsigned shape = 0; shape < 6; ++shape) {
            std::vector<T> values(n);
            T value = sizeof(T) == 8 ? T(1ull << 62) : T(17);
            for (unsigned r = 0; r < n; ++r) {
                value += T(shape == 0   ? 1 + (r * 971 % 3)
                           : shape == 1 ? unsigned(r % 17 == 0)
                           : shape == 2 ? unsigned(r >= n * 7 / 8)
                           : shape == 3 ? 0
                           : shape == 4 ? unsigned(r == n * 5 / 16) * 1000
                                        : 2);
                values[r] = value;
            }
            const auto source = bl::author::bind_search(bl::direct_source{values.data(), n});
            for (unsigned i = 0; i < 257; ++i) {
                const auto q = values[std::uint64_t(n - 1) * i / 256];
                check(source, q);
                if (q)
                    check(source, q - 1);
                check(source, std::uint64_t(q) + 1);
            }
            check(source, -1);
            check(source, UINT64_MAX);
            const std::array<std::uint64_t, 4> queries{0, std::uint64_t(values[0]),
                                                       std::uint64_t(values[n / 2]), UINT64_MAX};
            const std::array sources{source, source, source, source};
            const auto lower = bl::author::lower_bounds(sources, queries),
                       upper = bl::author::upper_bounds(sources, queries);
            for (unsigned i = 0; i < queries.size(); ++i) {
                assert(lower[i] ==
                       unsigned(std::lower_bound(values.begin(), values.end(), queries[i]) -
                                values.begin()));
                assert(upper[i] ==
                       unsigned(std::upper_bound(values.begin(), values.end(), queries[i]) -
                                values.begin()));
            }
            // Geometric preparation owns its routing/query state. Only backing
            // must survive; population composition separately borrows a source.
            const auto pending = bl::author::prepare_lower_bound(source, values[n / 2]);
            static_assert(sizeof(pending) == 32);
            static_assert(sizeof(source) == (sizeof(T) == 8 ? 48 : 40));
            assert(pending.finish() == lower[2]);
            const auto detached = [&] {
                const auto temporary_binding =
                    bl::author::bind_search(bl::direct_source{values.data(), n});
                return bl::author::prepare_lower_bound(temporary_binding, values[n / 2]);
            }();
            assert(detached.finish() == lower[2]);
            if (pending.has_local_read()) {
                const auto window = pending.local_window();
                assert(window.data() >= reinterpret_cast<const std::byte *>(values.data()));
                assert(window.data() + window.size() <=
                       reinterpret_cast<const std::byte *>(values.data() + n));
            }
        }
    constexpr unsigned n = 4103;
    std::array<std::vector<T>, 8> values;
    for (unsigned i = 0; i < 8; ++i) {
        values[i].resize(n);
        for (unsigned r = 0; r < n; ++r)
            values[i][r] = T(13 + i + r * (i + 1));
    }
    const auto sources = [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::array{bl::author::bind_search(bl::direct_source{values[I].data(), n})...};
    }(std::make_index_sequence<8>{});
    const std::array<std::int64_t, 8> queries{-1, 0, 300, 4000, 13000, 16000, 30000, INT64_MAX};
    const auto lower = bl::author::lower_bounds(sources, queries),
               upper = bl::author::upper_bounds(sources, queries);
    for (unsigned i = 0; i < 8; ++i) {
        unsigned l = n, u = n;
        for (unsigned r = 0; r < n; ++r) {
            if (l == n && __int128_t(values[i][r]) >= queries[i])
                l = r;
            if (u == n && __int128_t(values[i][r]) > queries[i])
                u = r;
        }
        assert(lower[i] == l && upper[i] == u);
    }
    // Exact tails and unaligned native values use the same prepared binding.
    const auto page = unsigned(sysconf(_SC_PAGESIZE));
    const auto bytes = ((n * sizeof(T) + page - 1) / page + 1) * page;
    auto *area = static_cast<std::byte *>(
        mmap(nullptr, bytes + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(area != MAP_FAILED && !mprotect(area + bytes, page, PROT_NONE));
    for (unsigned shift : {0u, 1u}) {
        auto *data = area + bytes - n * sizeof(T) - shift;
        for (unsigned r = 0; r < n; ++r) {
            const T v = T(r * 3);
            std::memcpy(data + r * sizeof(T), &v, sizeof(v));
        }
        const auto source =
            bl::author::bind_search(bl::direct_source{reinterpret_cast<const T *>(data), n});
        for (unsigned r : {0u, 1u, 2047u, 4095u, 4102u})
            assert(bl::lower_bound(source, T(r * 3)) == r);
        assert(bl::lower_bound(source, std::uint64_t(n) * 3) == n);
    }
    assert(!munmap(area, bytes + page));
}
void packed_geometry_checks();
void cuts_checks();
void byte_scans() {
    for (unsigned n :
         {0u, 1u, 7u, 8u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u, 128u, 256u, 4096u, 65535u}) {
        std::vector<std::uint8_t> values(n);
        for (unsigned i = 0; i < n; ++i)
            values[i] = std::uint64_t(i) * 255 / std::max(1u, n - 1);
        const bl::author::scan_source<std::uint8_t> source(values.data(), n);
        for (int q : {-1, 0, 1, 127, 128, 254, 255, 256})
            check(source, q);
    }
}
int main() {
    native<std::uint8_t>();
    byte_scans();
    native<std::uint16_t>();
    native<std::uint32_t>();
    native<std::uint64_t>();
    widths<std::uint8_t, std::uint32_t>();
    widths<std::uint16_t, std::uint32_t>();
    widths<std::uint8_t, std::uint64_t>();
    widths<std::uint16_t, std::uint64_t>();
    prepared_sources<std::uint16_t>();
    prepared_sources<std::uint32_t>();
    prepared_sources<std::uint64_t>();
    batches();
    ordered_domains();
    tails<std::uint8_t>();
    tails<std::uint16_t>();
    tails<std::uint32_t>();
    tails<std::uint64_t>();
}
