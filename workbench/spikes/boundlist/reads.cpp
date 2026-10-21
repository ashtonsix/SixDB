#include "metadata.h"
#include "../streampack/prefix.h"
#include "legacy-read.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <vector>
namespace sp = ikea::streampack;
namespace bl = boundlist_spike;
using word = std::uint64_t;
struct descriptor { unsigned offset, length; };
struct release { void operator()(std::byte* p) const { std::free(p); } };
struct fixture {
    unsigned rows;
    sp::layout layout;
    std::size_t metadata_bytes;
    std::unique_ptr<std::byte, release> metadata;
    std::vector<std::byte> payload;
    std::vector<descriptor> facts;
    std::vector<sp::view> views;
    fixture(unsigned nodes, sp::description schema)
        : rows(nodes * 256), layout(*sp::layout::prepare(schema)), payload(std::size_t(rows) * 48), facts(rows) {
        const auto lengths = (layout.directory_bytes() + 63) & ~63u;
        const auto control = lengths + layout.lengths_bytes();
        const auto stride = (control + layout.control_bytes() + 63) & ~63u;
        metadata_bytes = std::size_t(nodes) * stride;
        metadata.reset(static_cast<std::byte*>(std::aligned_alloc(64, metadata_bytes))); assert(metadata);
        views.reserve(nodes);
        unsigned base = 0; ikea::no_coverage effects;
        for (unsigned block = 0; block < nodes; ++block) {
            auto* p = metadata.get() + block * stride;
            views.push_back(*sp::view::bind(layout, {std::span(payload).subspan(base, 256 * 48),
                {p, layout.directory_bytes()}, {p + lengths, layout.lengths_bytes()}, {p + control, layout.control_bytes()}}));
            auto& v = views.back(); assert(sp::initialize(v, effects));
            for (unsigned r = 0; r < 256; ++r) {
                const auto global = block * 256 + r;
                const auto length = layout.fixed() ? 8u : 8 + global * 13 % 24;
                const auto reservation = schema.full || layout.fixed() ? length : length + global % 11;
                std::array<std::byte, 48> value; value.fill(std::byte(global & 255));
                const auto next = (global * 1664525u + 1013904223u) & (rows - 1);
                std::memcpy(value.data() + length - 4, &next, 4);
                facts[global] = {base + v.extent(), length};
                assert(sp::append(v, std::span(value).first(length), reservation, effects));
            }
            assert(v.validate()); base += v.extent();
        }
        for (auto& v : views) {
            const auto p = v.windows();
            v = sp::view::assume_valid(layout, {p.payload.first(v.extent()), p.directory, p.lengths, p.control});
        }
        // Extent is fully materialized. Unused vector capacity is reported
        // separately from the logical payload/read set, never called RSS.
        payload.resize(base);
    }
    sp::bytes oracle(unsigned r) const { return std::span(payload).subspan(facts[r].offset, facts[r].length); }
};
inline unsigned tail(sp::bytes value) { unsigned next; std::memcpy(&next, value.data() + value.size() - 4, 4); return next; }
template <class T> [[gnu::always_inline]] unsigned independent_load(const std::byte* p) { T x; std::memcpy(&x, p, sizeof(x)); return x; }
template <unsigned Method, class A> struct reader {
    const fixture& f;
    using access_type=std::conditional_t<Method==4,boundlist_before::author::access<A::family,
        typename A::width_type,typename A::offset_type,std::byte>,A>;
    access_type prototype;
    bl::metadata<A::family, typename A::width_type, typename A::offset_type> metadata;
    [[gnu::always_inline]] sp::bytes operator()(unsigned row) const {
        const auto& v = f.views[row / 256]; const auto rank = row % 256;
        if constexpr (Method == 0 || Method==4) return prototype.rebind(v).value(rank);
        else if constexpr (Method == 1) {
            const auto p = metadata.rebind(v).locate(rank);
            return sp::bytes(v.windows().payload).subspan(p.offset, p.length);
        } else if constexpr (Method == 2) return metadata.rebind(v).value(v.windows().payload, rank);
        else {
            // Independent raw field routing + explicit native prefix, with no
            // Ikea decoder/location/geometry helper in the complete consumer.
            using W = typename A::width_type; using O = typename A::offset_type;
            const auto& l = f.layout; const auto& p = v.windows();
            const auto slot = rank / 16 * l.group_stride() + rank % 16 * sizeof(W);
            unsigned begin;
            if (l.fixed()) begin = rank * l.fixed_width();
            else if constexpr (A::family == sp::geometry::direct)
                begin = rank ? independent_load<O>(p.directory.data() + l.geometry_offset() + (rank - 1) * sizeof(O)) : 0;
            else {
                unsigned prefix;
                [[clang::always_inline]] prefix = streampack_next::prefix16_native(reinterpret_cast<const W*>(p.lengths.data() + rank / 16 * l.group_stride()), rank % 16);
                begin = independent_load<O>(p.directory.data() + l.geometry_offset() + rank / 16 * sizeof(O)) + prefix;
            }
            unsigned length;
            if (l.has_used()) length = independent_load<W>(p.lengths.data() + l.used_offset() + slot);
            else if (l.fixed()) length = l.fixed_width();
            else if constexpr (A::family == sp::geometry::direct)
                length = independent_load<O>(p.directory.data() + l.geometry_offset() + rank * sizeof(O)) - begin;
            else length = independent_load<W>(p.lengths.data() + slot);
            return sp::bytes(p.payload).subspan(begin, length);
        }
    }
};
template <unsigned Chains, class Read> [[gnu::noinline]] word chase(unsigned rounds, Read read) {
    std::array<unsigned, Chains> ranks;
    for (unsigned c = 0; c < Chains; ++c) ranks[c] = c * 17;
    for (unsigned step = 0; step < rounds; ++step)
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one = [&](auto i) __attribute__((always_inline)) { ranks[i] = tail(read(ranks[i])); };
            (one(std::integral_constant<unsigned, I>{}), ...);
        }(std::make_index_sequence<Chains>{});
    return std::accumulate(ranks.begin(), ranks.end(), word{});
}
template <class Read> [[gnu::noinline]] word projection(const fixture& f, unsigned rounds, bool shuffled, Read read) {
    word sum = 0;
    for (unsigned it = 0; it < rounds; ++it) for (unsigned r = 0; r < f.rows; ++r)
        sum += tail(read(shuffled ? (r * 0x9e3779b1u + 17) & (f.rows - 1) : r));
    return sum;
}
template <class A, unsigned Method> [[gnu::noinline]] word carried_range(const fixture& f, unsigned rounds, A prototype) {
    word sum = 0;
    const auto metadata = bl::metadata<A::family, typename A::width_type, typename A::offset_type>::from(f.views[0]);
    for (unsigned it = 0; it < rounds; ++it) for (const auto& v : f.views) {
        auto sink = [&](unsigned, sp::bytes value) __attribute__((always_inline)) { sum += tail(value); };
        if constexpr (Method==1) metadata.rebind(v).range(v.windows().payload, 0, 256, sink);
        else if constexpr(Method==2) {
            const boundlist_before::author::access<A::family,typename A::width_type,typename A::offset_type,std::byte> before(v);
            [[clang::always_inline]] before.range(0,256,sink);
        }else [[clang::always_inline]] prototype.rebind(v).range(0, 256, sink);
    }
    return sum;
}
inline volatile word observed;
template <class Run> void trial(const fixture& f, const char* name, const char* metric, unsigned rep, unsigned operations, word expected, Run run) {
    observed = run(); assert(observed == expected);
    const auto begin = std::chrono::steady_clock::now(); const auto result = run(); const auto end = std::chrono::steady_clock::now();
    observed = result; assert(result == expected);
    std::printf("%s,%u,%u,%u,%u,%u,%u,%s,%s,%u,%.6f,%zu,%zu,%zu,%zu,%llu\n",
        f.layout.schema().family == sp::geometry::width ? "width" : "direct", f.layout.schema().maximum, f.rows,
        unsigned(f.layout.has_used()), unsigned(f.layout.has_tombstones()), unsigned(f.layout.fixed()), unsigned(f.layout.schema().lengths),
        name, metric, rep, double(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / operations,
        f.metadata_bytes, f.payload.size(), f.payload.capacity(), f.views.size() * sizeof(sp::view), (unsigned long long)result);
}
void panel(fixture& f, unsigned operations, unsigned reps) {
    const auto oracle = [&](unsigned r) { return f.oracle(r); };
    const auto a = chase<1>(operations, oracle), b = chase<8>(operations / 8, oracle);
    const unsigned rounds = std::max(1u, operations / f.rows), count = rounds * f.rows;
    const auto sum = projection(f, rounds, false, oracle);
    bl::two_format_dispatch(f.views[0], [&](auto prototype) {
        using A = decltype(prototype);
        const auto meta = bl::metadata<A::family, typename A::width_type, typename A::offset_type>::from(f.views[0]);
        for (unsigned rep = 0; rep < reps; ++rep) {
            auto measure = [&]<unsigned Method>(const char* name) {
                using R=reader<Method,A>;
                const R read{f,typename R::access_type(f.views.front()),meta};
                trial(f, name, "dependent_value_chain", rep, operations, a, [&] { return chase<1>(operations, read); });
                trial(f, name, "independent8_value_chain", rep, operations / 8 * 8, b, [&] { return chase<8>(operations / 8, read); });
                trial(f, name, "ordered_projection", rep, count, sum, [&] { return projection(f, rounds, false, read); });
                trial(f, name, "shuffled_projection", rep, count, sum, [&] { return projection(f, rounds, true, read); });
            };
            for (unsigned i = 0; i < 5; ++i) switch (rep % 2 ? 4 - i : i) {
            case 0: measure.template operator()<0>("current_streampack"); break;
            case 1: measure.template operator()<1>("extracted_tuple"); break;
            case 2: measure.template operator()<2>("bounds_used_composition"); break;
            case 3: measure.template operator()<3>("independent_fused"); break;
            case 4: measure.template operator()<4>("before_streampack"); break;
            }
            if(rep%2) trial(f,"before_streampack","carried_range",rep,count,sum,[&]{return carried_range<A,2>(f,rounds,prototype);});
            trial(f, "current_streampack", "carried_range", rep, count, sum, [&] { return carried_range<A, 0>(f, rounds, prototype); });
            trial(f, "bounds_used_composition", "carried_range", rep, count, sum, [&] { return carried_range<A, 1>(f, rounds, prototype); });
            if(!(rep%2))trial(f,"before_streampack","carried_range",rep,count,sum,[&]{return carried_range<A,2>(f,rounds,prototype);});
        }
    });
}
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const unsigned nodes = std::stoul(argv[1]), ops = std::stoul(argv[2]), reps = std::stoul(argv[3]);
    if (!std::has_single_bit(nodes) || nodes > 4096 || ops < 8 || !reps) return 2;
    std::puts("layout,maximum,rows,used,tombstones,fixed,placement,implementation,metric,rep,ns,metadata_bytes,payload_bytes,payload_capacity_bytes,binding_bytes,checksum");
    for (auto family : {sp::geometry::direct, sp::geometry::width}) for (unsigned maximum : {255, 4095})
        for (bool full : {false, true}) for (auto placement : {sp::placement::grouped, sp::placement::separated})
            for (bool fixed : {false, true}) {
                if (std::getenv("BOUNDLIST_READ_LARGE_ONLY") && fixed) continue;
                fixture f(nodes, {family, placement, std::uint16_t(maximum), 256 * maximum,
                    fixed ? std::optional<std::uint16_t>{8} : std::nullopt, full, false});
                panel(f, ops, reps);
            }
}
