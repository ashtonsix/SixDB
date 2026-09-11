#pragma once
#include <ikea2/seriespack/author/record.h>
#include <ikea2/seriespack/detail/placed_read.h>
#include <ikea2/seriespack/author/read.h>
#include "reference/physical.h"
#include "support.h"
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

namespace sp = ikea2::seriespack;
namespace cp = sp::composition;
namespace old = ikea2_reference::seriespack;
struct release {
    void operator()(std::uint8_t* p) const {
        std::free(p);
    }
};
template <class F> struct placed {
    static constexpr std::size_t count = 529;
    static constexpr auto tiles = (count + F::tile_rows - 1) / F::tile_rows;
    std::array<std::unique_ptr<std::uint8_t, release>, 3> memory;
    std::array<sp::plane<std::uint8_t>, 3> planes;
    std::vector<std::uint64_t> truth;
    placed() : truth(tiles * F::tile_rows) {
        constexpr std::array<std::size_t, 3> used{F::tile_bytes, F::heads >= 8 ? F::tile_rows : 0,
                                                  F::heads == 16 ? F::tile_rows : 0};
        std::mt19937_64 random(F::width * 31 + F::heads);
        for (auto& x : truth) {
            x = random();
            if constexpr (F::width < 64)
                x &= (std::uint64_t{1} << F::width) - 1;
        }
        for (unsigned p = 0; p < 3; ++p)
            if (used[p]) {
                const auto stride =
                    p == 0 ? (used[p] + 80) & ~std::size_t{63} : used[p] + 7 + p * 6;
                const auto bytes = (tiles - 1) * stride + used[p];
                memory[p].reset(static_cast<std::uint8_t*>(
                    std::aligned_alloc(64, (bytes + 63) & ~std::size_t{63})));
                IKEA2_CHECK(memory[p]);
                std::memset(memory[p].get(), 0x96, bytes);
                planes[p] = {{memory[p].get(), bytes}, stride};
            }
        for (std::size_t t = 0; t < tiles; ++t) {
            if constexpr (F::payload != 0) {
                std::array<std::uint64_t, F::tile_rows> payload;
                for (unsigned j = 0; j < F::tile_rows; ++j) {
                    payload[j] = truth[t * F::tile_rows + j];
                    if constexpr (F::payload < 64)
                        payload[j] &= (std::uint64_t{1} << F::payload) - 1;
                }
                constexpr auto G = F::storage == sp::geometry::local ? old::geometry::local8
                                                                     : old::geometry::striped;
                old::detail::encode_tile<F::payload, G>(payload.data(), planes[0].bytes.data() +
                                                                            t * planes[0].stride);
            }
            if constexpr (F::heads >= 8)
                for (unsigned j = 0; j < F::tile_rows; ++j)
                    planes[1].bytes[t * planes[1].stride + j] =
                        truth[t * F::tile_rows + j] >> (F::width - 8);
            if constexpr (F::heads == 16)
                for (unsigned j = 0; j < F::tile_rows; ++j)
                    planes[2].bytes[t * planes[2].stride + j] =
                        truth[t * F::tile_rows + j] >> (F::width - 16);
        }
    }
    auto source() const {
        auto v = sp::view<F>::attach(count, {{{planes[0].bytes, planes[0].stride},
                                              {planes[1].bytes, planes[1].stride},
                                              {planes[2].bytes, planes[2].stride}}});
        IKEA2_CHECK(v);
        return *v;
    }
};
