#pragma once
// Complete value consumers from the first portfolio candidate. The fused point
// reader uses raw fields and a separately maintained explicit SIMD prefix.
#include <ikea/streampack.h>
#include <ikea/streampack/author/native.h>
#include "../streampack/prefix.h"
#include "portfolio-fused.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

namespace sp = ikea::streampack;
namespace sc = ikea::boundlist::sidecar;
using word = std::uint64_t;
template <class T> T raw(const std::byte *p) {
    T x;
    std::memcpy(&x, p, sizeof(x));
    return x;
}
template <class A, class = void> struct used_type {
    using type = typename A::width_type;
};
template <class A> struct used_type<A, std::void_t<typename A::used_type>> {
    using type = typename A::used_type;
};
template <class A>
constexpr bool relative =
#ifdef BOUNDLIST_PORTFOLIO_LEGACY
    false;
#else
    A::family == sp::geometry::relative_ends;
#endif
struct release {
    void operator()(std::byte *p) const { std::free(p); }
};
struct fixture {
    sp::layout layout;
    unsigned nodes, count, mask, payload_stride, metadata_stride, length_at, control_at;
    std::unique_ptr<std::byte, release> metadata, payload;
    std::optional<sp::view> first;
    std::array<unsigned, 257> ends{};
    fixture(sp::description d, unsigned blocks, unsigned n)
        : layout(*sp::layout::prepare(d)), nodes(blocks), count(n), mask(blocks * n - 1) {
        assert(n && n <= 256 && std::has_single_bit(n * blocks));
        const unsigned upper = std::min(unsigned(d.maximum), d.byte_limit / n);
        assert(upper >= 8);
        for (unsigned r = 0; r < n; ++r)
            ends[r + 1] = ends[r] + std::min(upper, 16u + (r * 13) % 16);
        payload_stride = (ends[n] + 63) & ~63u;
        length_at = (layout.directory_bytes() + 63) & ~63u;
        control_at = length_at + layout.lengths_bytes(n);
        metadata_stride = (control_at + layout.control_bytes() + 63) & ~63u;
        metadata.reset(static_cast<std::byte *>(
            std::aligned_alloc(64, std::size_t(blocks) * metadata_stride)));
        payload.reset(
            static_cast<std::byte *>(std::aligned_alloc(64, std::size_t(blocks) * payload_stride)));
        assert(metadata && payload);
        ikea::no_coverage effects;
        std::array<std::byte, 64> input{};
        for (unsigned b = 0; b < blocks; ++b) {
            auto source = block(b);
            if (!b)
                first = source;
            assert(sp::initialize(source, effects));
            for (unsigned r = 0; r < n; ++r) {
                const auto reserve = ends[r + 1] - ends[r], used = d.full ? reserve : reserve - 4;
                const unsigned next = ((b * n + r) * 1664525u + 1013904223u) & mask;
                std::memcpy(input.data() + used - 4, &next, 4);
                assert(sp::append(source, std::span(input).first(used), reserve, effects));
            }
            assert(source.validate());
        }
    }
    sp::view block(unsigned b) const {
        auto *p = metadata.get() + std::size_t(b) * metadata_stride;
        auto *v = payload.get() + std::size_t(b) * payload_stride;
        return sp::view::assume_valid(layout, {{v, payload_stride},
                                               {p, layout.directory_bytes()},
                                               {p + length_at, layout.lengths_bytes(count)},
                                               {p + control_at, layout.control_bytes()}});
    }
    std::size_t encoded() const {
        return std::size_t(nodes) *
               (layout.directory_bytes() + layout.lengths_bytes(count) + layout.control_bytes());
    }
};
