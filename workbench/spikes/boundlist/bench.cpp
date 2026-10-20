#include "boundlist.h"
#include "geometry.h"
#include "fused.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ikea/streampack.h>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace bl = boundlist_spike;
namespace sp = ikea::streampack;
using clock_type = std::chrono::steady_clock;
using word = std::uint64_t;
inline word mix(word x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
struct alignas(64) child { word salt; std::array<word, 7> other{}; };
struct aligned_delete { void operator()(std::byte* p) const { std::free(p); } };
struct fixture {
    unsigned nodes, count, maximum, range_span, stride, lengths_at, control_at;
    bool width;
    sp::layout layout;
    std::unique_ptr<std::byte, aligned_delete> metadata;
    std::vector<child> children;
    std::vector<std::byte> payload;
    fixture(unsigned number, unsigned n, unsigned max, bool widths, unsigned span)
        : nodes(number), count(n), maximum(max), range_span(span), width(widths),
          layout(*sp::layout::prepare({widths ? sp::geometry::width : sp::geometry::direct,
              sp::placement::grouped, std::uint16_t(max), 256 * max, {}, true, false})),
          children(std::size_t(number) * (n + 1)), payload(std::size_t(n) * max) {
        lengths_at = (layout.directory_bytes() + 63) & ~63u;
        control_at = lengths_at + layout.lengths_bytes();
        stride = (control_at + layout.control_bytes() + 63) & ~63u;
        metadata.reset(static_cast<std::byte*>(std::aligned_alloc(64, std::size_t(nodes) * stride)));
        if (!metadata) std::abort();
        std::memset(metadata.get(), 0, std::size_t(nodes) * stride);
        // Generated independently of every search implementation. These first
        // measured geometric cells are all-live, positive widths; other states
        // already belong to correctness checks and get separate later panels.
        for (unsigned node = 0; node < nodes; ++node) {
            auto* p = meta(node);
            std::uint32_t end = 0;
            const auto seed = mix(node + 713);
            for (unsigned rank = 0; rank < n; ++rank) {
                const unsigned value = 1 + mix(seed + rank * 271) % (max <= 255 ? 31 : 257);
                if (widths) {
                    if (!(rank % 16)) store(p + 32 + rank / 16 * layout.offset_bytes(), end, layout.offset_bytes());
                    store(p + lengths_at + rank * layout.width_bytes(), value, layout.width_bytes());
                }
                end += value;
                if (!widths) store(p + 32 + rank * layout.offset_bytes(), end, layout.offset_bytes());
                children[std::size_t(node) * (n + 1) + rank].salt = mix(seed ^ (rank + 1) * 0x9e3779b97f4a7c15ULL);
            }
            children[std::size_t(node) * (n + 1) + n].salt = mix(seed ^ UINT64_MAX);
            store(p + control_at, n, 2);
            if (widths) store(p + control_at + 2, end, 4);
        }
        // The current StreamPack binding has a real shared readable payload
        // extent, but this search+child workload never reads those bytes.
        // Report it separately rather than inventing one full body per node.
    }
    static void store(std::byte* p, unsigned value, unsigned bytes) { std::memcpy(p, &value, bytes); }
    std::byte* meta(unsigned node) const { return metadata.get() + std::size_t(node) * stride; }
    sp::const_view stream(unsigned node) const {
        const auto* p = meta(node);
        return sp::const_view::assume_valid(layout,
            {payload, {p, layout.directory_bytes()}, {p + lengths_at, layout.lengths_bytes()},
             {p + control_at, layout.control_bytes()}});
    }
    template <class W, class O> auto deltas(unsigned node) const {
        const auto* p = meta(node);
        std::uint32_t extent;
        std::memcpy(&extent, p + control_at + 2, 4);
        return bl::delta_view<W, O>{reinterpret_cast<const W*>(p + lengths_at),
            reinterpret_cast<const O*>(p + 32), count, 16, O(extent), {}};
    }
    template <class O> auto direct(unsigned node) const {
        return bl::direct_view<O>{reinterpret_cast<const O*>(meta(node) + 32), count, {}};
    }
    unsigned extent(unsigned node) const {
        const auto* p = meta(node);
        unsigned value = 0;
        if (width) std::memcpy(&value, p + control_at + 2, 4);
        else std::memcpy(&value, p + 32 + (count - 1) * layout.offset_bytes(), layout.offset_bytes());
        return value;
    }
    unsigned query_domain() const { return count * (maximum <= 255 ? 16 : 129) + 17; }
    template <bool Width, class O> [[gnu::always_inline]] unsigned typed_extent(unsigned node) const {
        const auto* p = meta(node);
        if constexpr (Width) {
            std::uint32_t value;
            std::memcpy(&value, p + control_at + 2, sizeof(value));
            return value;
        } else {
            O value;
            std::memcpy(&value, p + 32 + (count - 1) * sizeof(O), sizeof(value));
            return value;
        }
    }
    unsigned oracle_end(unsigned node, unsigned rank) const {
        const auto seed = mix(node + 713);
        unsigned end = 0;
        for (unsigned r = 0; r <= rank; ++r) end += 1 + mix(seed + r * 271) % (maximum <= 255 ? 31 : 257);
        return end;
    }
    bl::rank_window oracle(unsigned node, unsigned lo, unsigned hi) const {
        unsigned first = count, limit = count, start = 0;
        const auto seed = mix(node + 713);
        if (lo < hi) for (unsigned r = 0; r < count; ++r) {
            const auto end = start + 1 + unsigned(mix(seed + r * 271) % (maximum <= 255 ? 31 : 257));
            if (lo < end && start < hi) { if (first == count) first = r; limit = r + 1; }
            start = end;
            if (start >= hi) break;
        }
        return {first, limit};
    }
};

template <bool Width, class W, class O, bl::method Method> struct candidate {
    using offset_type = O;
    static constexpr bool is_width = Width;
    const fixture& f;
    [[gnu::always_inline]] bl::rank_window operator()(unsigned node, unsigned lo, unsigned hi) const {
        if constexpr (Width) {
            const auto v = f.deltas<W, O>(node);
            return bl::overlap<bl::edge::after, bl::edge::at_or_after, Method>(v, bl::starts(v), {O(lo), O(hi)});
        } else {
            const auto v = f.direct<O>(node);
            return bl::overlap<bl::edge::after, bl::edge::at_or_after, Method>(v, bl::starts(v), {O(lo), O(hi)});
        }
    }
};

template <bool Width, class W, class O> struct fused {
    using offset_type = O;
    static constexpr bool is_width = Width;
    const fixture& f;
    [[gnu::always_inline]] bl::rank_window operator()(unsigned node, unsigned lo, unsigned hi) const {
        if (lo >= hi) return {f.count, f.count};
        unsigned first, limit;
        if constexpr (Width) {
            const auto v = f.deltas<W, O>(node);
            first = boundlist_fused::width<false>(v.widths, v.checkpoints, v.stride, v.count, v.extent, O(lo));
            limit = boundlist_fused::width<true, true>(v.widths, v.checkpoints, v.stride, v.count, v.extent, O(hi));
        } else {
            const auto* end = reinterpret_cast<const O*>(f.meta(node) + 32);
            first = boundlist_fused::flat<false>(end, f.count, O(lo));
            // Positive widths and hi>0 in this geometric panel: start>=hi is
            // the following rank after the first end>=hi, or count.
            const auto before = boundlist_fused::flat<true>(end, f.count, O(hi));
            limit = std::min(f.count, before + 1);
        }
        if (first >= limit) return {f.count, f.count};
        return {first, limit};
    }
};

template <bool Width, class W, class O, bool Paired> struct geometric {
    using offset_type = O;
    static constexpr bool is_width = Width;
    const fixture& f;
    [[gnu::always_inline]] bl::rank_window operator()(unsigned node, unsigned lo, unsigned hi) const {
        if constexpr (Width) return bl::geometry_overlap<Paired>(f.deltas<W, O>(node), {O(lo), O(hi)});
        else return bl::geometry_overlap<Paired>(f.direct<O>(node), {O(lo), O(hi)});
    }
};

template <bool Width, class W, class O> struct batched : candidate<Width, W, O, bl::method::binary> {
    template <unsigned Q>
    std::array<bl::rank_window, Q> batch(const std::array<unsigned, Q>& nodes,
                                      const std::array<unsigned, Q>& lo, const std::array<unsigned, Q>& hi) const {
        const auto& f = this->f;
        auto view = [&](unsigned node) {
            if constexpr (Width) return f.template deltas<W, O>(node);
            else return f.template direct<O>(node);
        };
        using V = decltype(view(0));
        using S = decltype(bl::starts(view(0)));
        std::array<V, Q> ends;
        std::array<S, Q> starts;
        std::array<bl::query_range<O>, Q> queries;
        for (unsigned i = 0; i < Q; ++i) {
            ends[i] = view(nodes[i]); starts[i] = bl::starts(ends[i]);
            queries[i] = {O(lo[i]), O(hi[i])};
        }
        return bl::overlap_shotgun<bl::edge::after, bl::edge::at_or_after, Q>(ends, starts, queries);
    }
};

template <bool Width, class W, class O> struct current_stream {
    using offset_type = O;
    static constexpr bool is_width = Width;
    const fixture& f;
    [[gnu::always_inline]] bl::rank_window operator()(unsigned node, unsigned lo, unsigned hi) const {
        if (lo >= hi) return {f.count, f.count};
        const auto view = f.stream(node);
        const sp::author::access<Width ? sp::geometry::width : sp::geometry::direct, W, O, const std::byte> a(view);
        const auto extent = f.typed_extent<Width, O>(node);
        // Real current native reverse, not a rewritten model. At/beyond extent
        // has no owner. hi-1 turns a half-open byte range into its last byte.
        auto owner = [&](unsigned point) __attribute__((always_inline)) {
            unsigned rank;
            [[clang::always_inline]] rank = a.reverse(point).rank;
            return rank;
        };
        const auto first = lo < extent ? owner(lo) : f.count;
        const auto last = hi - 1 < extent ? owner(hi - 1) : f.count - 1;
        if (first == f.count) return {f.count, f.count};
        return {first, last + 1};
    }
};

// Exact inclusive maxima, strictly ordered here: one boundary search and one
// child read. There is no duplicate-run correction or minima access. A lossy
// prefix would not satisfy this admission even when full keys are unique.
template <bool Width, class W, class O, bl::method Method, unsigned Control = 0> struct unique_point {
    static constexpr bl::edge point_edge = bl::edge::at_or_after;
    const fixture& f;
    [[gnu::always_inline]] bl::rank_window operator()(unsigned node, unsigned query, unsigned) const {
        const O p = O(query);
        const auto rank = [&]() __attribute__((always_inline)) {
            if constexpr (Control == 2) {
                if (!query) return 0u;
                if (query > f.typed_extent<Width, O>(node)) return f.count;
                const auto v = f.stream(node);
                const sp::author::access<Width ? sp::geometry::width : sp::geometry::direct, W, O, const std::byte> a(v);
                unsigned rank;
                [[clang::always_inline]] rank = a.reverse(query - 1).rank;
                return rank;
            } else if constexpr (Width) {
                const auto v = f.deltas<W, O>(node);
                if constexpr (Control == 1)
                    return boundlist_fused::width<true>(v.widths, v.checkpoints, v.stride, v.count, v.extent, p);
                else if constexpr (Control == 3) {
                    auto g = unsigned(std::lower_bound(v.checkpoints, v.checkpoints + v.groups(), p) - v.checkpoints);
                    if (g) --g;
                    if (v.extent < p) return v.count;
                    O position = v.checkpoints[g];
                    for (unsigned i = 0; i < 16 && g * 16 + i < v.count; ++i) {
                        position += v.group(g)[i];
                        if (position >= p) return g * 16 + i;
                    }
                    return v.count;
                } else return bl::seek<bl::edge::at_or_after, Method>(v, p);
            } else {
                const auto v = f.direct<O>(node);
                if constexpr (Control == 1) return boundlist_fused::flat<true>(v.ends, v.count, p);
                else if constexpr (Control == 3)
                    return unsigned(std::lower_bound(v.ends, v.ends + v.count, p) - v.ends);
                else return bl::seek<bl::edge::at_or_after, Method>(v, p);
            }
        }();
        return rank == f.count ? bl::rank_window{rank, rank} : bl::rank_window{rank, rank + 1};
    }
};

struct point_oracle {
    static constexpr bl::edge point_edge = bl::edge::at_or_after;
    const fixture& f;
    bl::rank_window operator()(unsigned node, unsigned query, unsigned) const {
        const auto seed = mix(node + 713);
        unsigned end = 0;
        for (unsigned r = 0; r < f.count; ++r) {
            end += 1 + unsigned(mix(seed + r * 271) % (f.maximum <= 255 ? 31 : 257));
            if (end >= query) return {r, r + 1};
        }
        return {f.count, f.count};
    }
};

struct state { word salt; unsigned node; };
template <unsigned Chains, class Find>
[[gnu::noinline]] word consume(const fixture& f, unsigned rounds, Find find) {
    std::array<state, Chains> s;
    for (unsigned c = 0; c < Chains; ++c) s[c] = {mix(713 + c * 0x3917), c % f.nodes};
    constexpr bool Point = requires { Find::point_edge; };
    const auto domain = f.query_domain();
    for (unsigned step = 0; step < rounds; ++step) {
      if constexpr (Chains > 1 && requires(std::array<unsigned, Chains> x) { find.template batch<Chains>(x, x, x); }) {
        std::array<unsigned, Chains> nodes, lo, hi;
        for (unsigned c = 0; c < Chains; ++c) {
            nodes[c] = s[c].node;
            lo[c] = unsigned((__uint128_t(s[c].salt) * domain) >> 64);
            if constexpr (Point) hi[c] = 0;
            else hi[c] = std::min(domain, lo[c] + 1 + unsigned(s[c].salt >> 48) % f.range_span);
        }
        const auto result = find.template batch<Chains>(nodes, lo, hi);
        for (unsigned c = 0; c < Chains; ++c) {
            const auto a = f.children[std::size_t(nodes[c]) * (f.count + 1) + result[c].first].salt;
            const auto last = result[c].empty() ? f.count : result[c].limit - 1;
            const auto b = [&] { if constexpr (Point) return word{};
                else return f.children[std::size_t(nodes[c]) * (f.count + 1) + last].salt; }();
            s[c].salt = mix(s[c].salt ^ a ^ std::rotl(b, 17));
            s[c].node = unsigned(s[c].salt) & (f.nodes - 1);
        }
      } else {
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one = [&](auto index) __attribute__((always_inline)) {
                auto& cursor = s[index];
                const auto lo = unsigned((__uint128_t(cursor.salt) * domain) >> 64);
                const auto hi = [&] { if constexpr (Point) return 0u;
                    else return std::min(domain, lo + 1 + unsigned(cursor.salt >> 48) % f.range_span); }();
                const auto result = find(cursor.node, lo, hi);
                const auto a = f.children[std::size_t(cursor.node) * (f.count + 1) + result.first].salt;
                const auto last = result.empty() ? f.count : result.limit - 1;
                const auto b = [&] { if constexpr (Point) return word{};
                    else return f.children[std::size_t(cursor.node) * (f.count + 1) + last].salt; }();
                cursor.salt = mix(cursor.salt ^ a ^ std::rotl(b, 17));
                cursor.node = unsigned(cursor.salt) & (f.nodes - 1);
            };
            (one(std::integral_constant<unsigned, I>{}), ...);
        }(std::make_index_sequence<Chains>{});
      }
    }
    word sum = 0;
    for (auto x : s) sum ^= x.salt + x.node;
    return sum;
}

