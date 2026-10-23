#include "probe.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace hb;
void need(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "handle-boundaries: %s\n", message);
        std::abort();
    }
}
word decoded(std::span<const std::byte> bytes, std::size_t row, std::size_t stride,
             std::span<const tp::code> codes, std::span<const std::uint8_t> map) {
    word result = 0;
    for (unsigned slot = 0; slot < map.size(); ++slot) {
        const auto code = codes[map[slot]];
        for (unsigned bit = 0; bit < code.width; ++bit)
            result |= word((std::to_integer<unsigned>(bytes[row * stride + code.offset]) >>
                            (code.shift + bit)) & 1) << (slot * 8 + bit);
    }
    return result;
}
void changed(std::span<std::byte> bytes, std::size_t row, std::size_t stride,
             std::span<const tp::code> codes, std::span<const std::uint8_t> map, word input) {
    for (unsigned slot = 0; slot < map.size(); ++slot) {
        const auto code = codes[map[slot]];
        for (unsigned bit = 0; bit < code.width; ++bit) {
            auto& target = bytes[row * stride + code.offset];
            const auto mask = 1u << (code.shift + bit);
            const auto value = (std::to_integer<unsigned>(target) & ~mask) |
                               (((input >> (slot * 8 + bit)) & 1) << (code.shift + bit));
            target = std::byte(value);
        }
    }
}
void point_checks(unsigned map_slots) {
    const std::array<tp::code, 4> codes{{{0, 0, 1}, {0, 1, 7}, {1, 0, 3}, {1, 3, 5}}};
    const std::array<std::uint8_t, 4> full_map{0, 2, 1, 3};
    const auto map = std::span(full_map).first(map_slots);
    const auto layout = *tp::layout::make(2, codes);
    const auto read_plan = *tp::read_plan<8>::make(layout, map);
    const auto write_plan = *tp::write_plan<8>::make(layout, map);
    std::array<std::byte, 64 * 16> bytes;
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = std::byte((i * 37 + 0x5a) & 255);
    const auto destination = *tp::view::attach(layout, bytes, 64, 16);
    const auto source = destination.as_const();
    auto read = *make_reader(read_plan, source);
    const auto write = *make_writer(write_plan, destination);
    const auto wanted = decoded(bytes, 7, 16, codes, map);
    need(raw_roundtrip(read, 7) == wanted, "raw reader handoff");
    need(checked_roundtrip(read_plan, source, 7) == wanted, "checked reader factory handoff");
    need(pass_native_reader(native_reader(read)).get_unchecked(7) == wanted,
         "native pointer-pair handoff");
    std::array<reader, 7> slots{read, read, read, read, read, read, read};
    need(stored_roundtrip(slots, 3, read, 7) == wanted, "stored and reloaded reader");
    const auto alternate = *tp::const_view::attach(layout, std::span<const std::byte>(bytes).subspan(16),
                                                   63, 16);
    need(bool(rebind_reader(slots[3], read_plan, alternate)), "same law/new placement rebind");
    need(raw_roundtrip(slots[3], 0) == decoded(bytes, 1, 16, codes, map), "rebind changes source");
    need(raw_roundtrip(slots[0], 0) == decoded(bytes, 0, 16, codes, map), "old binding remains old");
    const auto one_byte = *tp::layout::make(1, std::array<tp::code, 1>{{{0, 0, 8}}});
    const auto wrong = *tp::const_view::attach(one_byte, bytes, 64, 16);
    need(!make_reader(read_plan, wrong), "incompatible checked factory");
    need(!rebind_reader(slots[3], read_plan, wrong), "incompatible update refuses");
    need(raw_roundtrip(slots[3], 0) == decoded(bytes, 1, 16, codes, map),
         "failed update preserves previous binding");
    need(bool(rebind_reader(slots[3], read_plan, source)), "rebind to original placement");
    std::array<word, 2> tail{0xdeadbeef, 0xdeadbeef};
    need(!read.read(63, std::span(tail)), "short output range refuses");
    need(tail == std::array<word, 2>{0xdeadbeef, 0xdeadbeef}, "range refusal preserves output");
    const std::array<word, 1> selected{1};
    need(bool(read.read(62, std::span(tail), tp::selection::bits(62, selected))), "selected tail read");
    need(tail[0] == decoded(bytes, 62, 16, codes, map) && tail[1] == 0,
         "tail preserves original coordinates and zeroes inactive packet");

    word a = 1, b = 2, c = 3, d = 4, e = 5, f = 6;
    a += decoded(bytes, 0, 16, codes, map); b ^= decoded(bytes, 1, 16, codes, map);
    c += decoded(bytes, 2, 16, codes, map); d ^= decoded(bytes, 3, 16, codes, map);
    e += decoded(bytes, 4, 16, codes, map); f ^= decoded(bytes, 5, 16, codes, map);
    a = std::rotl(a, 7) + decoded(bytes, 6, 16, codes, map);
    const auto crowded = a ^ std::rotl(b, 11) ^ std::rotl(c, 19) ^ std::rotl(d, 23) ^ std::rotl(e, 31) ^ f;
    need(crowded_readers(slots, 0, 1, 2, 3, 4, 5, 6) == crowded, "crowded actual reads");

    std::array<ikea::journal::entry, 4> events;
    ikea::journal effects{events};
    auto expected = bytes;
    constexpr word replacement = 0x01020301;
    changed(expected, 7, 16, codes, map, replacement);
    need(bool(write_roundtrip(write, 7, replacement, effects)), "raw writer handoff command");
    need(bytes == expected, "wire byte oracle includes all untouched neighbours and stride gaps");
    need(effects.used != 0, "real effects survive handoff");
    for (const auto& event : effects.entries())
        need(event.source == &destination && event.plane == 0 && event.size && event.offset >= 7 * 16 &&
                 event.offset + event.size <= 7 * 16 + 2,
             "effect source/plane/physical bound");
    effects.used = 0; // Whole command resolved before its source can be rebound.
    need(!write_roundtrip(write, 7, 2, effects), "out-of-width input refuses");
    need(bytes == expected && effects.used == 0, "refusal changes neither bytes nor effects");
}

