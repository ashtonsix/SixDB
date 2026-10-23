#pragma once
// Shared complete-consumer fixtures and strong compact/frozen controls.
#include <ikea/intervals.h>
#include <ikea/streampack/detail/compact/read.h>
#include <ikea/streampack/detail/compact/write.h>
#include "control/sparse_before.h"

#include <ikea/effects.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace intervals_probe {
namespace bl = ikea::intervals;
using u64 = std::uint64_t;
constexpr unsigned capacity = 256, query_count = 4096;

void need(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

struct fixture {
    std::array<unsigned, capacity> widths{}, ends{};
    std::array<u64, capacity> children{};
    std::vector<std::uint8_t> payload;
    std::array<unsigned, query_count> ranks{}, points{}, short_starts{};
    unsigned count, maximum, extent{};
    fixture(unsigned n, unsigned m, unsigned seed = 713) : count(n), maximum(m) {
        std::mt19937 random(seed);
        for (unsigned i = 0; i < n; ++i) {
            widths[i] = random() % (m + 1);
            ends[i] = extent += widths[i];
            children[i] = random();
        }
        need(extent != 0, "empty payload fixture");
        payload.resize(extent);
        for (auto &v : payload) v = random();
        for (unsigned q = 0; q < query_count; ++q) {
            ranks[q] = random() % n;
            points[q] = random() % extent;
            short_starts[q] = random() % (n - 7);
        }
    }
    u64 use_interval(unsigned rank, unsigned first, unsigned limit) const {
        // An actual payload byte, plus its logical length. Empty intervals
        // contribute zero and never read a neighbour's payload.
        return limit - first + (first != limit ? payload[first] : 0u) + rank;
    }
    u64 expected(unsigned operation) const {
        u64 sum = 0;
        const unsigned queries = operation == 3 ? 16 : query_count;
        for (unsigned q = 0; q < queries; ++q) {
            if (operation == 1) {
                unsigned rank = 0;
                while (ends[rank] <= points[q]) ++rank;
                sum += children[rank];
                continue;
            }
            const unsigned first = operation == 0 ? ranks[q] :
                                   operation == 2 ? short_starts[q] : 0;
            const unsigned take = operation == 0 ? 1 : operation == 2 ? 8 : count;
            for (unsigned i = first; i < first + take; ++i)
                sum += use_interval(i, i ? ends[i - 1] : 0, ends[i]);
        }
        return sum;
    }
};

template <unsigned G, class W, class P, unsigned Maximum, bool Relative> struct sparse_image {
    using L = std::conditional_t<!Relative, W,
              std::conditional_t<(u64(G) * Maximum <= 255), std::uint8_t,
              std::conditional_t<(u64(G) * Maximum <= 65535), std::uint16_t, std::uint32_t>>>;
    std::array<L, capacity> fields{};
    std::array<P, capacity / G> checkpoints{};
    unsigned count;
    P extent;
    explicit sparse_image(const fixture &f) : count(f.count), extent(f.extent) {
        unsigned local = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (i % G == 0) {
                checkpoints[i / G] = i ? f.ends[i - 1] : 0;
                local = 0;
            }
            local += f.widths[i];
            fields[i] = Relative ? local : f.widths[i];
        }
    }
    auto source() const {
        if constexpr (Relative)
            return ikea::intervals::sparse_ends<G, L, P, capacity / G>{
                fields.data(), checkpoints.data(), count, 0, extent};
        else
            return ikea::intervals::sparse_sizes<G, W, P, capacity / G>{
                fields.data(), checkpoints.data(), count, 0, extent, Maximum};
    }
    auto before_source() const {
        if constexpr (Relative)
            return ikea::intervals_before::sparse_ends<G,L,P,capacity/G>{
                fields.data(),checkpoints.data(),count,0,extent};
        else
            return ikea::intervals_before::sparse_sizes<G,W,P,capacity/G>{
                fields.data(),checkpoints.data(),count,0,extent,Maximum};
    }
    std::size_t readable_bytes() const {
        return (count + 15) / 16 * 16 * sizeof(L) + (count + G - 1) / G * sizeof(P);
    }
};

struct compact_image {
    ikea::streampack::detail::compact::layout layout;
    std::vector<std::byte> directory, lengths, control;
    std::optional<ikea::streampack::detail::compact::view> view;
    compact_image(const fixture &f, ikea::streampack::detail::compact::geometry family)
        : layout(*ikea::streampack::detail::compact::layout::prepare({.family = family,
                                      .maximum = static_cast<std::uint16_t>(f.maximum),
                                      .extent_limit = capacity * f.maximum,
                                      .immortal = true})),
          directory(layout.directory_bytes()), lengths(layout.lengths_bytes(f.count)),
          control(layout.control_bytes()) {
        auto bound = ikea::streampack::detail::compact::view::attach(layout, {directory, lengths, control});
        need(bound.has_value(), "compact admission failed");
        view = *bound;
        std::array<ikea::journal::entry, 16> records;
        {
            ikea::journal effects{records};
            need(ikea::streampack::detail::compact::initialize(*view, effects).has_value(), "compact initialization failed");
        }
        for (unsigned i = 0; i < f.count; ++i) {
            ikea::journal effects{records};
            need(ikea::streampack::detail::compact::append(*view, f.widths[i], effects).has_value(), "compact append failed");
        }
        need(view->validate().has_value(), "compact image invalid");
    }
    std::size_t bytes() const { return directory.size() + lengths.size() + control.size(); }
};

// Separate operation instantiations leave no operation branch in query loops.
template <unsigned Operation, class Source>
u64 run(const Source &source, const fixture &f) {
    std::array<u64, 4> sums{};
    const unsigned queries = Operation == 3 ? 16 : query_count;
    for (unsigned q = 0; q < queries; ++q) {
        u64 value = 0;
        if constexpr (Operation == 0) {
            const unsigned rank = f.ranks[q];
            const auto bounds = bl::locate(source, rank);
            if (!bounds) std::abort();
            value = f.use_interval(rank, bounds->first, bounds->limit);
        } else if constexpr (Operation == 1) {
            const unsigned rank = bl::reverse(source, f.points[q]);
            if (rank >= f.count) std::abort();
            value = f.children[rank];
        } else {
            const unsigned first = Operation == 2 ? f.short_starts[q] : 0;
            const unsigned take = Operation == 2 ? 8 : f.count;
            const auto answer = bl::range(source, first, take, [&](unsigned rank, auto interval) {
                value += f.use_interval(rank, interval.first, interval.limit);
            });
            if (!answer) std::abort();
        }
        sums[q % 4] += value;
    }
    return sums[0] + sums[1] + sums[2] + sums[3];
}

} // namespace intervals_probe
