#include <ikea/streampack.h>
#include <ikea/streampack/author/native.h>
#include "prefix.h"
#include <pmu/pmu.h>
#include <fstream>
#include <linux/perf_event.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <sstream>
#include <vector>

namespace sp = ikea::streampack;
using Clock = std::chrono::steady_clock;
struct descriptor { std::uint32_t offset, length; };
struct release { void operator()(std::byte* p) const { std::free(p); } };
struct population {
    unsigned rows;
    sp::layout layout;
    std::unique_ptr<std::byte, release> metadata;
    std::vector<std::byte> payload;
    std::vector<descriptor> flat;
    std::vector<sp::view> views;
    population(unsigned n, sp::geometry family, unsigned maximum)
        : rows(n), layout(*sp::layout::make({family, sp::placement::grouped,
            std::uint16_t(maximum), 256 * maximum, {}, false, false})),
          payload(std::size_t(n) * 48), flat(n) {
        const auto dir = (layout.directory_bytes() + 63) & ~63u;
        const auto control = dir + layout.lengths_bytes();
        const auto stride = (control + layout.control_bytes() + 63) & ~63u;
        metadata.reset(static_cast<std::byte*>(std::aligned_alloc(64, std::size_t(n / 256) * stride)));
        assert(metadata);
        views.reserve(n / 256);
        std::uint32_t base = 0;
        std::array<ikea::journal::entry, 16> records;
        ikea::journal effects{records};
        for (unsigned block = 0; block < n / 256; ++block) {
            auto* p = metadata.get() + block * stride;
            views.push_back(*sp::view::attach(layout, {
                std::span(payload).subspan(base, 256 * 48), {p, layout.directory_bytes()},
                {p + dir, layout.lengths_bytes()}, {p + control, layout.control_bytes()}}));
            auto& v = views.back();
            effects.used = 0;
            assert(sp::initialize(v, effects));
            for (unsigned r = 0; r < 256; ++r) {
                const auto global = block * 256 + r;
                const auto len = 8 + (global * 13 % 24), reserve = len + global % 11;
                std::array<std::byte, 48> value;
                value.fill(std::byte(global & 255));
                const auto successor = (global * 1664525u + 1013904223u) & (n - 1);
                std::memcpy(value.data() + len - 4, &successor, 4);
                flat[global] = {base + v.extent(), len};
                effects.used = 0;
                assert(sp::append(v, std::span(value).first(len), reserve, effects));
            }
            assert(v.validate());
            base += v.extent();
        }
    }
    sp::bytes flat_value(unsigned r) const { return std::span(payload).subspan(flat[r].offset, flat[r].length); }
};
inline unsigned tail(sp::bytes b) { unsigned next; std::memcpy(&next, b.data() + b.size() - 4, 4); return next; }
// Fuse the same caller boundary for every candidate. The timed consumer remains
// noinline, while no candidate pays an accidental outlined read lambda per value.
template <class Read> [[gnu::always_inline]] inline unsigned read_tail(Read& read, unsigned rank) {
    [[clang::always_inline]] return tail(read(rank));
}
template <unsigned Chains, class Read>
[[gnu::noinline]] std::uint64_t chase(unsigned rounds, Read read) {
    std::array<unsigned, Chains> ranks{};
    for (unsigned c = 0; c < Chains; ++c) ranks[c] = c * 17;
    for (unsigned i = 0; i < rounds; ++i) {
        [&]<std::size_t... C>(std::index_sequence<C...>) {
            ((ranks[C] = read_tail(read, ranks[C])), ...);
        }(std::make_index_sequence<Chains>{});
    }
    return std::accumulate(ranks.begin(), ranks.end(), std::uint64_t{});
}
enum class projection_order { ordered, rotated, permuted };
constexpr unsigned projection_rank(unsigned i, unsigned rows, projection_order order) {
    if (order == projection_order::ordered) return i;
    if (order == projection_order::rotated) return (i + 17) & (rows - 1);
    // Each step is invertible modulo a power of two. This is a deterministic
    // permutation, without an extra rank-array load or a randomness claim.
    return (((i ^ (i >> 7)) * 0x9e3779b1u) + 17) & (rows - 1);
}
void validate_projection_orders(unsigned rows) {
    // A cold independent property check prevents a repeated-row or accidentally
    // sequential index map from qualifying as the permuted workload.
    std::vector<bool> visited(rows);
    unsigned previous = 0, nonconsecutive = 0;
    for (unsigned i = 0; i < rows; ++i) {
        const auto rank = projection_rank(i, rows, projection_order::permuted);
        assert(rank < rows && !visited[rank]);
        visited[rank] = true;
        if (i && ((rank - previous) & (rows - 1)) != 1) ++nonconsecutive;
        previous = rank;
    }
    assert(nonconsecutive >= rows / 2);
}
template <class Read> [[gnu::noinline]] std::uint64_t projection(unsigned rows, unsigned rounds, projection_order order, Read read) {
    std::uint64_t sum = 0;
    for (unsigned it = 0; it < rounds; ++it)
        for (unsigned i = 0; i < rows; ++i) sum += read_tail(read, projection_rank(i, rows, order));
    return sum;
}
volatile std::uint64_t observed;
template <class Run> void time(const population& p, const char* mode, const char* op, unsigned rep,
                               std::uint64_t count, std::uint64_t expected, Run run) {
    // Warm the actual path as well as the independent oracle, so descriptor
    // controls do not alone inherit their own preparation's cache state.
    const auto warm = run();
    if (warm != expected) std::abort();
    observed = warm;
    const auto* pmu_path = std::getenv("STREAMPACK_PMU");
    const std::array<workbench::pmu::EventSpec, 2> extras{{
        {"branch_misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES},
        {"cache_misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES}}};
    workbench::pmu::Session counters(pmu_path ? workbench::pmu::Mode::basic : workbench::pmu::Mode::off, extras);
    counters.start();
    const auto begin = Clock::now();
    auto sum = run();
    const auto end = Clock::now();
    counters.stop();
    if (pmu_path) {
        std::ofstream output(pmu_path, std::ios::app);
        output << "{\"family\":\"" << (p.layout.schema().family == sp::geometry::sizes ? "width" : "direct")
            << "\",\"maximum\":" << p.layout.schema().maximum << ",\"rows\":" << p.rows
            << ",\"path\":\"" << mode << "\",\"operation\":\"" << op << "\",\"repeat\":" << rep
            << ",\"values\":" << count << ",\"pmu\":";
        std::ostringstream report;
        counters.write_json(report);
        auto record = report.str();
        if (!record.empty() && record.back() == '\n') record.pop_back();
        output << record << "}\n";
    }
    if (sum != expected) { std::cerr << "checksum " << mode << ' ' << op << ' ' << sum << ' ' << expected << '\n'; std::abort(); }
    observed = sum;
    std::cout << (p.layout.schema().family == sp::geometry::sizes ? "width" : "direct") << ','
        << p.layout.schema().maximum << ',' << p.rows << ',' << mode << ',' << op << ',' << rep << ','
        << double(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / count << ',' << sum << '\n';
}
template <class Read> void measurements(const population& p, const char* name, unsigned rep, Read read) {
    const unsigned work = 1u << 20, rounds = std::max(1u, work / p.rows);
    auto baseline = [&](unsigned r) { return p.flat_value(r); };
    const auto c1 = chase<1>(work, baseline), c8 = chase<8>(work / 8, baseline);
    const auto projection_sum = projection(p.rows, rounds, projection_order::ordered, baseline);
    time(p, name, "chain1", rep, work, c1, [&] { return chase<1>(work, read); });
    time(p, name, "chain8", rep, work, c8, [&] { return chase<8>(work / 8, read); });
    time(p, name, "ordered", rep, std::uint64_t(rounds) * p.rows, projection_sum,
        [&] { return projection(p.rows, rounds, projection_order::ordered, read); });
    time(p, name, "rotated", rep, std::uint64_t(rounds) * p.rows, projection_sum,
        [&] { return projection(p.rows, rounds, projection_order::rotated, read); });
    time(p, name, "permuted", rep, std::uint64_t(rounds) * p.rows, projection_sum,
        [&] { return projection(p.rows, rounds, projection_order::permuted, read); });
}
void run(population& p, unsigned rep) {
    auto flat = [&] { measurements(p, "location8", rep, [&](unsigned r) { return p.flat_value(r); }); };
    auto ordinary = [&] { measurements(p, "ordinary", rep, [&](unsigned r) { const auto value = sp::value(p.views[r / 256], r % 256); if (!value) std::abort(); return *value; }); };
    auto typed = [&] { sp::author::visit(p.views[0], [&](auto prototype) {
        measurements(p, "typed", rep, [&](unsigned r) { return prototype.rebind_unchecked(p.views[r / 256]).value(r % 256); });
        measurements(p, "typed-checked", rep, [&](unsigned r) { const auto value = prototype.rebind_unchecked(p.views[r / 256]).get(r % 256); if (!value) std::abort(); return *value; });
    }); };
    // Matched spike kernels over the SAME bytes and owner windows. This control
    // removes only the public binding/schema route; it is not an external format.
    auto raw = [&] { sp::author::visit(p.views[0], [&](auto prototype) {
        using Access = decltype(prototype); using W = typename Access::width_type; using O = typename Access::offset_type;
        const auto& l = p.layout;
        const auto stride = l.group_stride(), used_offset = l.used_offset(), geometry_offset = l.geometry_offset();
        auto lookup = [&, stride, used_offset, geometry_offset](unsigned row) {
            const auto& w = p.views[row / 256].windows(); const auto r = row % 256;
            const auto slot = (r / 16) * stride + (r % 16) * sizeof(W);
            unsigned start;
            if constexpr (Access::family == sp::geometry::sizes)
                start = ikea::intervals::detail::load<O>(w.directory.data() + geometry_offset + (r / 16) * sizeof(O)) +
                    streampack_next::prefix16_native(reinterpret_cast<const W*>(w.lengths.data() + (r / 16) * stride), r % 16);
            else start = r ? ikea::intervals::detail::load<O>(w.directory.data() + geometry_offset + (r - 1) * sizeof(O)) : 0;
            return sp::bytes(w.payload).subspan(start, ikea::intervals::detail::load<W>(w.lengths.data() + used_offset + slot));
        };
        measurements(p, "spike-body", rep, lookup);
        measurements(p, "spike-checked", rep, [&](unsigned row) {
            const auto& view = p.views[row / 256];
            if (row % 256 >= view.size() || !view.live_unchecked(row % 256)) std::abort();
            return lookup(row);
        });
    }); };
    if (rep % 2) { raw(); typed(); ordinary(); flat(); }
    else { flat(); ordinary(); typed(); raw(); }
    const auto rounds = std::max(1u, (1u << 20) / p.rows);
    const auto expected = projection(p.rows, rounds, projection_order::ordered, [&](unsigned r) { return p.flat_value(r); });
    time(p, "range", "ordered", rep, std::uint64_t(rounds) * p.rows, expected, [&] {
        return sp::author::visit(p.views[0], [&](auto prototype) {
            std::uint64_t sum = 0;
            for (unsigned round = 0; round < rounds; ++round)
                for (const auto& v : p.views) prototype.rebind_unchecked(v).range(0, 256, [&](unsigned, sp::bytes b) { sum += tail(b); });
            return sum;
        });
    });
    std::array<std::uint16_t, 256> selected;
    for (unsigned i = 0; i < 256; ++i) selected[i] = (i * 137 + 17) % 256;
    time(p, "selection", "block-shuffled", rep, std::uint64_t(rounds) * p.rows, expected, [&] {
        std::uint64_t sum = 0;
        std::array<std::uint32_t, 256> scratch;
        for (unsigned round = 0; round < rounds; ++round)
            for (const auto& v : p.views) {
                auto ok = sp::select(v, selected, scratch, [&](unsigned, sp::bytes b) { sum += tail(b); });
                assert(ok);
            }
        return sum;
    });
    time(p, "location8", "block-shuffled", rep, std::uint64_t(rounds) * p.rows, expected, [&] {
        std::uint64_t sum = 0;
        for (unsigned round = 0; round < rounds; ++round)
            for (unsigned base = 0; base < p.rows; base += 256)
                for (auto r : selected) sum += tail(p.flat_value(base + r));
        return sum;
    });
}
int main(int argc, char** argv) {
    unsigned rows = argc > 1 ? std::stoul(argv[1]) : 256;
    unsigned maximum = argc > 2 ? std::stoul(argv[2]) : 4095;
    assert(rows >= 256 && std::has_single_bit(rows));
    validate_projection_orders(rows);
    std::cout << "family,maximum,rows,path,operation,repeat,ns_per_value,checksum\n";
    for (auto family : {sp::geometry::sizes, sp::geometry::absolute_ends}) {
        population p(rows, family, maximum);
        for (unsigned repeat = 0; repeat < 2; ++repeat) run(p, repeat);
    }
}
