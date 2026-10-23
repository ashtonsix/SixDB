#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <array>
#include <cassert>
#include <cstdio>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

namespace tp = ikea::tuplepack;
using Access = tp::author::read_access;

namespace {
void poison([[maybe_unused]] void* p, [[maybe_unused]] std::size_t bytes) {
#if __has_feature(address_sanitizer)
    __asan_poison_memory_region(p, bytes);
#endif
}
void unpoison([[maybe_unused]] void* p, [[maybe_unused]] std::size_t bytes) {
#if __has_feature(address_sanitizer)
    __asan_unpoison_memory_region(p, bytes);
#endif
}

template <unsigned Rows> void access_contract() {
    constexpr unsigned grain = 64 / Rows;
    constexpr std::uint64_t active = Rows == 64 ? 0xff000000000000ffull : 0xf000000full;
    std::array<tp::code, grain> codes;
    std::array<std::uint8_t, grain> map;
    for (unsigned i = 0; i < grain; ++i) {
        codes[i] = {std::uint8_t(i), std::uint8_t(i + 1), 3};
        map[i] = i;
    }
    const auto layout = *tp::layout::make(grain, codes);
    const auto rp = *tp::read_plan<64, Rows>::make(layout, map);
    const auto wp = *tp::write_plan<64, Rows>::make(layout, map);
    alignas(64) std::array<std::byte, 96> allocation;
    allocation.fill(std::byte{0x85});
    auto data = std::span(allocation).subspan(16, 64);
    auto view = *tp::view::attach(layout, data, Rows, grain);
    const auto read = *tp::bind_reader(rp, view);
    const auto write = *tp::bind_writer(wp, view);
    const auto strict_read = tp::author::native_reader(read);
    const auto strict_write = tp::author::native_writer(write);
    const tp::author::native_reader<64, std::byte, Rows, Access::common_window> window_read(read);
    const tp::author::native_writer<64, Rows, Access::common_window> window_write(write);
    static_assert(sizeof(strict_read) == 2 * sizeof(void*) &&
                  sizeof(window_read) == sizeof(strict_read) &&
                  sizeof(window_write) == sizeof(strict_write));
    std::array<std::uint8_t, 64> wanted{}, actual{}, replacement{};
    for (unsigned row = 0; row < Rows; ++row)
        for (unsigned slot = 0; slot < grain; ++slot)
            if (active & (std::uint64_t{1} << row)) {
                wanted[row * grain + slot] = (0x85 >> (slot + 1)) & 7;
                replacement[row * grain + slot] = wanted[row * grain + slot] ^ 1;
            }
    poison(allocation.data(), 16);
    poison(allocation.data() + 80, 16);
    poison(data.data() + 8, 48); // Interior inactive units, not allocation padding.
    assert(*read.get(0, active) == wanted);
    tp::native::store_packet(actual.data(), strict_read.get_unchecked(0, active));
    assert(actual == wanted);
    std::array<const std::uint8_t*, Rows> addresses{};
    for (unsigned row = 0; row < Rows; ++row)
        if (active & (std::uint64_t{1} << row))
            addresses[row] = ikea::to_u8(data.data() + row * grain);
    auto address = [&](unsigned row) {
        assert(active & (std::uint64_t{1} << row));
        return addresses[row];
    };
    tp::native::store_packet(actual.data(), tp::native::read_body<Rows>(rp.controls(), address, active));
    assert(actual == wanted); // No stride proof; inactive addresses are null.
    tp::native::store_packet(actual.data(), tp::native::read_body<Rows, Access::common_window>(
        rp.controls(), address, active));
    assert(actual == wanted); // A grant alone does not establish stride geometry.
    if constexpr (Rows == 64) {
        const auto predicate = *tp::bind_predicate(read);
        assert(predicate.template filter<ikea::comparison::equal>(0, 2, active) == active);
    }
    std::array<ikea::journal::entry, Rows> entries;
    ikea::journal effects{entries};
    assert(strict_write.set(0, tp::native::load_packet(replacement.data()), effects, active));
    effects.used = 0;
    assert(write.set(0, wanted, effects, active));
    // An empty operation may start at end, even with all payload inaccessible.
    poison(data.data(), 64);
    assert(*read.get(Rows, 0) == tp::packet<64>{});
    tp::native::store_packet(actual.data(), window_read.get_unchecked(Rows, 0));
    assert(actual == tp::packet<64>{});
    const auto used = effects.used;
    assert(window_write.set(Rows, tp::native::load_packet(replacement.data()), effects, 0));
    assert(effects.used == used);
    unpoison(data.data(), 64);

    // This owner now grants the complete initialized common window. Stores and
    // emitted effects still exclude the inactive interior.
    tp::native::store_packet(actual.data(), window_read.get_unchecked(0, active));
    assert(actual == wanted);
    effects.used = 0;
    assert(window_write.set(0, tp::native::load_packet(replacement.data()), effects, active));
    for (unsigned byte = 0; byte < 64; ++byte) {
        const bool selected = active & (std::uint64_t{1} << (byte / grain));
        const auto want = selected ? (0x85 ^ (1u << (byte % grain + 1))) : 0x85;
        assert(std::to_integer<unsigned>(data[byte]) == want);
        bool covered = false;
        for (unsigned i = 0; i < effects.used; ++i) {
            const auto& e = entries[i];
            assert(e.source == &view && e.plane == 0 && e.offset + e.size <= 64);
            covered |= e.offset <= byte && byte < e.offset + e.size;
        }
        assert(covered == selected);
    }
    // Granted windows with a first/last active row inside the allocation must
    // not touch the preceding/following inactive units either.
    poison(data.data(), 8);
    constexpr auto end = 40 + 8 * grain;
    poison(data.data() + end, 64 - end);
    constexpr auto middle = (std::uint64_t{0xff} << (8 / grain)) |
                            (std::uint64_t{0xff} << (40 / grain));
    std::array<std::uint8_t, 64> middle_wanted{};
    for (unsigned row = 0; row < Rows; ++row)
        if (middle & (std::uint64_t{1} << row))
            for (unsigned slot = 0; slot < grain; ++slot)
                middle_wanted[row * grain + slot] = (0x85 >> (slot + 1)) & 7;
    tp::native::store_packet(actual.data(), window_read.get_unchecked(0, middle));
    assert(actual == middle_wanted);
    effects.used = 0;
    assert(window_write.set(0, tp::native::load_packet(middle_wanted.data()), effects, middle));
    unpoison(allocation.data(), allocation.size());
}

void gpr_contract() {
    const auto layout = *tp::layout::make(1, std::array<tp::code, 1>{{{0, 1, 3}}});
    const auto rp = *tp::read_plan<8, 8>::make(layout, std::array<std::uint8_t, 1>{0});
    const auto wp = *tp::write_plan<8, 8>::make(layout, std::array<std::uint8_t, 1>{0});
    std::array<std::byte, 8> data;
    data.fill(std::byte{0x85});
    auto view = *tp::view::attach(layout, data, 8, 1);
    const auto read = *tp::bind_reader(rp, view);
    const auto write = *tp::bind_writer(wp, view);
    const tp::author::native_reader<8, std::byte, 8, Access::common_window> nr(read);
    const tp::author::native_writer<8, 8, Access::common_window> nw(write);
    const auto value = nr.get_unchecked(0, 0x81);
    assert(value == 0x0200000000000002ull);
    std::array<ikea::journal::entry, 2> entries;
    ikea::journal effects{entries};
    assert(nw.set(0, value, effects, 0x81));
    for (const auto byte : data)
        assert(byte == std::byte{0x85});
}
} // namespace

int main() {
    access_contract<32>();
    access_contract<64>();
    gpr_contract();
    std::puts("TuplePack access: strict inactive interiors, explicit common windows, exact hulls, effects and GPR passed"
#if __has_feature(address_sanitizer)
              " (ASan poisoning enabled)"
#endif
    );
}
