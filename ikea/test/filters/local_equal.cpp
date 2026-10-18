#include <ikea/seriespack/author/local_equal.h>
#include <cassert>
#include <random>

template <unsigned K> void check_local_equal(std::mt19937_64& rng) {
    for (unsigned trial = 0; trial < 1024; ++trial) {
        const auto packed = rng();
        for (unsigned query = 0; query < (1u << K); ++query) {
            unsigned expected = 0;
            for (unsigned row = 0; row < 8; ++row) {
                unsigned value = 0;
                for (unsigned bit = 0; bit < K; ++bit)
                    value |= ((packed >> (8 * bit + row)) & 1) << bit;
                expected |= unsigned(value == query) << row;
            }
            const ikea::seriespack::composition::local_equal8<K> match(query);
            assert(match(packed) == expected);
        }
    }
}
void check_local_equal() {
    std::mt19937_64 rng(0xa190fe);
    check_local_equal<1>(rng); check_local_equal<2>(rng); check_local_equal<3>(rng);
    check_local_equal<4>(rng); check_local_equal<5>(rng); check_local_equal<6>(rng);
    check_local_equal<7>(rng);
}
