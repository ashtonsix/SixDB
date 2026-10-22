#include <ikea/seriespack/detail/native/neon/read.h>
#include <cstdio>
#include <cstdlib>
#include <random>

// Independent bit-by-bit reference for the variant's transpose128: output byte j,
// bit b equals input byte b, bit j, in each 64-bit lane.
static std::uint64_t reference(std::uint64_t x) {
    std::uint64_t y = 0;
    for (unsigned b = 0; b < 8; ++b)
        for (unsigned j = 0; j < 8; ++j)
            y |= ((x >> (8 * b + j)) & 1) << (8 * j + b);
    return y;
}
int main() {
    namespace neon = ikea::seriespack::neon;
    std::mt19937_64 random(7);
    std::size_t checked = 0;
    auto check = [&](std::uint64_t a, std::uint64_t b) {
        const auto x = vcombine_u64(vcreate_u64(a), vcreate_u64(b));
        const auto y = vreinterpretq_u64_u8(neon::transpose128(x));
        if (vgetq_lane_u64(y, 0) != reference(a) || vgetq_lane_u64(y, 1) != reference(b)) {
            std::fprintf(stderr, "mismatch %016llx %016llx\n", (unsigned long long)a,
                         (unsigned long long)b);
            std::abort();
        }
        ++checked;
    };
    for (unsigned i = 0; i < 64; ++i)
        check(std::uint64_t{1} << i, ~(std::uint64_t{1} << i));
    for (unsigned i = 0; i < 1000000; ++i)
        check(random(), random());
    std::printf("%s: %zu transposes match the bitwise reference\n", NEON_LOCAL_TRANSPOSE_VARIANT,
                checked);
}
