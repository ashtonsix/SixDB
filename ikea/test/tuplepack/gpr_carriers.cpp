#include <cassert>
#include <ikea/tuplepack/author/execution.h>
#include <random>
#include <vector>
namespace tp = ikea::tuplepack;
#if defined(__aarch64__) || defined(__AVX2__)
namespace {
template <unsigned Rows> void check(std::mt19937_64 &random) {
    for (unsigned n = 0; n < 1000; ++n) {
        const auto value = random();
        const auto expanded = tp::native::expand_word(value);
        std::array<tp::byte, 64> bytes;
        tp::native::store_packet(bytes.data(), expanded);
        for (unsigned i = 0; i < 64; ++i)
            assert(bytes[i] == (i < 8 ? tp::byte(value >> (8 * i)) : 0));
        for (unsigned i = 8; i < 64; ++i)
            bytes[i] = tp::byte(random());
        assert(tp::native::compact_word(tp::native::load_packet(bytes.data())) == value);
    }
}
} // namespace
#endif
void carrier_check() {
#if defined(__aarch64__) || defined(__AVX2__)
    std::mt19937_64 random(3472);
    // The same logical projection and groups have identical positions at both
    // widths. Compare actual bound operations, including holes and sparse rows.
    const auto f = *tp::layout::make(
        2, std::array<tp::code, 3>{tp::code{0, 0, 8}, tp::code{1, 0, 4}, tp::code{1, 4, 4}});
    std::array<tp::byte, 4> storage{13, 0xba, 241, 0x5c};
    for (const auto map :
         {std::array<tp::byte, 3>{0, 1, 2}, std::array<tp::byte, 3>{0, tp::hole, 2}})
        for (const auto &groups :
             {std::vector<unsigned>{3}, std::vector<unsigned>{2, 1}, std::vector<unsigned>{1, 2}}) {
            auto gr = *tp::reader<8, 2>::make(f, map, groups);
            auto vr = *tp::reader<64, 2>::make(f, map, groups);
            auto gw = *tp::writer<8, 2>::make(f, map, groups);
            auto vw = *tp::writer<64, 2>::make(f, map, groups);
            for (unsigned active = 0; active < 4; ++active) {
                const auto saved = storage;
                const auto word = gr.get_unchecked(storage.data(), 2, active);
                const auto wide = tp::native::read_body<2>(
                    vr.controls(), [&](unsigned r) { return storage.data() + 2 * r; }, active, 2);
                assert(tp::native::compact_word(wide) == word);
                auto input = word ^ 0x0101010101010101ull;
                gw.set_unchecked(storage.data(), 2, input, active);
                const auto expected = storage;
                storage = saved;
                tp::native::write_body<2>(
                    vw.controls(), [&](unsigned r) { return storage.data() + 2 * r; },
                    tp::native::expand_word(input), active, 2);
                assert(storage == expected);
                storage = saved;
            }
        }
    check<1>(random);
    check<2>(random);
    check<4>(random);
    check<8>(random);
#endif
}
