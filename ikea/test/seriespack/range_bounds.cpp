#include <ikea/seriespack/operations.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

namespace {
namespace sp = ikea::seriespack;
using value_type = std::uint64_t;
sp::execution_target target = sp::execution_target::automatic;
std::size_t cases = 0;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "SeriesPack range bounds: %s\n", message);
    std::abort();
}

struct plane {
    std::byte* mapping = nullptr;
    std::byte* data = nullptr;
    std::size_t mapping_bytes = 0, bytes = 0, stride = 0;

    plane(std::size_t tile_bytes, std::size_t tiles, bool gaps) {
        if (tile_bytes == 0) return;
        const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        stride = gaps ? 2 * page : tile_bytes;
        bytes = (tiles - 1) * stride + tile_bytes;
        const auto writable = gaps ? page : ((bytes + page - 1) / page) * page;
        mapping_bytes = gaps ? tiles * 2 * page : writable + 2 * page;
        mapping = static_cast<std::byte*>(mmap(nullptr, mapping_bytes, PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (mapping == MAP_FAILED) fail("mmap");
        if (gaps) {
            for (std::size_t tile = 0; tile != tiles; ++tile)
                if (mprotect(mapping + tile * 2 * page, page, PROT_READ | PROT_WRITE)) fail("mprotect");
            data = mapping + page - tile_bytes;
        } else {
            if (mprotect(mapping + page, writable, PROT_READ | PROT_WRITE)) fail("mprotect");
            data = mapping + page + writable - bytes;
        }
    }
    ~plane() { if (mapping) munmap(mapping, mapping_bytes); }
    std::span<std::byte> span() { return {data, bytes}; }
};

value_type mixed(value_type x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

template<class U>
void check_carrier(const sp::bound_reader& reader, const std::vector<value_type>& original) {
    if (8 * sizeof(U) < reader.source().layout().width) return;
    constexpr std::array<unsigned, 13> counts{0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33};
    plane destination(34 * sizeof(U), 1, false);
    const auto n = original.size();
    for (std::size_t first = 0; first <= n; ++first) {
        for (const auto count : counts) {
            if (count > n - first) continue;
            auto* out = reinterpret_cast<U*>(destination.data + destination.bytes - count * sizeof(U));
            out[-1] = U(0xd3);
            reader.decode({first, first + count}, sp::output_values{std::span(out, count)});
            if (out[-1] != U(0xd3)) fail("output prefix changed");
            for (unsigned lane = 0; lane != count; ++lane) {
                if (out[lane] != original[first + lane]) {
                    const auto d = reader.source().layout();
                    std::fprintf(stderr, "k%u h%u geometry%u carrier%zu n%zu first%zu count%u lane%u\n",
                        d.width, d.head_bits, unsigned(d.storage), 8 * sizeof(U), n, first, count, lane);
                    fail("decoded value");
                }
            }
            ++cases;
        }
    }
}

void check(sp::description layout, std::size_t n, unsigned gaps) {
    const auto T = sp::tile_values(layout), B = sp::tile_bytes(layout), tiles = (n + T - 1) / T;
    // Mixed strides admit a payload pair while keeping the two head planes
    // independently strided. A full gap mode guards every physical tile.
    plane payload(B, tiles, gaps == 2);
    plane head0(layout.head_bits >= 8 ? T : 0, tiles, gaps != 0);
    plane head1(layout.head_bits == 16 ? T : 0, tiles, gaps != 0);
    auto destination = sp::mutable_view::attach(layout, n,
        {{payload.span(), payload.stride}, {{{head0.span(), head0.stride}, {head1.span(), head1.stride}}}});
    if (!destination) fail("placement admission");
    std::vector<value_type> original(n);
    const auto mask = layout.width == 64 ? ~value_type{0} : (value_type{1} << layout.width) - 1;
    for (std::size_t i = 0; i != n; ++i) original[i] = mixed(i + 83) & mask;
    // Construction and the original values are independent of the native
    // reader. The separate physical wire oracle verifies the scalar wire map.
    if (!sp::encode(*destination, sp::input_values{std::span(original)}, nullptr, sp::execution_target::scalar))
        fail("scalar construction");
    auto reader = sp::bind_reader(destination->as_const(), target);
    if (!reader) fail("target unavailable");
    check_carrier<std::uint8_t>(*reader, original);
    check_carrier<std::uint16_t>(*reader, original);
    check_carrier<std::uint32_t>(*reader, original);
    check_carrier<std::uint64_t>(*reader, original);
}
}

void run_target() {
    cases = 0;
    unsigned descriptions = 0;
    for (unsigned k = 1; k <= 64; ++k) for (unsigned h : {0u, 8u, 16u})
        for (auto g : {sp::geometry::local8, sp::geometry::striped}) {
            const sp::description d{k, h, g};
            if (!sp::validate(d)) continue;
            ++descriptions;
            const auto T = sp::tile_values(d);
            std::vector<std::size_t> sizes{1, 7, 8, 9, 15, 16, 17, T - 1, T, T + 1, 2 * T + 17};
            std::sort(sizes.begin(), sizes.end());
            sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
            for (auto n : sizes) for (unsigned gaps = 0; gaps != 3; ++gaps) check(d, n, gaps);
        }
    const char* name = target == sp::execution_target::scalar ? "scalar" :
        target == sp::execution_target::avx2 ? "avx2" :
        target == sp::execution_target::avx512 ? "avx512" : "neon";
    std::printf("SeriesPack range bounds (%s): %u descriptions, %zu exact guarded ranges passed\n", name, descriptions, cases);
}

int main(int argc, char** argv) {
    const bool all = argc == 2 && std::strcmp(argv[1], "--all-available") == 0;
    if (argc == 3 && std::strcmp(argv[1], "--target") == 0) {
        if (std::strcmp(argv[2], "neon") == 0) target = sp::execution_target::neon;
        else if (std::strcmp(argv[2], "avx2") == 0) target = sp::execution_target::avx2;
        else if (std::strcmp(argv[2], "avx512") == 0) target = sp::execution_target::avx512;
        else if (std::strcmp(argv[2], "scalar") == 0) target = sp::execution_target::scalar;
        else fail("unknown target");
    } else if (argc != 1 && !all) fail("usage: range_bounds [--all-available | --target scalar|neon|avx2|avx512]");
    const auto empty = sp::const_view::assume_valid({1, 0, sp::geometry::local8}, 0, {{{}, 1}, {}});
    if (!all) {
        const auto reader = sp::bind_reader(empty, target);
        if (!reader) fail("requested target unavailable");
        target = reader->target();
        run_target();
        return 0;
    }
    for (auto candidate : {sp::execution_target::scalar, sp::execution_target::neon,
                           sp::execution_target::avx2, sp::execution_target::avx512}) {
        const auto reader = sp::bind_reader(empty, candidate);
        if (!reader) {
            if (reader.error() != sp::error::unsupported) fail("target admission");
            continue;
        }
        target = reader->target();
        run_target();
    }
}
