// Complete ordered-node lookup: prepared request, search, selected 64B child,
// rank/child-dependent next request. Every query uses an independent rank oracle.
#include <ikea/boundlist.h>
#include <ikea/boundlist/author/search.h>
#include "fused.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <vector>
namespace bl = ikea::boundlist;
using word = std::uint64_t;
word mix(word x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
struct alignas(64) child {
    word salt;
    std::array<word, 7> padding{};
};
template <class T> struct fixture {
    struct request {
        T point;
        unsigned rank, link;
    };
    unsigned count, distribution;
    std::vector<T> ends;
    std::vector<child> children;
    std::array<request, 1024> requests;
    fixture(unsigned n, unsigned d) : count(n), distribution(d), ends(n), children(n + 1) {
        const word origin = sizeof(T) == 1 ? 0 : word(1) << (sizeof(T) * 8 - 2);
        const word range = sizeof(T) == 1 ? 250 : n * 3;
        for (unsigned r = 0; r < n; ++r) {
            const word position = d == 0   ? word(r + 1) * range / n
                                  : d == 1 ? word(r / 8) * range / ((n + 7) / 8)
                                  : r < n * 7 / 8
                                      ? 0
                                      : word(r - n * 7 / 8 + 1) * range / (n - n * 7 / 8);
            ends[r] = T(origin + position);
            children[r].salt = mix(r + 713);
        }
        children[n].salt = mix(n + 713);
        std::array<unsigned, 1024> order;
        std::iota(order.begin(), order.end(), 0);
        for (unsigned i = 1023; i; --i)
            std::swap(order[i], order[mix(i + 971) % (i + 1)]);
        for (unsigned i = 0; i < 1024; ++i) {
            const auto index = order[i], next = order[(i + 1) % 1024];
            const T q = T(origin + mix(index + 17) % (range + 2));
            unsigned rank = 0;
            while (rank < n && ends[rank] < q)
                ++rank;
            requests[index] = {q, rank, next - rank - unsigned(children[rank].salt)};
        }
    }
};
template <unsigned Method, class T> [[gnu::always_inline]] unsigned find(const fixture<T> &f, T q) {
    if constexpr (Method == 0)
        return bl::lower_bound(bl::direct_source<T>{f.ends.data(), f.count}, q);
    else if constexpr (Method == 1)
        return boundlist_fused::flat<true>(f.ends.data(), f.count, q);
    else if constexpr (Method == 2)
        return unsigned(std::lower_bound(f.ends.begin(), f.ends.end(), q) - f.ends.begin());
    else
        return bl::lower_bound(bl::author::scan_source<T>{f.ends.data(), f.count}, q);
}
template <unsigned C, unsigned Method, class T>
[[gnu::noinline]] word consume(const fixture<T> &f, unsigned ops) {
    std::array<unsigned, C> state;
    for (unsigned c = 0; c < C; ++c)
        state[c] = c * 127;
    for (unsigned step = 0; step < ops / C; ++step)
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one = [&](auto c) __attribute__((always_inline)) {
                const auto q = f.requests[state[c]];
                const auto rank = Method == 4 ? q.rank : find<Method == 4 ? 0 : Method>(f, q.point);
                state[c] = (rank + unsigned(f.children[rank].salt) + q.link) & 1023;
            };
            (one(std::integral_constant<unsigned, I>{}), ...);
        }(std::make_index_sequence<C>{});
    word sum = 0;
    for (auto x : state)
        sum += x;
    return sum;
}
inline volatile word observed;
template <class T, unsigned C> void panel(fixture<T> &f, unsigned ops, unsigned reps) {
    const auto expected = consume<C, 4>(f, ops);
    auto run = [&]<unsigned M>(unsigned rep, const char *name) {
        for (const auto q : f.requests)
            assert(find<M>(f, q.point) == q.rank);
        observed = consume<C, M>(f, 1024);
        const auto start = std::chrono::steady_clock::now();
        const auto result = consume<C, M>(f, ops);
        const auto stop = std::chrono::steady_clock::now();
        observed = result;
        assert(result == expected);
        std::printf(
            "%zu,%u,%u,%s,%u,%u,%.6f,%zu,%zu,%llu\n", sizeof(T) * 8, f.count, f.distribution, name,
            C, rep,
            double(std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()) /
                (ops / C * C),
            f.ends.size() * sizeof(T), f.children.size() * sizeof(child),
            static_cast<unsigned long long>(result));
    };
    for (unsigned rep = 0; rep < reps; ++rep)
        for (unsigned i = 0; i < 4; ++i) {
            switch (rep % 2 ? 3 - i : i) {
            case 0:
                run.template operator()<0>(rep, "default");
                break;
            case 1:
                run.template operator()<1>(rep, "independent_fused");
                break;
            case 2:
                run.template operator()<2>(rep, "std_lower_bound");
                break;
            case 3:
#ifdef BOUNDLIST_PORTFOLIO_LEGACY
                if constexpr (sizeof(T) != 1)
#endif
                {
                    run.template operator()<3>(rep, "opt_in_scan");
                }
                break;
            }
        }
}
int main(int argc, char **argv) {
    const unsigned ops = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 131071,
                   reps = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 3;
    std::puts(
        "bits,count,distribution,method,concurrency,rep,ns_request,end_bytes,child_bytes,checksum");
    auto all = [&]<class T>() {
        for (unsigned n : {16u, 32u, 64u, 128u, 256u, 4096u})
            for (unsigned d = 0; d < 3; ++d) {
                fixture<T> f(n, d);
                panel<T, 1>(f, ops, reps);
                panel<T, 8>(f, ops, reps);
            }
    };
    all.template operator()<std::uint8_t>();
    all.template operator()<std::uint16_t>();
    all.template operator()<std::uint32_t>();
    all.template operator()<std::uint64_t>();
}
