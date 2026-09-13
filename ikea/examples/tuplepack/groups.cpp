#include <cassert>
#include <cstdio>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>

int main() {
    namespace tp = ikea::tuplepack;
    // The caller treats A+B as a 12-bit integer and C as a separate nibble.
    // TuplePack only sees three byte-contained codes and two packet groups.
    const auto format = *tp::layout::make(
        2, std::array<tp::code, 3>{tp::code{0, 0, 8}, tp::code{1, 0, 4}, tp::code{1, 4, 4}});
    const std::array<tp::byte, 3> map{0, 1, 2};
    const std::array<unsigned, 2> groups{2, 1};
    auto rp = *tp::reader<64, 4>::make(format, map, groups);
    // Only A+B are replaced. Their single group fills an eight-byte packet.
    auto wp = *tp::writer<8, 4>::make(format, std::array<tp::byte, 2>{0, 1});
    std::array<tp::byte, 8> bytes{0xff, 0xa0, 0xff, 0xbf, 0x20, 0xc1, 0x80, 0xd2};
    const auto saved = bytes;
    auto view = *tp::view::bind(format, bytes, 4, 2);
    auto read = *tp::bind_reader(rp, view);
    auto write = *tp::bind_writer(wp, view);
    // A0 B0 A1 B1 A2 B2 A3 B3 | C0 C1 C2 C3 | trailing zeros.
    const auto value = *read.get(0);
    for (unsigned r = 0; r < 4; ++r) {
        assert(value[2 * r] == bytes[2 * r]);
        assert(value[2 * r + 1] == (bytes[2 * r + 1] & 15));
        assert(value[8 + r] == (bytes[2 * r + 1] >> 4));
    }
#if defined(__aarch64__) || defined(__AVX2__)
    const auto native = tp::native_reader(read);
    const auto decoded = native.get_unchecked(0);
    // Four caller-interpreted 16-bit lanes fit in a GPR. Carry cannot cross
    // lanes because admitted AB values are at most 4095. No payload buffer.
    const auto integers = tp::native::compact_word(decoded);
    const auto next = (integers + 0x0001000100010001ull) & 0x0fff0fff0fff0fffull;
    std::array<ikea::owner_write, 4> records;
    ikea::source_write_journal effects{records};
    assert(tp::native_writer(write).set(0, next, effects));
    for (unsigned r = 0; r < 4; ++r) {
        const auto old = saved[2 * r] | ((saved[2 * r + 1] & 15) << 8);
        assert(bytes[2 * r] == tp::byte(old + 1));
        assert(bytes[2 * r + 1] == ((saved[2 * r + 1] & 0xf0) | (((old + 1) >> 8) & 15)));
    }
    // Effects name issued byte coverage in view coordinates. The owner handles
    // visibility and publication separately, as for every TuplePack mutation.
    assert(effects.used);
#else
    (void)saved;
    (void)write;
#endif
    std::puts(
        "TuplePack: grouped 12-bit values, native/GPR composition and preserved neighbors passed");
}
