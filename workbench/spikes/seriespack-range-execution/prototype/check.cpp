#include "../regions.h"
#include <ikea/seriespack/detail/physical.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

namespace {
namespace sp = ikea::seriespack;
using U = std::uint64_t;
std::size_t placements = 0, queries = 0;

[[noreturn]] void fail(const char* what) { std::fprintf(stderr, "%s\n", what); std::abort(); }

// Each owned tile abuts a protected page in strided mode. Dense mode guards
// the exact physical prefix/suffix; both orientations are checked. The spare
// readable bytes are not claimed as source padding by the attached placement.
struct guarded_plane {
    std::byte *mapping, *data;
    std::size_t mapped, extent, stride, writable, page, tiles;
    bool gaps;
    guarded_plane(std::size_t B, std::size_t count, bool strided, bool suffix)
        : page(sysconf(_SC_PAGESIZE)), tiles(count), gaps(strided) {
        stride = gaps ? 2 * page : B;
        extent = (tiles - 1) * stride + B;
        writable = gaps ? page : (extent + page - 1) / page * page;
        mapped = gaps ? (2 * tiles + 1) * page : writable + 2 * page;
        mapping = static_cast<std::byte*>(mmap(nullptr, mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (mapping == MAP_FAILED) fail("mmap");
        if (gaps) {
            for (std::size_t tile = 0; tile != tiles; ++tile)
                if (mprotect(mapping + (2 * tile + 1) * page, page, PROT_READ | PROT_WRITE)) fail("mprotect");
            data = mapping + page + (suffix ? page - B : 0);
        } else {
            if (mprotect(mapping + page, writable, PROT_READ | PROT_WRITE)) fail("mprotect");
            data = mapping + page + (suffix ? writable - extent : 0);
        }
    }
    void read_only() {
        if (gaps) {
            for (std::size_t tile = 0; tile != tiles; ++tile)
                if (mprotect(mapping + (2 * tile + 1) * page, page, PROT_READ)) fail("mprotect read");
        } else if (mprotect(mapping + page, writable, PROT_READ)) fail("mprotect read");
    }
    ~guarded_plane() { munmap(mapping, mapped); }
};

U mixed(U x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

template<unsigned K, sp::geometry G>
void check(sp::execution_target target) {
    using F = sp::static_format<K, G>;
    constexpr auto T = F::payload::tile_values, B = F::payload::tile_bytes;
    constexpr U mask = ~U{0} >> (64 - K);
    const auto region = seriespack_measurement::static_materialized16(F::layout, target);
    if (!region.read || !region.raw_dense) fail("missing selected region");
    for (auto n : {16U, 17U, unsigned(T + 16), unsigned(2 * T + 17), unsigned(5 * T + 31)}) {
        const auto tiles = (n + T - 1) / T;
        std::vector<U> original(tiles * T, 0);
        for (std::size_t i = 0; i != n; ++i) {
            original[i] = mixed(i + 913) & mask;
            if (i % 19 == 0) original[i] = mask;
            if (i % 17 == 0) original[i] = U{1} << (i % K);
            if (i % 13 == 0) original[i] = 0;
        }
        for (bool gaps : {false, true}) for (bool suffix : {false, true}) {
            guarded_plane encoded(B, tiles, gaps, suffix), output(18 * sizeof(U), 1, false, suffix);
            // Construction is the separately wire-verified scalar encoder;
            // expected outputs remain the independent original values above.
            for (std::size_t tile = 0; tile != tiles; ++tile)
                sp::detail::encode_low_tile<K, G>(original.data() + tile * T,
                    reinterpret_cast<std::uint8_t*>(encoded.data + tile * encoded.stride));
            encoded.read_only();
            const auto source = sp::const_view::assume_valid(F::layout, n,
                {{{encoded.data, encoded.extent}, encoded.stride}, {}});
            auto* out = reinterpret_cast<U*>(output.data) + (suffix ? 2 : 0);
            for (std::size_t origin = 0; origin + 16 <= n; origin += 16) {
                const auto verify = [&] {
                    for (unsigned lane = 0; lane != 16; ++lane)
                        if (out[lane] != original[origin + lane]) {
                            std::fprintf(stderr, "K%u G%u origin%zu lane%u gaps%d suffix%d\n", K, unsigned(G), origin, lane, gaps, suffix);
                            fail("original-coordinate mismatch");
                        }
                    const auto* guard = reinterpret_cast<const U*>(output.data) + (suffix ? 0 : 16);
                    if (guard[0] != 0xd3d3d3d3d3d3d3d3ULL || guard[1] != 0xd3d3d3d3d3d3d3d3ULL)
                        fail("output footprint");
                    ++queries;
                };
                std::memset(output.data, 0xd3, output.extent);
                region.read(source, origin, out);
                verify();
                if (!gaps) {
                    std::memset(output.data, 0xd3, output.extent);
                    region.raw_dense(reinterpret_cast<const std::uint8_t*>(encoded.data), origin, out);
                    verify();
                }
            }
            ++placements;
        }
    }
}

void run(sp::execution_target target) {
    placements = queries = 0;
    sp::detail::static_for<7>([&](auto i) {
        check<i + 1, sp::geometry::local8>(target);
        check<i + 1, sp::geometry::striped>(target);
    });
    check<56, sp::geometry::local8>(target);
    check<10, sp::geometry::striped>(target);
    check<12, sp::geometry::striped>(target);
    check<14, sp::geometry::striped>(target);
    check<15, sp::geometry::striped>(target);
    check<20, sp::geometry::striped>(target);
    for (auto d : {sp::description{0, 0, sp::geometry::local8}, {1, 8, sp::geometry::local8},
                   {9, 0, sp::geometry::local8}, {56, 0, sp::geometry::striped}})
        if (seriespack_measurement::static_materialized16(d, target).read) fail("unsupported admission");
    std::printf("Admitted regions target%u: 20 descriptions, %zu protected placements, %zu original-coordinate queries passed\n",
        unsigned(target), placements, queries);
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--avx2") == 0) run(sp::execution_target::avx2);
    else if (argc == 2 && std::strcmp(argv[1], "--avx512") == 0) run(sp::execution_target::avx512);
    else if (argc == 2 && std::strcmp(argv[1], "--neon") == 0) run(sp::execution_target::neon);
    else fail("expected --neon, --avx2, or --avx512");
}
