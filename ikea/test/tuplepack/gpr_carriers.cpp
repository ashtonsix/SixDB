#include <ikea/tuplepack/author/execution.h>
#include <cassert>
#include <random>
namespace tp = ikea::tuplepack;
#if defined(__aarch64__) || defined(__AVX2__)
namespace {
template <unsigned Rows> void check(std::mt19937_64& random) {
    for (unsigned n = 0; n < 1000; ++n) {
        const auto value = random();
        const auto expanded = tp::native::expand_word<Rows>(value);
        std::array<tp::byte, 64> bytes;
        tp::native::store_packet(bytes.data(), expanded);
        for (unsigned r = 0; r < Rows; ++r)
            for (unsigned i = 0; i < 64 / Rows; ++i)
                assert(bytes[r * (64 / Rows) + i] ==
                       (i < 8 / Rows ? tp::byte(value >> (8 * (r * (8 / Rows) + i))) : 0));
        // Nonzero discarded slots also establish that compacting masks them.
        for (unsigned r = 0; r < Rows; ++r)
            for (unsigned i = 8 / Rows; i < 64 / Rows; ++i)
                bytes[r * (64 / Rows) + i] = tp::byte(random());
        assert(tp::native::compact_word<Rows>(tp::native::load_packet(bytes.data())) == value);
    }
}
} // namespace
#endif
void carrier_check() {
#if defined(__aarch64__) || defined(__AVX2__)
    std::mt19937_64 random(3472);
    check<1>(random);
    check<2>(random);
    check<4>(random);
    check<8>(random);
#endif
}
