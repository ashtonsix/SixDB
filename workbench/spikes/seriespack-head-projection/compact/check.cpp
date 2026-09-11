// Reuse the independent specification oracle, never native decoding as truth.
#define main seriespack_original_public_main
#include "ikea/test/seriespack/physical_operations.cpp"
#undef main
#include <sys/mman.h>
#include <unistd.h>

namespace {
struct guarded_bytes {
    std::byte* mapping;
    std::byte* data;
    std::size_t extent, mapped;
    guarded_bytes(std::size_t bytes, bool beginning) : extent(bytes) {
        const auto page = std::size_t(sysconf(_SC_PAGESIZE));
        mapped = ((bytes + page - 1) / page + 2) * page;
        mapping = static_cast<std::byte*>(mmap(nullptr, mapped, PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (mapping == MAP_FAILED) std::abort();
        if (bytes && mprotect(mapping + page, mapped - 2 * page, PROT_READ | PROT_WRITE)) std::abort();
        data = beginning ? mapping + page : mapping + mapped - page - bytes;
        if (bytes) std::memset(data, 0xcd, bytes);
    }
    ~guarded_bytes() { munmap(mapping, mapped); }
    std::span<std::byte> span() { return {data, extent}; }
};
std::size_t guard_cases = 0;
template<class U>
void guard(sp::description desc, std::size_t n, unsigned placement, bool beginning) {
    fixture oracle(desc, n, false);
    const auto T = oracle.values_per_tile, tiles = oracle.tiles;
    const std::array<std::size_t, 3> stride = {
        oracle.tile_bytes[0] + (placement == 3 ? 32 : 0),
        T + ((placement & 1) ? 13 : 0), T + ((placement & 2) ? 17 : 0)};
    std::array<std::size_t, 3> extent{};
    for (unsigned p = 0; p < 3; ++p)
        if (tiles && oracle.tile_bytes[p]) extent[p] = (tiles - 1) * stride[p] + oracle.tile_bytes[p];
    guarded_bytes input(n * sizeof(U), beginning), payload(extent[0], beginning),
        head0(extent[1], beginning), head1(extent[2], beginning);
    std::array actual = {payload.span(), head0.span(), head1.span()};
    auto expected = oracle.bytes;
    for (std::size_t i = 0; i < tiles * T; ++i) {
        U value = 0;
        if (i < n) {
            value = static_cast<U>(random_bits() & mask(desc.width));
            if (i % 17 == 0) value = static_cast<U>(mask(desc.width));
            if (i % 19 == 0) value = 0;
            std::memcpy(input.data + i * sizeof(U), &value, sizeof value);
        }
        oracle.oracle_set(expected, i, value);
    }
    std::array<std::vector<std::byte>, 3> expected_guard;
    for (unsigned p = 0; p < 3; ++p) {
        expected_guard[p].assign(extent[p], std::byte{0xcd});
        if (!oracle.tile_bytes[p]) continue;
        for (std::size_t tile = 0; tile < tiles; ++tile)
            std::copy_n(expected[p].begin() + oracle.base[p] + tile * oracle.stride[p],
                oracle.tile_bytes[p], expected_guard[p].begin() + tile * stride[p]);
    }
    auto attached = sp::mutable_view::attach(desc, n,
        {{actual[0], stride[0]}, {{{actual[1], stride[1]}, {actual[2], stride[2]}}}});
    if (!attached) fail("guard attach", desc, n);
    const sp::input_values values{std::span(reinterpret_cast<const U*>(input.data), n)};
    const auto compare = [&] {
        for (unsigned p = 0; p < 3; ++p)
            if (!std::equal(actual[p].begin(), actual[p].end(), expected_guard[p].begin()))
                fail("guard independent wire/head/slack/gap", desc, n, p);
    };
    if (!sp::encode(*attached, values, nullptr, execution)) fail("guard checked encode", desc, n);
    compare();
    for (auto bytes : actual) std::fill(bytes.begin(), bytes.end(), std::byte{0xcd});
    const auto bound = sp::bind_encoder(*attached, execution);
    if (!bound) fail("guard bind", desc, n);
    bound->encode(values); compare(); ++guard_cases;
}
void check_compact_description(sp::description desc) {
    for (std::size_t n = 0; n <= 33; ++n) for (bool gap : {false, true}) check_case(desc, n, gap);
    for (std::size_t n : {63, 64, 65, 127, 128, 129, 255, 256, 257})
        for (bool gap : {false, true}) check_case(desc, n, gap);
    for (std::size_t n = 0; n <= 257; ++n) {
        if (n > 33 && n != 63 && n != 64 && n != 65 && n != 127 && n != 128 && n != 129 && n < 255) continue;
        for (unsigned placement = 0; placement < 4; ++placement) for (bool beginning : {false, true}) {
            guard<std::uint64_t>(desc, n, placement, beginning);
            if (n <= 2 || n == 31 || n == 32 || n == 33 || n == 255 || n == 256 || n == 257) {
                guard<std::uint8_t>(desc, n, placement, beginning);
                guard<std::uint16_t>(desc, n, placement, beginning);
                guard<std::uint32_t>(desc, n, placement, beginning);
            }
        }
    }
}
}
int main(int argc, char** argv) {
    execution = sp::execution_target::avx2; target_name = "avx2";
    if (argc == 2 && std::strcmp(argv[1], "avx512") == 0) {
        execution = sp::execution_target::avx512; target_name = "avx512";
    }
    for (unsigned k = 16; k <= 64; ++k) {
        check_compact_description({k, 16, sp::geometry::local8});
        if (striped_width(k - 16)) check_compact_description({k, 16, sp::geometry::striped});
    }
    for (auto d : {sp::description{8, 8, sp::geometry::local8},
                   sp::description{17, 8, sp::geometry::local8},
                   sp::description{23, 8, sp::geometry::striped},
                   sp::description{56, 0, sp::geometry::local8}}) check_compact_description(d);
    std::printf("H16 compact %s: %zu independent public placements, %zu reads, %zu mutations; "
                "%zu checked/bound guarded encodes passed\n", target_name, placements, reads, writes, guard_cases);
}