void predicate_checks(unsigned width, unsigned stride) {
    const unsigned shift = width == 8 ? 0 : 2;
    const unsigned limit = (1u << width) - 1;
    const std::array<tp::code, 1> codes{{{0, std::uint8_t(shift), std::uint8_t(width)}}};
    const auto layout = *tp::layout::make(1, codes);
    const std::array<std::uint8_t, 1> map{0};
    const auto plan = *filter_plan::make(layout, map);
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto* allocation = static_cast<std::byte*>(mmap(nullptr, 3 * page, PROT_NONE,
                                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    need(allocation != MAP_FAILED, "guard allocation");
    need(mprotect(allocation + page, page, PROT_READ | PROT_WRITE) == 0, "guard middle page");
    const auto bytes = 17 * stride + 1;
    auto* data = allocation + 2 * page - bytes;
    for (unsigned i = 0; i < bytes; ++i)
        data[i] = std::byte((i * 37 + 0xa5) & 255);
    const auto source = *tp::const_view::attach(layout, {data, bytes}, 18, stride);
    const auto operation = *make_filter(plan, source);
    auto oracle = [&](unsigned first, word rhs, word active) {
        word wanted = 0;
        for (unsigned i = 0; i < 16; ++i)
            if ((active >> i) & 1) {
                const auto value = (std::to_integer<unsigned>(data[(first + i) * stride]) >> shift) & limit;
                if (value >= rhs)
                    wanted |= word(1) << i;
            }
        return wanted;
    };
    for (const word active : {word(0), word(0x5555), word(0xffff)})
        for (const word rhs : {word(0), word(12), word(31), word(32), ~word(0)}) {
            const auto wanted = oracle(0, rhs, active);
            need(consume_filter(operation, 0, rhs, active) == wanted, "predicate opaque handoff");
            need(checked_filter_command(plan, source, 0, rhs, active) == wanted,
                 "checked predicate bind and whole operation");
        }
    need(checked_filter_command(plan, source, 16, 12, 3) == oracle(16, 12, 3),
         "exact two-row tail at protected boundary");
    need(!checked_filter_command(plan, source, 16, 12, 0xffff), "active out-of-range tail rejects");
    std::array<filter, 7> filters{operation, operation, operation, operation, operation, operation, operation};
    const auto value = oracle(0, 12, 0xb6db);
    const auto wanted = (std::rotl(word(1) + value, 7) + value) ^ std::rotl(word(2) ^ value, 11) ^
                        std::rotl(word(3) + value, 19) ^ std::rotl(word(4) ^ value, 23) ^
                        std::rotl(word(5) + value, 31) ^ (word(6) ^ value);
    need(crowded_filters(filters, 0, 12, 0xb6db, 1, 2, 3, 4, 5, 6) == wanted,
         "crowded encoded predicates");
    const auto holes = *filter_plan::make(layout, std::array<std::uint8_t, 1>{255});
    need(!make_filter(holes, source), "one-code hole plan refuses before payload access");
    need(mprotect(allocation + page, page, PROT_NONE) == 0, "protect all source storage");
    need(consume_filter(operation, 18, 12, 0) == 0, "empty predicate forms no payload access");
    need(munmap(allocation, 3 * page) == 0, "guard release");
}
} // namespace

int main() {
    for (unsigned slots : {1u, 2u, 4u})
        point_checks(slots);
    for (unsigned width : {1u, 5u, 8u})
        for (unsigned stride : {1u, 7u})
            predicate_checks(width, stride);
    std::puts("handle-boundaries: raw/checked/stored/crowded commands, rebind refusal, wire/effects, exact-tail and protected-none checks passed");
}
