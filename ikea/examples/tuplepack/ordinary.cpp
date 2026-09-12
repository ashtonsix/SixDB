#include <ikea/tuplepack.h>
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    namespace tp = ikea::tuplepack;
    // Two packed bytes: [seven-bit rank | flag] [spare five bits | tag].
    const std::array<tp::code, 3> codes{{{0, 0, 1}, {0, 1, 7}, {1, 0, 3}}};
    auto format = tp::layout::make(2, codes);
    assert(format);
    std::array<tp::byte, 16> bytes{};
    auto source = tp::view::bind(*format, bytes, 2, 8, 2);
    assert(source);
    auto construction = tp::constructor::make(*format);
    assert(construction);
    auto initialize = tp::bind_constructor(*construction, *source);
    assert(initialize);
    std::array<tp::construction_input, 2> input{};
    input[0][0] = 1;
    input[0][1] = 37;
    input[0][2] = 5;
    input[1][1] = 12;
    input[1][2] = 2;
    std::array<ikea::owner_write, 4> writes;
    ikea::source_write_journal effects{writes};
    assert(initialize->initialize(0, input, effects));
    const std::array<tp::byte, 4> map{1, tp::hole, 0, 2};
    auto reader = tp::reader<8>::make(*format, map);
    assert(reader);
    auto read = tp::bind_reader(*reader, *source);
    assert(read);
    auto value = read->get(0);
    assert(value);
    assert(*value == 0x05010025);
    // The same map repeats for each row; packet width is decoded bytes,
    // independent of the two-byte physical units and their eight-byte stride.
    auto pair_plan = tp::reader<8, 2>::make(*format, map);
    assert(pair_plan);
    auto pair = tp::bind_reader(*pair_plan, *source);
    assert(pair);
    auto pair_value = pair->get(0);
    assert(pair_value && *pair_value == 0x0200000c05010025ull);
    const std::array<tp::byte, 1> rank{1};
    auto writer = tp::writer<8>::make(*format, rank);
    assert(writer);
    auto update = tp::bind_writer(*writer, *source);
    assert(update);
    effects.used = 0;
    assert(update->set(0, 99, effects));
    assert(tp::byte(read->get_unchecked(0)) == 99);
    assert(effects.entries()[0].source == &*source && effects.entries()[0].bytes.offset == 2);
    const auto saved = bytes;
    assert(!update->set(1, 128, effects)); // too wide; no data/effect change
    assert(bytes == saved && effects.used == 1);
    std::puts("TuplePack ordinary: constructed, projected, replaced; one qualified byte effect");
}
