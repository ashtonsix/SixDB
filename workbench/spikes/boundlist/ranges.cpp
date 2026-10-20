#include "boundlist.h"
#include "fused.h"
#include "wide-control.h"
#include <ikea/boundlist.h>
#include <ikea/boundlist/author/search.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace bl = boundlist_spike;
using word = std::uint64_t;
inline word mix(word x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
struct alignas(64) child { word salt; std::array<word, 7> other{}; };
template <class T> T point(unsigned node, unsigned ordinal) {
    if constexpr (std::same_as<T, bl::wide_key>)
        return {0x8000000000000000ULL + (word(ordinal) >> 8),
            (word(ordinal & 255) << 56) | (mix((word(node) << 32) ^ ordinal) & 0x00ffffffffffffffULL)};
    else if constexpr (sizeof(T) == 8) return T(0xf000000000000000ULL + (word(node) << 24) + ordinal);
    else if constexpr (sizeof(T) == 4) return T(0x80000000u + (node % 1024) * 65536 + ordinal);
    else return T(16384 + node % 1024 + ordinal);
}
template <class T> struct fact {
    std::array<T, 4> records;
    bool live;
};
template <class T, bool Sparse> struct fixture {
    using binding = decltype(ikea::boundlist::author::bind_search(ikea::boundlist::direct_source<T>{}));
    using prepared_source = std::conditional_t<Sparse, ikea::boundlist::populated_source<binding>, binding>;
    unsigned nodes, count, repeat, gap, span, domain, words;
    std::vector<T> minima, maxima;
    std::vector<word> live;
    std::vector<child> children;
    std::vector<fact<T>> facts; // Released before measurements, not a free cache.
    std::vector<prepared_source> prepared_low, prepared_high;
    fixture(unsigned number, unsigned n, unsigned duplication, unsigned spacing, unsigned width)
        : nodes(number), count(n), repeat(duplication), gap(spacing), span(width),
          domain(((4 * n + duplication - 1) / duplication + 2) * 2 * spacing), words((n + 63) / 64),
          minima(std::size_t(nodes) * n), maxima(minima.size()), live(Sparse ? std::size_t(nodes) * words : 0),
          children(std::size_t(nodes) * (n + 1)), facts(minima.size()) {
        if constexpr (sizeof(T) == 2) assert(domain + 17407 < 65536);
        for (unsigned node = 0; node < nodes; ++node) for (unsigned r = 0; r < count; ++r) {
            auto& row = facts[std::size_t(node) * count + r];
            row.live = !Sparse || r % 8 != 7;
            for (unsigned j = 0; j < 4; ++j) row.records[j] = point<T>(node, ((r * 4 + j) / repeat) * 2 * gap);
            minima[std::size_t(node) * count + r] = row.records.front();
            maxima[std::size_t(node) * count + r] = row.records.back();
            if constexpr (Sparse) live[std::size_t(node) * words + r / 64] |= word(row.live) << (r % 64);
            children[std::size_t(node) * (count + 1) + r].salt = child_salt(node, r);
            if (r) assert(!(row.records.front() < facts[std::size_t(node) * count + r - 1].records.back()));
        }
        for (unsigned node = 0; node < nodes; ++node) children[std::size_t(node) * (count + 1) + count].salt = child_salt(node, count);
        prepared_low.reserve(nodes); prepared_high.reserve(nodes);
        for (unsigned node=0;node<nodes;++node) {
            auto add=[&](auto& target,const T* data) {
                auto source=ikea::boundlist::author::bind_search(ikea::boundlist::direct_source<T>{data,count});
                if constexpr(Sparse) target.emplace_back(source,ikea::boundlist::population{
                    std::span(reinterpret_cast<const std::byte*>(activity(node)),(count+7)/8)});
                else target.push_back(source);
            };
            add(prepared_low,low(node)); add(prepared_high,high(node));
        }
    }
    word child_salt(unsigned node, unsigned rank) const { return mix((word(node) << 32) ^ (rank + 713)); }
    const word* activity(unsigned node) const {
        if constexpr (Sparse) return live.data() + std::size_t(node) * words;
        else return nullptr;
    }
    const T* low(unsigned node) const { return minima.data() + std::size_t(node) * count; }
    const T* high(unsigned node) const { return maxima.data() + std::size_t(node) * count; }
    bool active(unsigned node, unsigned r) const {
        if constexpr (Sparse) return live[std::size_t(node) * words + r / 64] >> (r % 64) & 1;
        else return true;
    }
    template <bool Point> bl::rank_window linear(unsigned node, const T& lo, const T& hi) const {
        unsigned first = count, limit = count;
        if constexpr (!Point) if (!(lo < hi)) return {count, count};
        for (unsigned r = 0; r < count; ++r) if (active(node, r)) {
            const auto a = point<T>(node, (r * 4 / repeat) * 2 * gap);
            const auto b = point<T>(node, ((r * 4 + 3) / repeat) * 2 * gap);
            if (!(b < lo) && (Point || a < hi)) {
                if (first == count) first = r;
                limit = r + 1;
                if constexpr (Point) break;
            }
        }
        return {first, limit};
    }
    template <bool Point> bl::rank_window oracle(unsigned node, const T& lo, const T& hi) const {
        const auto first = facts.begin() + std::size_t(node) * count, end = first + count;
        auto a = std::lower_bound(first, end, lo, [](const fact<T>& row, const T& q) { return row.records.back() < q; });
        while (a != end && !a->live) ++a;
        if constexpr (Point) return a == end ? bl::rank_window{count, count} : bl::rank_window{unsigned(a - first), unsigned(a - first + 1)};
        else {
            if (!(lo < hi)) return {count, count};
            auto b = std::lower_bound(first, end, hi, [](const fact<T>& row, const T& q) { return row.records.front() < q; });
            if (a >= b) return {count, count};
            do { --b; } while (b > a && !b->live);
            return {unsigned(a - first), unsigned(b - first + 1)};
        }
    }
};

template <class T> [[gnu::always_inline]] unsigned native_flat(const T* data, unsigned n, const T& query, const word* live = nullptr) {
    if constexpr (std::same_as<T, bl::wide_key>) return boundlist_wide_control::flat<true>(data, n, query, live);
    else return boundlist_fused::flat<true>(data, n, query, live);
}
template <class T, bool Sparse, bool Point, unsigned Method>
[[gnu::always_inline]] bl::rank_window lookup(const fixture<T, Sparse>& f, unsigned node, const T& low, const T& high) {
    if constexpr (Method == 99) return f.template oracle<Point>(node, low, high);
    else if constexpr (Method == 8 || Method == 9) {
        namespace prod=ikea::boundlist;
        const prod::direct_source<T> high_source{f.high(node),f.count}, low_source{f.low(node),f.count};
        auto find=[&](const auto& maxima,const auto& minima) __attribute__((always_inline)) {
            if constexpr(Point) {
                const auto rank=prod::lower_bound(maxima,low);
                return bl::rank_window{rank,rank==f.count?rank:rank+1};
            } else {
                const auto ranks=prod::overlap(maxima,minima,prod::query_range{low,high});
                return bl::rank_window{ranks.first,ranks.limit};
            }
        };
        if constexpr(Method==9) return find(f.prepared_high[node],f.prepared_low[node]);
        else if constexpr(Sparse) {
            const auto bytes=std::span(reinterpret_cast<const std::byte*>(f.activity(node)),(f.count+7)/8);
            const prod::populated_source maxima{high_source,prod::population{bytes}}, minima{low_source,prod::population{bytes}};
            return find(maxima,minima);
        } else return find(high_source,low_source);
    }
    else if constexpr (Method < 5) {
        constexpr auto M = Method == 0 ? bl::method::binary : Method == 1 ? bl::method::select_binary
            : Method == 2 ? bl::method::scan : Method == 3 ? bl::method::hybrid : bl::method::select_hybrid;
        const bl::direct_view<T> maxima{f.high(node), f.count, {f.activity(node)}};
        if constexpr (Point) {
            const auto r = bl::seek<bl::edge::at_or_after, M>(maxima, low);
            return r == f.count ? bl::rank_window{r, r} : bl::rank_window{r, r + 1};
        } else {
            const bl::direct_view<T> minima{f.low(node), f.count, {f.activity(node)}};
            return bl::extrema_overlap<M>(maxima, minima, {low, high});
        }
    } else {
        if constexpr (!Point) if (!(low < high)) return {f.count, f.count};
        auto locate = [&](const T* data, const T& q, bool activity) __attribute__((always_inline)) {
            if constexpr (Method == 6) return native_flat(data, f.count, q, activity ? f.activity(node) : nullptr);
            else if constexpr (Method == 7) {
                auto r = unsigned(std::lower_bound(data, data + f.count, q) - data);
                if (activity) while (r < f.count && !f.active(node, r)) ++r;
                return r;
            } else {
                // Fully fused independent checkpoint choice + local ISA mask.
                unsigned lo = 0, hi = (f.count + 15) / 16;
                while (lo < hi) {
                    const auto mid = lo + (hi - lo) / 2;
                    if (!(data[std::min(f.count, (mid + 1) * 16) - 1] < q)) hi = mid;
                    else lo = mid + 1;
                }
                for (unsigned g = lo; g < (f.count + 15) / 16; ++g) {
                    const auto first = g * 16, take = std::min(16u, f.count - first);
                    const auto r = native_flat(data + first, take, q);
                    for (unsigned i = first + r; r != take && i < first + take; ++i)
                        if (!activity || f.active(node, i)) return i;
                }
                return f.count;
            }
        };
        const auto first = locate(f.high(node), low, true);
        if constexpr (Point) return first == f.count ? bl::rank_window{first, first} : bl::rank_window{first, first + 1};
        else {
            const auto stop = locate(f.low(node), high, false);
            if (first >= stop) return {f.count, f.count};
            auto last = stop - 1;
            while (last > first && !f.active(node, last)) --last;
            return {first, last + 1};
        }
    }
}

template <unsigned Chains, class T, bool Sparse, bool Point, unsigned Method>
[[gnu::noinline]] word consume(const fixture<T, Sparse>& f, unsigned rounds) {
    std::array<word, Chains> state;
    for (unsigned c = 0; c < Chains; ++c) state[c] = mix(713 + c * 0x3917);
    for (unsigned step = 0; step < rounds; ++step) {
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one = [&](auto index) __attribute__((always_inline)) {
                auto& salt = state[index];
                const auto node = unsigned(salt) & (f.nodes - 1);
                const auto q = unsigned((__uint128_t(salt) * f.domain) >> 64);
                const auto low = point<T>(node, q);
                const auto high = [&]() __attribute__((always_inline)) { if constexpr (Point) return low;
                    else return point<T>(node, std::min(f.domain, q + 1 + unsigned(salt >> 48) % f.span)); }();
                const auto result = lookup<T, Sparse, Point, Method>(f, node, low, high);
                const auto first = result.empty() ? f.count : result.first;
                const auto last = result.empty() ? f.count : result.limit - 1;
                const auto a = [&]() __attribute__((always_inline)) { if constexpr (Method == 99) return f.child_salt(node, first);
                    else return f.children[std::size_t(node) * (f.count + 1) + first].salt; }();
                const auto b = [&]() __attribute__((always_inline)) { if constexpr (Point) return word{};
                    else if constexpr (Method == 99) return f.child_salt(node, last);
                    else return f.children[std::size_t(node) * (f.count + 1) + last].salt; }();
                salt = mix(salt ^ a ^ std::rotl(b, 17));
            };
            (one(std::integral_constant<unsigned, I>{}), ...);
        }(std::make_index_sequence<Chains>{});
    }
    word sum = 0;
    for (auto s : state) sum ^= s;
    return sum;
}
inline volatile word observed;
template <unsigned Chains, class T, bool Sparse, bool Point, unsigned Method>
void trial(const fixture<T, Sparse>& f, const char* name, unsigned operations, unsigned rep, word expected) {
    observed = consume<Chains, T, Sparse, Point, Method>(f, 256);
    const auto rounds = std::max(1u, operations / Chains);
    const auto begin = std::chrono::steady_clock::now();
    const auto result = consume<Chains, T, Sparse, Point, Method>(f, rounds);
    const auto end = std::chrono::steady_clock::now();
    observed = result;
    assert(result == expected);
    std::printf("%zu,%u,%u,%u,%u,%u,%u,%s,%s,%u,%.6f,%zu,%zu,%zu,%llu\n", sizeof(T) * 8, f.count,
        f.nodes, f.repeat, f.gap, f.span, unsigned(Sparse), name,
        Point ? (Chains == 1 ? "dependent_point_child" : "independent8_point_child")
              : (Chains == 1 ? "dependent_extrema_children" : "independent8_extrema_children"), rep,
        double(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / (rounds * Chains),
        f.minima.size() * sizeof(T) * 2 + f.live.size() * sizeof(word), f.children.size() * sizeof(child),
        Method==9 ? 2*f.nodes*sizeof(typename fixture<T,Sparse>::prepared_source) : 0,
        (unsigned long long)result);
}
template <class T, bool Sparse, bool Point>
void panel(unsigned nodes, unsigned count, unsigned repeat, unsigned gap, unsigned span, unsigned operations, unsigned reps) {
    fixture<T, Sparse> f(nodes, count, repeat, gap, span);
    for (unsigned node : {0u, nodes / 2, nodes - 1}) for (unsigned q = 0; q < f.domain; q += std::max(1u, f.domain / 257)) {
        const auto lo = point<T>(node, q), hi = point<T>(node, std::min(f.domain, q + 1 + q % span));
        const auto expected = f.template linear<Point>(node, lo, hi);
        assert(f.template oracle<Point>(node, lo, hi) == expected);
        assert((lookup<T, Sparse, Point, 0>(f, node, lo, hi) == expected));
        assert((lookup<T, Sparse, Point, 1>(f, node, lo, hi) == expected));
        assert((lookup<T, Sparse, Point, 2>(f, node, lo, hi) == expected));
        assert((lookup<T, Sparse, Point, 3>(f, node, lo, hi) == expected));
        assert((lookup<T, Sparse, Point, 4>(f, node, lo, hi) == expected));
        assert((lookup<T, Sparse, Point, 5>(f, node, lo, hi) == expected));
        assert((lookup<T, Sparse, Point, 6>(f, node, lo, hi) == expected));
        assert((lookup<T, Sparse, Point, 7>(f, node, lo, hi) == expected));
        assert((lookup<T, Sparse, Point, 8>(f, node, lo, hi) == expected));
        assert((lookup<T, Sparse, Point, 9>(f, node, lo, hi) == expected));
    }
    const auto expected1 = consume<1, T, Sparse, Point, 99>(f, operations);
    const auto expected8 = consume<8, T, Sparse, Point, 99>(f, std::max(1u, operations / 8));
    decltype(f.facts){}.swap(f.facts);
    auto one = [&]<unsigned Method>(const char* name, unsigned rep) {
        trial<1, T, Sparse, Point, Method>(f, name, operations, rep, expected1);
        trial<8, T, Sparse, Point, Method>(f, name, operations, rep, expected8);
    };
    for (unsigned rep = 0; rep < reps; ++rep) for (unsigned i = 0; i < 10; ++i)
        switch (rep % 2 ? 9 - i : i) {
        case 0: one.template operator()<0>("binary", rep); break;
        case 1: one.template operator()<1>("select", rep); break;
        case 2: one.template operator()<2>("scan", rep); break;
        case 3: one.template operator()<3>("hybrid", rep); break;
        case 4: one.template operator()<4>("select_convergence", rep); break;
        case 5: one.template operator()<5>("independent_fused_binary", rep); break;
        case 6: one.template operator()<6>("independent_fused_scan", rep); break;
        case 7: one.template operator()<7>("std_lower_bound", rep); break;
        case 8: one.template operator()<8>("production_boundlist", rep); break;
        case 9: one.template operator()<9>("production_bound_search", rep); break;
        }
}
template <class T> void run(unsigned nodes, unsigned count, unsigned repeat, unsigned gap, unsigned span, bool sparse, bool point,
                           unsigned operations, unsigned reps) {
#define BL_RANGE(S, P) panel<T, S, P>(nodes, count, repeat, gap, span, operations, reps)
    if (sparse) { if (point) BL_RANGE(true, true); else BL_RANGE(true, false); }
    else { if (point) BL_RANGE(false, true); else BL_RANGE(false, false); }
#undef BL_RANGE
}
int main(int argc, char** argv) {
    if (argc != 11) return 2;
    const unsigned bits = std::stoul(argv[1]), nodes = std::stoul(argv[2]), count = std::stoul(argv[3]);
    const unsigned repeat = std::stoul(argv[4]), gap = std::stoul(argv[5]), span = std::stoul(argv[6]);
    const bool sparse = std::stoul(argv[7]), point = std::stoul(argv[8]);
    const unsigned operations = std::stoul(argv[9]), reps = std::stoul(argv[10]);
    if (!std::has_single_bit(nodes) || !count || count > 16384 || !repeat || repeat > 256 || !gap || gap > 16 || !span ||
        span > 16384 || !operations || !reps) return 2;
    if (bits == 16 && ((4 * count + repeat - 1) / repeat + 2) * 2 * gap + 17407 >= 65536) return 2;
    std::puts("bits,count,nodes,repeat,gap,span,sparse,implementation,metric,rep,ns,metadata_bytes,child_bytes,binding_bytes,checksum");
#define BL_TYPE(T) run<T>(nodes, count, repeat, gap, span, sparse, point, operations, reps)
    switch (bits) {
    case 16: BL_TYPE(std::uint16_t); break;
    case 32: BL_TYPE(std::uint32_t); break;
    case 64: BL_TYPE(std::uint64_t); break;
    case 128: BL_TYPE(bl::wide_key); break;
    default: return 2;
    }
#undef BL_TYPE
}
