#include <ikea/intervals.h>
#include <array>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
namespace bl = ikea::intervals;
using ikea::intervals::sparse_sizes;
using u64 = std::uint64_t;

void need(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template <unsigned G, class W, class P> struct image {
    static constexpr unsigned capacity = 256;
    std::array<W, capacity> widths{}; // Includes initialized final native padding.
    std::array<P, (capacity + G - 1) / G> checkpoints{};
    unsigned count{};
    P origin{}, extent{};
    unsigned maximum{};

    void rebuild() {
        P position = origin;
        for (unsigned i = 0; i < count; ++i) {
            if (i % G == 0) checkpoints[i / G] = position;
            position += widths[i];
        }
        extent = position;
    }
    sparse_sizes<G, W, P> source() const {
        return {widths.data(), checkpoints.data(), count, origin, extent, maximum};
    }
    struct stores { unsigned widths{}, checkpoints{}, extent{}; };
    stores replace(unsigned rank, W value) {
        need(rank < count && value <= maximum, "invalid replacement");
        if (value == widths[rank]) return {};
        const std::int64_t delta = std::int64_t(value) - widths[rank];
        // The fixture admits enough Point headroom for all test replacements.
        widths[rank] = value;
        stores changed{1, 0, 1};
        for (unsigned group = rank / G + 1; group < (count + G - 1) / G; ++group) {
            checkpoints[group] = static_cast<P>(checkpoints[group] + delta);
            ++changed.checkpoints;
        }
        extent = static_cast<P>(extent + delta);
        return changed;
    }
};

template <class Image> std::vector<u64> scalar_ends(const Image &data) {
    std::vector<u64> result;
    u64 position = data.origin;
    for (unsigned rank = 0; rank < data.count; ++rank) {
        position += data.widths[rank];
        result.push_back(position);
    }
    return result;
}

template <unsigned G, class W, class P, class Source>
void verify_source(const image<G, W, P> &data, const Source &source) {
    const auto ends = scalar_ends(data);
    const unsigned n = data.count;
    std::vector<u64> queries{0, data.origin, data.extent, std::numeric_limits<u64>::max()};
    if (data.origin) queries.push_back(data.origin - 1);
    if (data.extent < std::numeric_limits<u64>::max()) queries.push_back(data.extent + 1);
    for (unsigned rank = 0; rank < n; ++rank) {
        need(source.at(rank) == ends[rank], "rank endpoint mismatch");
        const auto located = bl::locate(source, rank);
        const u64 start = rank ? ends[rank - 1] : data.origin;
        need(located && located->first == start && located->limit == ends[rank],
             "complete locate mismatch");
        queries.push_back(ends[rank]);
        if (ends[rank]) queries.push_back(ends[rank] - 1);
    }
    need(!bl::locate(source, n), "out-of-range locate accepted");
    need(bl::lower_bound(source, -1ll) == 0 && bl::upper_bound(source, -1ll) == 0,
         "negative query narrowed into Point");
    for (u64 query : queries) {
        unsigned lower = 0, upper = 0;
        while (lower < n && ends[lower] < query) ++lower;
        while (upper < n && ends[upper] <= query) ++upper;
        need(bl::lower_bound(source, query) == lower, "lower bound mismatch");
        const auto pending_lower = bl::prepare_lower_bound(source, query);
        const auto pending_upper = bl::prepare_upper_bound(source, query);
        need(pending_lower.finish() == lower && pending_upper.finish() == upper,
             "prepared search disagrees with independent oracle");
        need(bl::upper_bound(source, query) == upper, "upper bound mismatch");
        if constexpr (G == 16) {
            const bl::delta_source<W, P> maintained{data.widths.data(), data.checkpoints.data(),
                                                   n, 16, data.extent};
            need(bl::lower_bound(maintained, query) == lower &&
                     bl::upper_bound(maintained, query) == upper,
                 "maintained G16 control disagrees with the independent oracle");
        }
        const unsigned owner = query < data.origin ? n : upper;
        need(bl::reverse(source, query) == owner, "inverse/payload rank mismatch");
    }

    // The public range path must use one carried walk, preserving original
    // coordinates even through zero-size ranks and an explicit selection.
    std::array<std::byte, 32> eligible{};
    for (unsigned rank = 0; rank < n; ++rank)
        if (rank % 3 != 1) eligible[rank / 8] |= std::byte(1u << (rank % 8));
    const bl::selected_source selected{source, bl::rank_selection{eligible}};
    for (unsigned first : {0u, n / 3, n}) {
        for (unsigned take : {0u, (n - first) / 2, n - first}) {
            unsigned visited = 0;
            auto answer = bl::range(selected, first, take, [&](unsigned rank, auto bounds) {
                need(rank >= first && rank < first + take && rank % 3 != 1,
                     "range changed selected coordinates");
                need(bounds.first == (rank ? ends[rank - 1] : data.origin) &&
                         bounds.limit == ends[rank], "carried interval mismatch");
                ++visited;
            });
            unsigned expected = 0;
            for (unsigned rank = first; rank < first + take; ++rank)
                expected += rank % 3 != 1;
            need(answer.has_value() && visited == expected, "selected range count mismatch");
        }
    }
    need(!bl::range(source, n, 1, [](auto, auto) {}), "invalid range accepted");

    // Overlap is checked by interval intersection, independently of endpoint
    // searches or checkpoint routing. Internal zero-width holes remain holes.
    const u64 middle = data.origin + (data.extent - data.origin) / 2;
    const std::array windows{std::pair{u64(data.origin), u64(data.extent)},
                             std::pair{middle, middle},
                             std::pair{middle, u64(data.extent)},
                             std::pair{u64(data.extent), u64(data.origin)}};
    for (auto [low, high] : windows) {
        unsigned first = n, limit = n;
        if (low < high) {
            for (unsigned rank = 0; rank < n; ++rank) {
                const u64 start = rank ? ends[rank - 1] : data.origin;
                if (start < ends[rank] && ends[rank] > low && start < high) {
                    if (first == n) first = rank;
                    limit = rank + 1;
                }
            }
        }
        need(bl::overlap(source, low, high) == bl::rank_window{first, limit}, "overlap mismatch");
    }
}

template <unsigned G, class W, class P> void verify(const image<G, W, P> &data) {
    verify_source(data, data.source());
    verify_source(data, ikea::intervals::sparse_sizes<G,W,P,256/G>{
        data.widths.data(),data.checkpoints.data(),data.count,data.origin,data.extent,data.maximum});
    using Local = std::conditional_t<(u64(G) * std::numeric_limits<W>::max() <= 65535),
                                     std::uint16_t, std::uint32_t>;
    std::array<Local, 256> local_ends{};
    Local cumulative = 0;
    for (unsigned rank = 0; rank < data.count; ++rank) {
        if (rank % G == 0) cumulative = 0;
        cumulative += data.widths[rank];
        local_ends[rank] = cumulative;
    }
    verify_source(data, ikea::intervals::sparse_ends<G, Local, P>{
                            local_ends.data(), data.checkpoints.data(), data.count,
                            data.origin, data.extent});
    verify_source(data, ikea::intervals::sparse_ends<G,Local,P,256/G>{
        local_ends.data(),data.checkpoints.data(),data.count,data.origin,data.extent});
}

template <unsigned G, class W, class P> unsigned cases() {
    unsigned total_cases = 0;
    for (unsigned n : {0u, 1u, 15u, 16u, 17u, 63u, 64u, 65u, 255u, 256u}) {
        for (unsigned pattern = 0; pattern < 4; ++pattern) {
            image<G, W, P> data;
            data.count = n;
            data.maximum = std::numeric_limits<W>::max();
            const u64 value_limit = std::min<u64>(
                data.maximum, (u64(std::numeric_limits<P>::max()) - 32) / std::max(1u, n));
            std::mt19937 random(713);
            u64 total = 0;
            for (unsigned rank = 0; rank < n; ++rank) {
                const auto value = pattern == 0 ? std::min<u64>(1, value_limit) :
                                   pattern == 1 ? (rank % 2 ? value_limit : 0u) :
                                   pattern == 2 && rank >= 64 ? random() % (value_limit + 1) : 0u;
                data.widths[rank] = static_cast<W>(value);
                total += value;
            }
            // Near-top origins catch accidental narrowing of checkpoint bases;
            // narrow widths and local native thresholds still remain small.
            data.origin = static_cast<P>(std::numeric_limits<P>::max() - total - 32);
            data.rebuild();
            verify(data);
            if (n) {
                for (unsigned rank : {0u, n / 2, n - 1}) {
                    const W value = static_cast<W>(data.widths[rank] == data.maximum ?
                                                   data.widths[rank] - 1 : data.widths[rank] + 1);
                    const auto declared = data.replace(rank, value);
                    need(declared.widths == 1 && declared.extent == 1 &&
                             declared.checkpoints == (n + G - 1) / G - rank / G - 1,
                         "declared replacement field count mismatch");
                    verify(data);
                }
            }
            ++total_cases;
        }
    }
    return total_cases;
}

template <unsigned G> unsigned check_spacing() {
    return cases<G, std::uint8_t, std::uint16_t>() + cases<G, std::uint16_t, std::uint16_t>() +
           cases<G, std::uint8_t, std::uint32_t>() + cases<G, std::uint8_t, u64>() +
           cases<G, std::uint16_t, std::uint32_t>() + cases<G, std::uint16_t, u64>();
}
} // namespace

int main() {
    try {
        const unsigned count = check_spacing<16>() + check_spacing<32>() + check_spacing<64>() +
                               check_spacing<128>() + check_spacing<256>();
        std::printf("%u fixture cases passed for both sparse Sizes and Relative Ends: exact endpoints, inverse, carried/selected ranges, "
                    "overlap and ordered replacements. No timing or publication claim.\n", count);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
