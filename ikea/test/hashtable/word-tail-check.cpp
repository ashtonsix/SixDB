#include <ikea/hashtable/table.h>
#include <cstdio>
#include <random>
#include <sys/mman.h>
#include <unistd.h>

using namespace ikea::hashtable;
template <unsigned Bits> void check(std::uint8_t* end) {
    // A complete eight-row tile ends at an unreadable guard page. The oracle
    // describes the wire bits directly, independently of the pack readers.
    auto* tile = end - Bits;
    std::array<std::uint8_t, Bits> expected;
    std::mt19937_64 random(811 + Bits);
    for (unsigned repetition = 0; repetition < 100; ++repetition) {
        for (unsigned i = 0; i < Bits; ++i)
            tile[i] = expected[i] = std::uint8_t(random());
        for (unsigned lane = 0; lane < 8; ++lane) {
            const auto value = repetition == 0   ? word{0}
                               : repetition == 1 ? mask<Bits>
                                                 : random() & mask<Bits>;
            constexpr auto body = Bits / 8, tail = Bits % 8;
            for (unsigned byte_index = 0; byte_index < body; ++byte_index)
                expected[lane * body + byte_index] = std::uint8_t(value >> (tail + 8 * byte_index));
            for (unsigned bit = 0; bit < tail; ++bit) {
                auto& plane_byte = expected[8 * body + bit];
                plane_byte = static_cast<std::uint8_t>((plane_byte & ~(1u << lane)) |
                                                          (((value >> bit) & 1u) << lane));
            }
            set_point<Bits>(tile, lane, 8, value);
            require(std::equal(expected.begin(), expected.end(), tile),
                    "point wire/neighbor preservation");
        }
    }
}
int main() {
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto* memory = static_cast<std::uint8_t*>(
        mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    require(memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0,
            "point guard allocation");
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (check<I + 1>(memory + page), ...);
    }(std::make_index_sequence<64>{});
    require(munmap(memory, 2 * page) == 0, "point guard release");
    std::printf(
        "PASS: all 64 Local widths, all rows, independent wire oracle and exact guard-page bounds; BMI2=%u\n",
#ifdef __BMI2__
        1u
#else
        0u
#endif
    );
}
