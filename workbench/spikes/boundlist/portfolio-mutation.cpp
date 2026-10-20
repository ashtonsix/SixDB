// Complete admitted construction/packing against a raw, independently fused
// implementation. Input restoration and independent output checks are untimed.
#include "portfolio-fixture.h"
#include <ikea/streampack/author/write.h>

template <class T> void put(std::byte *p, T x) { std::memcpy(p, &x, sizeof(x)); }
template <class A> unsigned geometry_at(A a, unsigned r) {
    using W = typename A::width_type;
    unsigned base = 0;
#ifndef BOUNDLIST_PORTFOLIO_LEGACY
    base = a.format().geometry_lengths_offset();
#endif
    return base + (a.format().schema().lengths == sp::placement::grouped
                       ? (r / 16) * a.format().group_stride() + (r % 16) * sizeof(W)
                       : r * sizeof(W));
}
template <class A> unsigned used_at(A a, unsigned r) {
    using U = typename used_type<A>::type;
    const auto &l = a.format();
    return l.used_offset() + (l.schema().lengths == sp::placement::grouped
                                  ? (r / 16) * l.group_stride() + (r % 16) * sizeof(U)
                                  : r * sizeof(U));
}
using inputs = std::array<std::array<std::byte, 64>, 256>;
template <class A>
[[gnu::always_inline]] void fused_construct(A a, unsigned count, const inputs &values) {
    using W = typename A::width_type;
    using O = typename A::offset_type;
    using U = typename used_type<A>::type;
    const auto &l = a.format();
    const auto &w = a.source().windows();
    std::memset(w.directory.data(), 0, w.directory.size());
    std::memset(w.lengths.data(), 0, w.lengths.size());
    unsigned end = 0, local = 0;
    const unsigned upper = std::min(unsigned(l.schema().maximum), l.schema().byte_limit / count);
    for (unsigned r = 0; r < count; ++r) {
        const unsigned width = std::min(upper, 16u + (r * 13) % 16),
                       used = l.has_used() ? width - 4 : width;
        std::memcpy(w.payload.data() + end, values[r].data(), used);
        if (width > used)
            std::memset(w.payload.data() + end + used, 0, width - used);
        if constexpr (A::family == sp::geometry::direct)
            put(w.directory.data() + l.geometry_offset() + r * sizeof(O), O(end + width));
        else {
            if (!(r % 16)) {
                put(w.directory.data() + l.geometry_offset() + (r / 16) * sizeof(O), O(end));
                local = 0;
            }
            local += width;
            put(w.lengths.data() + geometry_at(a, r), W(relative<A> ? local : width));
        }
        if (l.has_used())
            put(w.lengths.data() + used_at(a, r), U(used));
        end += width;
    }
    put(w.control.data(), std::uint16_t(count));
    if (l.has_extent())
        put(w.control.data() + 2, std::uint32_t(end));
}
template <class A> [[gnu::always_inline]] void fused_pack(A a, unsigned n) {
    using W = typename A::width_type;
    using O = typename A::offset_type;
    using U = typename used_type<A>::type;
    const auto &l = a.format();
    const auto &w = a.source().windows();
    unsigned from = 0, to = 0, old_local = 0, new_local = 0;
    for (unsigned r = 0; r < n; ++r) {
        unsigned width;
        if constexpr (A::family == sp::geometry::direct) {
            const auto end = raw<O>(w.directory.data() + l.geometry_offset() + r * sizeof(O));
            width = end - from;
        } else if constexpr (relative<A>) {
            if (!(r % 16))
                old_local = 0;
            const auto end = raw<W>(w.lengths.data() + geometry_at(a, r));
            width = end - old_local;
            old_local = end;
        } else
            width = raw<W>(w.lengths.data() + geometry_at(a, r));
        const bool live = !(std::to_integer<unsigned>(w.directory[r / 8]) & (1u << (r % 8)));
        const unsigned used = l.has_used() ? raw<U>(w.lengths.data() + used_at(a, r)) : width;
        const unsigned keep = live ? used : 0;
        if (keep && from != to)
            std::memmove(w.payload.data() + to, w.payload.data() + from, keep);
        if constexpr (A::family == sp::geometry::direct)
            put(w.directory.data() + l.geometry_offset() + r * sizeof(O), O(to + keep));
        else {
            if (!(r % 16)) {
                put(w.directory.data() + l.geometry_offset() + (r / 16) * sizeof(O), O(to));
                new_local = 0;
            }
            new_local += keep;
            put(w.lengths.data() + geometry_at(a, r), W(relative<A> ? new_local : keep));
        }
        if (l.has_used() && !live)
            put(w.lengths.data() + used_at(a, r), U(0));
        from += width;
        to += keep;
    }
    if (l.has_extent())
        put(w.control.data() + 2, std::uint32_t(to));
}
template <unsigned C, unsigned Method, unsigned Operation, class A>
[[gnu::noinline]] word run(fixture &f, A prototype, const inputs &values) {
    word sum = 0;
    ikea::no_coverage effects;
    [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
        auto one = [&](auto index) __attribute__((always_inline)) {
            const auto v = f.block(index);
            const auto a = prototype.rebind(v);
            if constexpr (Operation == 0) {
                if constexpr (Method == 2)
                    fused_construct(a, f.count, values);
                else {
                    sp::author::initialize_unchecked(v, effects);
#ifndef BOUNDLIST_PORTFOLIO_LEGACY
                    if constexpr (Method == 1) {
                        sp::author::value_encoder encoder(a, 0, 0, effects);
                        for (unsigned r = 0; r < f.count; ++r) {
                            const auto reserve = f.ends[r + 1] - f.ends[r],
                                       used = a.format().has_used() ? reserve - 4 : reserve;
                            encoder.append(std::span(values[r]).first(used), reserve);
                        }
                        encoder.finish();
                    } else
#endif
                        for (unsigned r = 0; r < f.count; ++r) {
                            const auto reserve = f.ends[r + 1] - f.ends[r],
                                       used = a.format().has_used() ? reserve - 4 : reserve;
                            sp::author::append_unchecked(a, std::span(values[r]).first(used),
                                                         reserve, effects);
                        }
                }
            } else if constexpr (Operation == 2) {
                const auto source = f.block(unsigned(index) + 8);
                std::array<std::uint16_t, 256> order;
                for (unsigned r = 0; r < f.count; ++r)
                    order[r] = (r * 193 + 17) & (f.count - 1);
                assert(sp::transfer(source, std::span(order).first(f.count), v, true, effects));
            } else {
                if constexpr (Method == 2)
                    fused_pack(a, f.count);
                else
                    sp::author::soft_pack_unchecked(a, true, effects);
            }
            // Full stores and a real result escape, including emitted payload.
            asm volatile(""
                         :
                         : "r"(v.windows().payload.data()), "r"(v.windows().directory.data()),
                           "r"(v.windows().lengths.data()), "r"(v.windows().control.data())
                         : "memory");
            sum += v.extent() + std::to_integer<unsigned>(v.windows().payload[0]);
        };
        (one(std::integral_constant<unsigned, I>{}), ...);
    }(std::make_index_sequence<C>{});
    return sum;
}
inline volatile word observed_mutation;
template <unsigned C, unsigned Method, unsigned Operation, class A>
void trial(fixture &f, A a, unsigned rounds, unsigned rep) {
    std::vector<std::byte> saved(f.metadata.get(), f.metadata.get() + 8 * f.metadata_stride);
    std::vector<std::byte> body(f.payload.get(), f.payload.get() + 8 * f.payload_stride);
    inputs values{};
    for (unsigned r = 0; r < f.count; ++r) {
        const auto width = f.ends[r + 1] - f.ends[r],
                   used = f.layout.has_used() ? width - 4 : width;
        put(values[r].data() + used - 4, unsigned(r * 17 + 9));
    }
    std::chrono::nanoseconds elapsed{};
    word sum = 0;
    for (unsigned i = 0; i < rounds; ++i) {
        std::memcpy(f.metadata.get(), saved.data(), saved.size());
        std::memcpy(f.payload.get(), body.data(), body.size());
        const auto begin = std::chrono::steady_clock::now();
        sum += run<C, Method, Operation>(f, a, values);
        const auto end = std::chrono::steady_clock::now();
        elapsed += std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);
        // Independent whole-result geometry and payload oracle, outside timing.
        for (unsigned b = 0; b < C; ++b) {
            const auto v = f.block(b);
            assert(v.validate() && v.size() == f.count);
            const auto access = a.rebind(v);
            unsigned end = 0;
            for (unsigned r = 0; r < f.count; ++r) {
                const bool live = Operation != 1 || r % 5;
                const unsigned from = Operation == 2 ? (r * 193 + 17) & (f.count - 1) : r;
                const unsigned reserve = f.ends[from + 1] - f.ends[from],
                               used = f.layout.has_used() ? reserve - 4 : reserve;
                const auto width = Operation == 0 ? reserve : (live ? used : 0);
                assert(access.start(r) == end && access.reserved(r) == width &&
                       access.live(r) == live);
                if (live) {
                    const auto value = access.value(r);
                    assert(value.size() == used);
                    const auto expected =
                        Operation == 0
                            ? r * 17 + 9
                            : (((b + (Operation == 2 ? 8 : 0)) * f.count + from) * 1664525u +
                               1013904223u) &
                                  f.mask;
                    assert(raw<unsigned>(value.data() + used - 4) == expected);
                }
                end += width;
            }
            assert(v.extent() == end);
        }
    }
    std::memcpy(f.metadata.get(), saved.data(), saved.size());
    std::memcpy(f.payload.get(), body.data(), body.size());
    observed_mutation = sum;
    std::printf("%u,%u,%u,%u,%u,%s,%s,%u,%u,%.6f,%.6f,%llu\n", unsigned(f.layout.schema().family),
                unsigned(f.layout.schema().maximum), f.layout.schema().byte_limit,
                unsigned(f.layout.has_used()), unsigned(f.layout.schema().lengths),
                Method == 2   ? "independent_fused"
                : Method == 1 ? "carried_encoder"
                              : "per_slot",
                Operation == 0   ? "construct"
                : Operation == 1 ? "soft_pack"
                                 : "shuffled_transfer",
                C, rep, double(elapsed.count()) / (rounds * C),
                double(elapsed.count()) / (rounds * C * f.count),
                static_cast<unsigned long long>(sum));
}
int main(int argc, char **argv) {
    const unsigned rounds = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 32;
    const unsigned reps = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 3;
    std::puts("encoding,maximum,limit,used,placement,method,operation,concurrency,rep,ns_block,ns_"
              "rank,checksum");
    std::vector<sp::geometry> formats{sp::geometry::direct, sp::geometry::width};
#ifndef BOUNDLIST_PORTFOLIO_LEGACY
    formats.push_back(sp::geometry::relative_ends);
#endif
    for (const auto [maximum, limit] :
         {std::pair{15u, 3840u}, std::pair{16u, 4096u}, std::pair{255u, 65280u},
          std::pair{4095u, 1048320u}, std::pair{4096u, 1048576u}, std::pair{65535u, 65535u}})
        for (auto family : formats)
            for (bool full : {false, true})
                for (auto placement : {sp::placement::grouped, sp::placement::separated}) {
                    if ((full || family == sp::geometry::direct) &&
                        placement == sp::placement::separated)
                        continue;
                    fixture f({family, placement, std::uint16_t(maximum), limit, {}, full, false},
                              16, 256);
                    sp::author::dispatch(*f.first, [&](auto a) {
                        for (unsigned rep = 0; rep < reps; ++rep) {
                            trial<1, 0, 2>(f, a, rounds, rep);
                            trial<8, 0, 2>(f, a, rounds, rep);
                            if (rep % 2) {
                                trial<1, 2, 0>(f, a, rounds, rep);
                                trial<8, 2, 0>(f, a, rounds, rep);
                            }
                            trial<1, 0, 0>(f, a, rounds, rep);
                            trial<8, 0, 0>(f, a, rounds, rep);
#ifndef BOUNDLIST_PORTFOLIO_LEGACY
                            trial<1, 1, 0>(f, a, rounds, rep);
                            trial<8, 1, 0>(f, a, rounds, rep);
#endif
                            if (!(rep % 2)) {
                                trial<1, 2, 0>(f, a, rounds, rep);
                                trial<8, 2, 0>(f, a, rounds, rep);
                            }
                        }
                    });
                    ikea::no_coverage effects;
                    for (unsigned b = 0; b < 8; ++b) {
                        auto v = f.block(b);
                        for (unsigned r = 0; r < 256; r += 5)
                            assert(sp::retire(v, r, effects));
                    }
                    sp::author::dispatch(*f.first, [&](auto a) {
                        for (unsigned rep = 0; rep < reps; ++rep) {
                            if (rep % 2) {
                                trial<1, 2, 1>(f, a, rounds, rep);
                                trial<8, 2, 1>(f, a, rounds, rep);
                            }
                            trial<1, 0, 1>(f, a, rounds, rep);
                            trial<8, 0, 1>(f, a, rounds, rep);
                            if (!(rep % 2)) {
                                trial<1, 2, 1>(f, a, rounds, rep);
                                trial<8, 2, 1>(f, a, rounds, rep);
                            }
                        }
                    });
                }
}
