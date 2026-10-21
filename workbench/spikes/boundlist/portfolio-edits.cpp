#include <ikea/boundlist.h>
#include "portfolio-edit-fused.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
namespace sc = ikea::boundlist;
using word = std::uint64_t;
template <class T> T get(const std::byte *p) {
    T x;
    std::memcpy(&x, p, sizeof(x));
    return x;
}
template <class T> void put(std::byte *p, T x) { std::memcpy(p, &x, sizeof(x)); }
struct fixture {
    sc::layout layout;
    struct alignas(64) body {
        std::array<std::byte, 1088> directory{}, lengths{};
        std::array<std::byte, 6> control{};
    };
    std::array<body, 8> data;
    std::array<sc::view, 8> views;
    fixture(sc::geometry g, unsigned max)
        : layout(*sc::layout::prepare({g, std::uint16_t(max), 256 * max, {}, false})),
          views{make(0), make(1), make(2), make(3), make(4), make(5), make(6), make(7)} {
        ikea::no_coverage none;
        for (auto &v : views) {
            assert(sc::initialize(v, none));
            for (unsigned r = 0; r < 256; ++r)
                assert(sc::append(v, 16, none));
        }
    }
    sc::view make(unsigned b) {
        return *sc::view::bind(layout,
                               {std::span(data[b].directory).first(layout.directory_bytes()),
                                std::span(data[b].lengths).first(layout.lengths_bytes()),
                                std::span(data[b].control).first(layout.control_bytes())});
    }
};
struct journal {
    std::array<ikea::owner_write, 16> entries;
    unsigned used = 0;
    template <class S> [[gnu::always_inline]] void before(const S &source, ikea::byte_write w) {
        entries[used++] = {&source, w};
    }
};
template <class A>
[[gnu::always_inline]] void fused_edit(A a, unsigned r, int delta, bool resize, journal &log) {
    using W = typename A::width_type;
    using O = typename A::offset_type;
    const auto &s = a.source();
    const auto &p = s.windows();
    const auto &l = a.format();
    auto store = [&]<class T>(sc::plane plane, unsigned at, T value) {
        log.before(s, {unsigned(plane), at, sizeof(T)});
        put((plane == sc::plane::directory ? p.directory
             : plane == sc::plane::lengths ? p.lengths
                                           : p.control)
                    .data() +
                at,
            value);
    };
    auto add = [&]<class T>(sc::plane plane, unsigned at, unsigned first, unsigned limit,
                            unsigned padded, int d) {
        if (first == limit)
            return;
        log.before(s, {unsigned(plane), at, padded * sizeof(T)});
        edit_fused::add_suffix<T>((plane == sc::plane::directory ? p.directory : p.lengths).data() +
                                      at,
                                  first, limit, padded, d);
    };
    const auto geom = l.geometry_offset();
    if constexpr (A::family == sc::geometry::absolute_ends) {
        if (resize)
            add.template operator()<O>(sc::plane::directory, geom, r, 256, 256, delta);
        else
            store(sc::plane::directory, geom + r * sizeof(O),
                  O(get<O>(p.directory.data() + geom + r * sizeof(O)) + delta));
    } else {
        const auto at = (r / 16) * l.group_stride() + (r % 16) * sizeof(W);
        if constexpr (A::family == sc::geometry::sizes) {
            store(sc::plane::lengths, at, W(get<W>(p.lengths.data() + at) + delta));
            if (!resize && r + 1 < 256) {
                const auto next = ((r + 1) / 16) * l.group_stride() + ((r + 1) % 16) * sizeof(W);
                store(sc::plane::lengths, next, W(get<W>(p.lengths.data() + next) - delta));
            }
        } else if (resize)
            add.template operator()<W>(sc::plane::lengths, (r / 16) * l.group_stride(), r % 16, 16,
                                       16, delta);
        else
            store(sc::plane::lengths, at, W(get<W>(p.lengths.data() + at) + delta));
        if (resize)
            add.template operator()<O>(sc::plane::directory, geom, r / 16 + 1, 16, 16, delta);
        else if (r + 1 < 256 && !((r + 1) % 16)) {
            const auto next = geom + (r / 16 + 1) * sizeof(O);
            store(sc::plane::directory, next, O(get<O>(p.directory.data() + next) + delta));
            if constexpr (A::family == sc::geometry::relative_ends)
                add.template operator()<W>(sc::plane::lengths, (r / 16 + 1) * l.group_stride(), 0,
                                           16, 16, -delta);
        }
        if (resize || r == 255)
            store(sc::plane::control, 2, std::uint32_t(s.extent() + delta));
    }
}
template <unsigned C, bool Fused, bool Resize, class A>
[[gnu::noinline]] word run(fixture &f, A prototype, unsigned ops) {
    std::array<unsigned, C> state{};
    std::array<journal, C> logs;
    for (unsigned c = 0; c < C; ++c)
        state[c] = c;
    for (unsigned i = 0; i < ops / C; ++i)
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one = [&](auto c) __attribute__((always_inline)) {
                const auto &v = f.views[c];
                const auto a = prototype.rebind(v);
                const auto r = state[c];
                auto &log = logs[c];
                log.used = 0;
                for (int delta : {1, -1}) {
                    const auto old_end = a.at(r), old_size = a.slot_size(r);
                    if constexpr (Fused)
                        fused_edit(a, r, delta, Resize, log);
                    else if constexpr (Resize)
                        sc::author::resize_unchecked(a, r, old_size + delta, delta, log);
                    else
                        sc::author::set_boundary_unchecked(a, r, old_end + delta, delta, log);
                    // Each mutation is observable before its inverse. Without
                    // this boundary the compiler may cancel a reversible pair.
                    asm volatile(""
                                 :
                                 : "r"(v.windows().directory.data()),
                                   "r"(v.windows().lengths.data()), "r"(v.windows().control.data()),
                                   "r"(log.entries.data()), "r"(log.used)
                                 : "memory");
                }
                asm volatile(""
                             :
                             : "r"(v.windows().directory.data()), "r"(v.windows().lengths.data()),
                               "r"(v.windows().control.data())
                             : "memory");
                asm volatile("" : : "r"(log.entries.data()), "r"(log.used) : "memory");
                const auto end = a.at(r);
                state[c] = (r * 1664525u + 1013904223u + end - (r + 1) * 16) & 255;
                for (unsigned j = 0; j < log.used; ++j)
                    assert(log.entries[j].source == &v);
            };
            (one(std::integral_constant<unsigned, I>{}), ...);
        }(std::make_index_sequence<C>{});
    word sum = 0;
    for (auto x : state)
        sum += x;
    return sum;
}
inline volatile word observed;
template <unsigned C, bool Fused, bool Resize, class A>
void trial(fixture &f, A a, unsigned ops, unsigned rep) {
    const auto saved = f.data;
    const auto start = std::chrono::steady_clock::now();
    const auto sum = run<C, Fused, Resize>(f, a, ops);
    const auto stop = std::chrono::steady_clock::now();
    observed = sum;
    for (unsigned c = 0; c < C; ++c) {
        assert(f.views[c].validate());
        assert(f.data[c].directory == saved[c].directory && f.data[c].lengths == saved[c].lengths &&
               f.data[c].control == saved[c].control);
    }
    unsigned expected = 0;
    for (unsigned c = 0; c < C; ++c) {
        unsigned r = c;
        for (unsigned i = 0; i < ops / C; ++i)
            r = (r * 1664525u + 1013904223u) & 255;
        expected += r;
    }
    assert(sum == expected);
    std::printf("%u,%u,%s,%s,%u,%u,%.6f,%llu\n", unsigned(f.layout.schema().family),
                unsigned(f.layout.schema().maximum), Fused ? "independent_packed" : "shared",
                Resize ? "resize_pair" : "boundary_pair", C, rep,
                double(std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()) /
                    (ops / C * C),
                static_cast<unsigned long long>(sum));
}
int main(int argc, char **argv) {
    const unsigned ops = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 131071,
                   reps = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 3;
    std::puts("encoding,maximum,method,operation,concurrency,rep,ns_pair,checksum");
    for (auto g : {sc::geometry::absolute_ends, sc::geometry::sizes, sc::geometry::relative_ends})
        for (unsigned max : {4095u, 4096u, 65535u}) {
            fixture f(g, max);
            sc::author::dispatch(f.views[0], [&](auto a) {
                for (unsigned rep = 0; rep < reps; ++rep) {
                    auto arm = [&]<bool F>() {
                        trial<1, F, false>(f, a, ops, rep);
                        trial<8, F, false>(f, a, ops, rep);
                        trial<1, F, true>(f, a, ops, rep);
                        trial<8, F, true>(f, a, ops, rep);
                    };
                    if (rep % 2) {
                        arm.template operator()<true>();
                        arm.template operator()<false>();
                    } else {
                        arm.template operator()<false>();
                        arm.template operator()<true>();
                    }
                }
            });
        }
}