inline volatile word observed;
template <unsigned Chains, class Find>
void measure(const fixture& f, const char* name, unsigned operations, unsigned rep, word expected, Find find) {
    const auto rounds = std::max(1u, operations / Chains);
    const auto warm = consume<Chains>(f, 256, find);
    observed = warm;
    const auto begin = clock_type::now();
    const auto result = consume<Chains>(f, rounds, find);
    const auto end = clock_type::now();
    observed = result;
    if (result != expected) { std::fprintf(stderr, "trace checksum differs: %s\n", name); std::abort(); }
    std::printf("%s,%u,%u,%u,%u,%s,%s,%u,%.6f,%llu,%zu,%zu,%zu\n",
        f.width ? "width" : "direct", f.maximum, f.count, f.nodes, f.range_span, name,
        requires { Find::point_edge; } ? (Chains == 1 ? "dependent_unique_point_child" : "independent8_unique_point_child")
                                     : (Chains == 1 ? "dependent_range_child" : "independent8_range_child"), rep,
        double(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / (rounds * Chains),
        (unsigned long long)result, std::size_t(f.nodes) * f.stride,
        f.children.size() * sizeof(child), f.payload.size());
}

template <bool Width, class W, class O> void panel(fixture& f, unsigned operations, unsigned reps) {
    const auto scalar = [&](unsigned node, unsigned lo, unsigned hi) { return f.oracle(node, lo, hi); };
    const auto expected1 = consume<1>(f, std::max(1u, operations), scalar);
    const auto expected8 = consume<8>(f, std::max(1u, operations / 8), scalar);
    auto run = [&](const char* name, auto finder, unsigned rep) {
        for (unsigned node : {0u, f.nodes / 2, f.nodes - 1}) {
            assert(f.stream(node).validate());
            for (unsigned r = 0; r < f.count; ++r) {
                const auto end = f.oracle_end(node, r);
                for (auto lo : {end ? end - 1 : 0, end, end + 1}) {
                    const auto hi = lo + 1 + r % 71;
                    assert(finder(node, lo, hi) == f.oracle(node, lo, hi));
                }
            }
        }
        measure<1>(f, name, operations, rep, expected1, finder);
        measure<8>(f, name, operations, rep, expected8, finder);
    };
    for (unsigned rep = 0; rep < reps; ++rep) for (unsigned i = 0; i < 13; ++i)
        switch (rep % 2 ? 12 - i : i) {
        case 0: run("current_streampack", current_stream<Width, W, O>{f}, rep); break;
        case 1: run("candidate_binary", candidate<Width, W, O, bl::method::binary>{f}, rep); break;
        case 2: run("candidate_select", candidate<Width, W, O, bl::method::select_binary>{f}, rep); break;
        case 3: run("candidate_scan", candidate<Width, W, O, bl::method::scan>{f}, rep); break;
        case 4: run("candidate_lerp", candidate<Width, W, O, bl::method::lerp>{f}, rep); break;
        case 5: run("candidate_gallop", candidate<Width, W, O, bl::method::gallop>{f}, rep); break;
        case 6: run("candidate_quad", candidate<Width, W, O, bl::method::quad>{f}, rep); break;
        case 7: run("geometry_separate", geometric<Width, W, O, false>{f}, rep); break;
        case 8: run("geometry_paired", geometric<Width, W, O, true>{f}, rep); break;
        case 9: run("shotgun8", batched<Width, W, O>{{f}}, rep); break;
        case 10: run("independent_fused", fused<Width, W, O>{f}, rep); break;
        case 11: run("hybrid_convergence", candidate<Width, W, O, bl::method::hybrid>{f}, rep); break;
        case 12: run("select_convergence", candidate<Width, W, O, bl::method::select_hybrid>{f}, rep); break;
        }
}

template <bool Width, class W, class O> void point_panel(fixture& f, unsigned operations, unsigned reps) {
    const point_oracle oracle{f};
    const auto expected1 = consume<1>(f, std::max(1u, operations), oracle);
    const auto expected8 = consume<8>(f, std::max(1u, operations / 8), oracle);
    auto run = [&](const char* name, auto find, unsigned rep) {
        for (unsigned node : {0u, f.nodes / 2, f.nodes - 1}) for (unsigned r = 0; r < f.count; ++r) {
            const auto end = f.oracle_end(node, r);
            for (unsigned q : {0u, end - 1, end, end + 1, f.extent(node) + 1})
                assert(find(node, q, 0) == oracle(node, q, 0));
        }
        measure<1>(f, name, operations, rep, expected1, find);
        measure<8>(f, name, operations, rep, expected8, find);
    };
    for (unsigned rep = 0; rep < reps; ++rep) for (unsigned i = 0; i < 10; ++i)
        switch (rep % 2 ? 9 - i : i) {
        case 0: run("current_streampack_max_adapter", unique_point<Width, W, O, bl::method::binary, 2>{f}, rep); break;
        case 1: run("candidate_binary", unique_point<Width, W, O, bl::method::binary>{f}, rep); break;
        case 2: run("candidate_scan", unique_point<Width, W, O, bl::method::scan>{f}, rep); break;
        case 3: run("candidate_lerp", unique_point<Width, W, O, bl::method::lerp>{f}, rep); break;
        case 4: run("candidate_gallop", unique_point<Width, W, O, bl::method::gallop>{f}, rep); break;
        case 5: run("independent_fused", unique_point<Width, W, O, bl::method::binary, 1>{f}, rep); break;
        case 6: run("std_lower_bound", unique_point<Width, W, O, bl::method::binary, 3>{f}, rep); break;
        case 7: run("hybrid_convergence", unique_point<Width, W, O, bl::method::hybrid>{f}, rep); break;
        case 8: run("candidate_select", unique_point<Width, W, O, bl::method::select_binary>{f}, rep); break;
        case 9: run("select_convergence", unique_point<Width, W, O, bl::method::select_hybrid>{f}, rep); break;
        }
}

int main(int argc, char** argv) {
    const unsigned nodes = argc > 1 ? std::stoul(argv[1]) : 64;
    const unsigned count = argc > 2 ? std::stoul(argv[2]) : 256;
    const unsigned maximum = argc > 3 ? std::stoul(argv[3]) : 255;
    const unsigned operations = argc > 4 ? std::stoul(argv[4]) : 131072;
    const unsigned reps = argc > 5 ? std::stoul(argv[5]) : 5;
    const unsigned range_span = argc > 6 ? std::stoul(argv[6]) : 257;
    const bool point_only = argc > 7 && std::stoul(argv[7]) != 0;
    if (!std::has_single_bit(nodes) || !count || count > 256 || !range_span || range_span > 65536 ||
        (maximum != 255 && maximum != 4095)) return 2;
    std::puts("layout,maximum,count,nodes,range_span,implementation,metric,rep,ns,checksum,metadata_bytes,child_bytes,shared_payload_bytes");
    for (bool width : {false, true}) {
        fixture f(nodes, count, maximum, width, range_span);
        if (maximum == 255) {
            if (width) {
                if (point_only) point_panel<true, std::uint8_t, std::uint16_t>(f, operations, reps);
                else panel<true, std::uint8_t, std::uint16_t>(f, operations, reps);
            } else {
                if (point_only) point_panel<false, std::uint8_t, std::uint16_t>(f, operations, reps);
                else panel<false, std::uint8_t, std::uint16_t>(f, operations, reps);
            }
        } else {
            if (width) {
                if (point_only) point_panel<true, std::uint16_t, std::uint32_t>(f, operations, reps);
                else panel<true, std::uint16_t, std::uint32_t>(f, operations, reps);
            } else {
                if (point_only) point_panel<false, std::uint16_t, std::uint32_t>(f, operations, reps);
                else panel<false, std::uint16_t, std::uint32_t>(f, operations, reps);
            }
        }
    }
    return 0;
}
