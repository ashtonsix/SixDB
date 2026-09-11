// Ignored runtime-range diagnostic. Wire oracle derived independently from
// ikea/test/seriespack/physical_operations.cpp; no production encoder is used.
#pragma once
#include <ikea/seriespack.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace seriespack_runtime_ranges {
namespace sp = ikea::seriespack;
using word = std::uint64_t;
constexpr word seed = 0x64e987627a0b18cfULL;
constexpr std::size_t query_count = 4096, batch = 256, max_count = 17;
inline void require(bool good, const char* message) {
    if (!good) throw std::runtime_error(message);
}
inline word mix(word x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline word value_at(std::size_t index, unsigned width) {
    return mix(seed + index) & ((word{1} << width) - 1);
}
enum class placement { dense, headgapped, independentlygapped };
struct shape { unsigned width, head; sp::geometry geometry; placement placed; };
inline constexpr std::array shapes{
    shape{7, 0, sp::geometry::local8, placement::dense},
    shape{23, 16, sp::geometry::local8, placement::headgapped},
    shape{23, 16, sp::geometry::local8, placement::independentlygapped},
    shape{12, 0, sp::geometry::striped, placement::dense},
    shape{28, 16, sp::geometry::striped, placement::independentlygapped},
};
struct pattern { std::size_t origin, count; };
struct regime {
    const char* name;
    std::array<pattern, 4> patterns;
    std::size_t size;
};
inline std::vector<regime> regimes(sp::geometry geometry) {
    if (geometry == sp::geometry::local8) return {
        {"o1-n16", {{{1,16}}}, 1}, {"o1-n17", {{{1,17}}}, 1},
        {"mixed", {{{0,16},{1,16},{1,17},{7,2}}}, 4},
    };
    return {
        {"o16-n16", {{{16,16}}}, 1}, {"o17-n16", {{{17,16}}}, 1},
        {"o17-n17", {{{17,17}}}, 1},
        {"mixed", {{{16,16},{17,16},{17,17},{63,17}}}, 4},
    };
}
inline const char* target_name(sp::execution_target t) {
    switch (t) {
    case sp::execution_target::scalar: return "scalar";
    case sp::execution_target::avx2: return "avx2";
    case sp::execution_target::avx512: return "avx512";
    case sp::execution_target::neon: return "neon";
    default: return "automatic";
    }
}
inline std::string name(shape s, unsigned bytes, regime r, sp::execution_target t) {
    return std::string("runtime/series/") + target_name(t) + "/" +
        (s.geometry == sp::geometry::local8 ? "local" : "striped") +
        "/k" + std::to_string(s.width) + "/h" + std::to_string(s.head) +
        "/u" + std::to_string(8 * bytes) + "/" +
        (s.placed == placement::dense ? "dense" : s.placed == placement::headgapped ?
            "headgapped" : "independentlygapped") + "/" + r.name;
}
struct fixture {
    shape s;
    sp::description description;
    std::size_t n, tile_values, tiles;
    std::array<std::size_t, 3> tile_bytes{}, strides{}, envelopes{}, base{};
    std::array<std::vector<std::byte>, 3> storage;
    std::vector<sp::index_range> queries;
    std::size_t output_values = 0;
    word query_hash = 0;

    explicit fixture(shape selected, regime selected_regime)
        : s(selected), description{s.width, s.head, s.geometry},
          n(std::max<std::size_t>(256, (4096 * 8 / s.width) & ~std::size_t{255})),
          tile_values(s.geometry == sp::geometry::local8 ? 8 : 64), tiles(n / tile_values) {
        require(sp::validate(description) && sp::tile_values(description) == tile_values,
                "runtime shape geometry");
        const auto payload_width = s.width - s.head;
        require(payload_width == 7 || payload_width == 12, "oracle supports only W7/W12");
        tile_bytes = {tile_values * payload_width / 8, s.head ? tile_values : 0,
                      s.head == 16 ? tile_values : 0};
        for (unsigned p = 0; p < 3; ++p) {
            if (!tile_bytes[p]) continue;
            const bool gap = s.placed == placement::independentlygapped ||
                (s.placed == placement::headgapped && p != 0);
            strides[p] = tile_bytes[p] + (gap ? (p == 0 ?
                (s.geometry == sp::geometry::striped ? 32 : 11) : p == 1 ? 13 : 17) : 0);
            envelopes[p] = (tiles - 1) * strides[p] + tile_bytes[p];
            storage[p].assign(envelopes[p] + 128, std::byte{0xd3});
            auto address = reinterpret_cast<std::uintptr_t>(storage[p].data());
            base[p] = (64 - address % 64) % 64;
            if (p || s.geometry == sp::geometry::local8) base[p] += p + 1;
            for (std::size_t tile = 0; tile < tiles; ++tile)
                std::fill_n(storage[p].data() + base[p] + tile * strides[p], tile_bytes[p], std::byte{});
        }
        // Direct W7 Local and W12 Striped wire construction. Local low bits
        // occupy seven bit planes. Striped high bytes precede the shared low
        // nibble stripe, whose low/high nibbles encode the two 32-row groups.
        for (std::size_t i = 0; i < n; ++i) {
            const auto value = value_at(i, s.width), tile = i / tile_values, lane = i % tile_values;
            auto* payload = storage[0].data() + base[0] + tile * strides[0];
            if (s.geometry == sp::geometry::local8) {
                for (unsigned bit = 0; bit < 7; ++bit)
                    payload[bit] |= std::byte(((value >> bit) & 1) << lane);
            } else {
                payload[lane] = std::byte((value >> 4) & 255);
                payload[64 + lane % 32] |= std::byte((value & 15) << (4 * (lane / 32)));
            }
            for (unsigned h = 0; h < s.head / 8; ++h)
                storage[h + 1][base[h + 1] + tile * strides[h + 1] + lane] =
                    std::byte((value >> (s.width - 8 * (h + 1))) & 255);
        }
        queries.reserve(query_count);
        for (std::size_t i = 0; i < query_count; ++i) {
            const auto p = selected_regime.patterns[i % selected_regime.size];
            const auto legal_bases = (n - p.origin - p.count) / tile_values + 1;
            const auto tile = mix(seed + i * 0x9e3779b97f4a7c15ULL) % legal_bases;
            const auto begin = tile * tile_values + p.origin;
            queries.push_back({begin, begin + p.count});
            output_values += p.count;
            query_hash = mix(query_hash ^ begin ^ ((begin + p.count) << 32));
        }
    }
    std::span<const std::byte> plane(unsigned p) const {
        return envelopes[p] ? std::span(storage[p]).subspan(base[p], envelopes[p]) : std::span<const std::byte>{};
    }
    sp::const_view view() const {
        auto result = sp::const_view::attach(description, n,
            {{plane(0), strides[0]}, {{{plane(1), strides[1]}, {plane(2), strides[2]}}}});
        require(result.has_value(), "runtime placement admission");
        return *result;
    }
    template<class U> void verify(const sp::bound_reader& reader) const {
        std::array<U, max_count + 2> output;
        for (auto query : queries) {
            output.fill(U(0xd3));
            const auto count = query.end - query.begin;
            reader.decode(query, sp::output_values{std::span(output).subspan(1, count)});
            for (std::size_t j = 0; j < count; ++j)
                require(output[j + 1] == value_at(query.begin + j, s.width), "runtime output oracle");
            require(output.front() == U(0xd3), "runtime output prefix");
            for (auto j = count + 1; j < output.size(); ++j)
                require(output[j] == U(0xd3), "runtime exact output suffix");
        }
    }
};
} // namespace seriespack_runtime_ranges
