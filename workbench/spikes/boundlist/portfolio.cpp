#include "portfolio-fixture.h"
#include <pmu/pmu.h>
#include <iostream>
template <class A> [[gnu::always_inline]] sp::bytes fused_value(A a, unsigned r) {
    using W = typename A::width_type;
    using O = typename A::offset_type;
    using U = typename used_type<A>::type;
    const auto &l = a.format();
    const auto &p = a.source().windows();
    const auto *geometry = p.directory.data() + l.geometry_offset();
    unsigned begin;
    if (l.fixed())
        begin = r * l.fixed_width();
    else if constexpr (A::family == sp::geometry::direct)
        begin = r ? raw<O>(geometry + (r - 1) * sizeof(O)) : 0;
    else {
        begin = raw<O>(geometry + (r / 16) * sizeof(O));
        const auto *fields = p.lengths.data() + a.width_offset(r - r % 16);
        if constexpr (relative<A>)
            begin += r % 16 ? raw<W>(fields + (r % 16 - 1) * sizeof(W)) : 0;
        else
            begin += streampack_next::prefix16_native(reinterpret_cast<const W *>(fields), r % 16);
    }
    unsigned used;
    if (l.has_used())
        used = raw<U>(p.lengths.data() + a.used_offset(r));
    else if (l.fixed())
        used = l.fixed_width();
    else if constexpr (A::family == sp::geometry::direct)
        used = raw<O>(geometry + r * sizeof(O)) - begin;
    else if constexpr (relative<A>)
        used = raw<O>(geometry + (r / 16) * sizeof(O)) +
               raw<W>(p.lengths.data() + a.width_offset(r)) - begin;
    else
        used = raw<W>(p.lengths.data() + a.width_offset(r));
    return sp::bytes(p.payload).subspan(begin, used);
}
template <class A> [[gnu::always_inline]] unsigned fused_reverse(A a, unsigned point) {
    using W = typename A::width_type;
    using O = typename A::offset_type;
    const auto &l = a.format();
    const auto *geometry = a.source().windows().directory.data() + l.geometry_offset();
    if (l.fixed())
        return point / l.fixed_width();
    if constexpr (A::family == sp::geometry::direct)
        return boundlist_fused::flat<false>(reinterpret_cast<const O *>(geometry), a.size(),
                                            O(point));
    else {
        const auto groups = (a.size() + 15) / 16;
        const auto next =
            boundlist_fused::flat<false>(reinterpret_cast<const O *>(geometry), groups, O(point));
        const auto group = next - 1, first = group * 16;
        const auto relative_point = point - raw<O>(geometry + group * sizeof(O));
        const auto *fields = reinterpret_cast<const W *>(a.source().windows().lengths.data() +
                                                         a.width_offset(first));
        if constexpr (relative<A>)
            return first + boundlist_fused::flat<false>(fields, std::min(16u, a.size() - first),
                                                        W(relative_point));
        else if constexpr (sizeof(W) == 1)
            return first + boundlist_fused::byte_owner<false, false>(fields, relative_point, 65535);
        else
            return first + portfolio_fused::full_prefix_owner(
                               fields, relative_point,
                               std::min(16u * l.schema().maximum, l.extent_limit()));
    }
}
template <bool Fused, unsigned Mode, class A>
[[gnu::always_inline]] unsigned read_next(const fixture &f, A prototype, unsigned row,
                                          unsigned shift, unsigned rank_mask) {
    const auto view = f.block(row >> shift);
    const auto a = prototype.rebind(view);
    auto rank = row & rank_mask;
    if constexpr (Mode) {
        const auto point = f.ends[rank] + 1;
        bool dead;
        if constexpr (Fused) {
            rank = fused_reverse(a, point);
            dead = a.format().has_tombstones() &&
                   (std::to_integer<unsigned>(view.windows().directory[rank / 8]) &
                    (1u << (rank % 8)));
        } else {
            const auto owner = [&] {
                if constexpr (Mode == 1)
                    return a.reverse(point);
                else
                    return *sp::reverse(view, point);
            }();
            rank = owner.rank;
            dead = owner.tombstone;
        }
        if (dead)
            return row; // A fingerprint consumer discards retired matches.
    }
    const auto value = [&] __attribute__((always_inline)) {
        if constexpr (Fused)
            return fused_value(a, rank);
        else
            return a.value(rank);
    }();
    return raw<unsigned>(value.data() + value.size() - 4);
}
template <unsigned C, bool Fused, unsigned Mode, class A>
[[gnu::noinline]] word chase(const fixture &f, A prototype, unsigned operations) {
    std::array<unsigned, C> state;
    for (unsigned c = 0; c < C; ++c)
        state[c] = c & f.mask;
    const unsigned shift = std::countr_zero(f.count), rank_mask = f.count - 1;
    for (unsigned i = 0; i < operations / C; ++i)
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one = [&](auto c) __attribute__((always_inline)) {
                state[c] = read_next<Fused, Mode>(f, prototype, state[c], shift, rank_mask);
            };
            (one(std::integral_constant<unsigned, I>{}), ...);
        }(std::make_index_sequence<C>{});
    word sum = 0;
    for (auto x : state)
        sum += x;
    return sum;
}
template <unsigned C> word oracle(const fixture &f, unsigned operations) {
    word sum = 0;
    for (unsigned c = 0; c < C; ++c) {
        unsigned row = c & f.mask;
        for (unsigned i = 0; i < operations / C; ++i)
            row = (row * 1664525u + 1013904223u) & f.mask;
        sum += row;
    }
    return sum;
}
inline volatile word observed;
template <class Run>
void timed(const fixture &f, const char *method, const char *operation, unsigned c, unsigned rep,
           unsigned count, word expected, Run run) {
    observed = run();
    assert(observed == expected);
    workbench::pmu::Session counters(std::getenv("BOUNDLIST_COUNTERS")
                                         ? workbench::pmu::Mode::stalls
                                         : workbench::pmu::Mode::off);
    counters.start();
    const auto start = std::chrono::steady_clock::now();
    const auto result = run();
    const auto stop = std::chrono::steady_clock::now();
    counters.stop();
    observed = result;
    assert(result == expected);
    if (std::getenv("BOUNDLIST_COUNTERS")) {
        std::fprintf(
            stderr,
            "encoding=%u maximum=%u used=%u placement=%u method=%s op=%s C=%u rep=%u ops=%u ",
            unsigned(f.layout.schema().family), unsigned(f.layout.schema().maximum),
            unsigned(f.layout.has_used()), unsigned(f.layout.schema().lengths), method, operation,
            c, rep, count);
        counters.write_json(std::cerr);
    }
    std::printf("%u,%u,%u,%u,%u,%u,%u,%s,%s,%u,%u,%.6f,%zu,%zu,%zu,%zu,%llu\n",
                unsigned(f.layout.schema().family), unsigned(f.layout.schema().maximum),
                f.layout.schema().byte_limit, f.count, f.nodes, unsigned(f.layout.has_used()),
                unsigned(f.layout.schema().lengths), method, operation, c, rep,
                double(std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()) /
                    count,
                f.encoded(), std::size_t(f.nodes) * f.metadata_stride,
                std::size_t(f.nodes) * f.payload_stride, sizeof(sp::view),
                static_cast<unsigned long long>(result));
}
void panel(fixture &f, unsigned operations, unsigned reps) {
    sp::author::dispatch(*f.first, [&](auto a) {
        const unsigned shift = std::countr_zero(f.count), rank_mask = f.count - 1;
        // Independent every-rank checks precede timing. A final chase checksum
        // alone can hide a broken reader when the trace spans a whole cycle.
        for (unsigned row = 0; row < f.nodes * f.count; ++row) {
            const auto expected = (row * 1664525u + 1013904223u) & f.mask;
            assert((read_next<false, 0>(f, a, row, shift, rank_mask) == expected));
            assert((read_next<true, 0>(f, a, row, shift, rank_mask) == expected));
            assert((read_next<false, 1>(f, a, row, shift, rank_mask) == expected));
            assert((read_next<true, 1>(f, a, row, shift, rank_mask) == expected));
        }
        for (unsigned rep = 0; rep < reps; ++rep)
            for (unsigned order = 0; order < 2; ++order) {
                const bool fused = (rep % 2 ? 1 - order : order);
                auto run = [&]<bool Fused>() {
                    const auto *name = Fused ? "independent_fused" : "streampack";
                    auto operation = [&]<unsigned Mode>(const char *label) {
                        timed(f, name, label, 1, rep, operations, oracle<1>(f, operations),
                              [&] { return chase<1, Fused, Mode>(f, a, operations); });
                        timed(f, name, label, 8, rep, operations / 8 * 8, oracle<8>(f, operations),
                              [&] { return chase<8, Fused, Mode>(f, a, operations); });
                    };
                    operation.template operator()<0>("value_chain");
                    operation.template operator()<1>("reverse_value_chain");
                    if constexpr (!Fused)
                        operation.template operator()<2>("ordinary_reverse_value_chain");
                };
                if (fused)
                    run.template operator()<true>();
                else
                    run.template operator()<false>();
            }
    });
    // Full lifecycle checks run in the same first-candidate executable. Timed
    // writes/native/retained-owner panels are added alongside their controls.
    auto &v = *f.first;
    ikea::no_coverage effects;
    for (unsigned r = 0; r < f.count; r += 5)
        assert(sp::retire(v, r, effects));
    assert(sp::soft_pack(v, true, effects) && v.validate());
    unsigned end = 0;
    sp::author::dispatch(v, [&](auto a) {
        for (unsigned r = 0; r < f.count; ++r) {
            const auto original = f.ends[r + 1] - f.ends[r];
            const auto width = r % 5 ? original - (f.layout.has_used() ? 4 : 0) : 0;
            assert(a.start(r) == end && a.reserved(r) == width && a.live(r) == bool(r % 5));
            end += width;
        }
    });
    assert(v.extent() == end);
}
int main(int argc, char **argv) {
    const unsigned nodes = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 1;
    const unsigned operations = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 131072;
    const unsigned reps = argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 3;
    const unsigned count = argc > 4 ? std::strtoul(argv[4], nullptr, 10) : 256;
    std::puts("encoding,maximum,limit,count,nodes,used,placement,method,operation,concurrency,rep,"
              "ns_request,encoded_bytes,metadata_backing,payload_backing,view_bytes,checksum");
    std::vector<sp::geometry> formats{sp::geometry::direct, sp::geometry::width};
#ifndef BOUNDLIST_PORTFOLIO_LEGACY
    formats.push_back(sp::geometry::relative_ends);
#endif
    for (const auto [maximum, limit] :
         {std::pair{15u, 3840u}, std::pair{16u, 4096u}, std::pair{255u, 65280u},
          std::pair{4095u, 1048320u}, std::pair{4096u, 1048576u}, std::pair{65535u, 65535u},
          std::pair{65535u, 255u}})
        for (auto family : formats)
            for (bool full : {false, true})
                for (auto placement : {sp::placement::grouped, sp::placement::separated}) {
                    if (limit == 255 && count > 16)
                        continue;
                    if (std::getenv("BOUNDLIST_PORTFOLIO_LARGE_ONLY") &&
                        (maximum != 4095 || full || placement != sp::placement::grouped))
                        continue;
                    if ((full || family == sp::geometry::direct) &&
                        placement == sp::placement::separated)
                        continue;
                    fixture f({family, placement, std::uint16_t(maximum), limit, {}, full, false},
                              nodes, count);
                    panel(f, operations, reps);
                }
}
