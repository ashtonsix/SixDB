#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <cassert>
#include <cstdio>

int main() {
    namespace tp = ikea::tuplepack;
    // Four byte-contained codes per physical tuple. Low bits remain outside
    // this map; the caller can give them a separate meaning and operation.
    const std::array<tp::code, 4> codes{{{0, 1, 7}, {1, 1, 7}, {2, 1, 7}, {3, 1, 7}}};
    const std::array<std::uint8_t, 4> map{0, 1, 2, 3};
    const auto format = tp::layout::make(4, codes);
    assert(format);
    const auto rp = tp::read_plan<8, 2>::make(*format, map);
    const auto wp = tp::write_plan<8, 2>::make(*format, map);
    assert(rp && wp);
    std::array<std::uint8_t, 12> bytes{3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25};
    auto view = tp::view::attach(*format, std::as_writable_bytes(std::span{bytes}), 3, 4);
    assert(view);
    const auto read = tp::bind_reader(*rp, *view);
    const auto write = tp::bind_writer(*wp, *view);
    assert(read && write);

    // Low four bytes belong to row 0; high four to row 1. No output buffer.
    const auto result = read->get(0);
    assert(result && *result == 0x0807060504030201ull);
    const std::uint32_t first = *result, second = *result >> 32;
    assert(first == 0x04030201 && second == 0x08070605);

    // The native adapter inlines the same prepared lowering into a consumer.
    // uint64_t also stays in a GPR across author::packet_chain<8> continuation hops.
    const auto nr = tp::author::native_reader(*read);
    const auto nw = tp::author::native_writer(*write);
    std::array<ikea::journal::entry, 3> entries;
    ikea::journal effects{entries};
    for (std::size_t row = 0; row < 3; row += 2) {
        const std::uint64_t active = row == 0 ? 3 : 1;
        assert(nr.admit(row, active));
        const auto value = nr.get_unchecked(row, active) ^ 0x0101010101010101ull;
        assert(nw.set(row, value, effects, active));
        // No suspension inside the operation. The owner may retain the plans,
        // view, effects and completed row frontier here, then publish separately.
    }
    for (unsigned i = 0; i < bytes.size(); ++i)
        assert(bytes[i] == (((i + 1) ^ 1) * 2 + 1));
    std::puts("TuplePack: two four-code rows in a GPR, native update and one-row tail passed");
}
