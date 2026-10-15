#include "support.h"
void check_widths_1();
void check_widths_9();
void check_widths_17();
void check_widths_25();
void check_widths_33();
void check_widths_41();
void check_widths_49();
void check_widths_57();
void check_tuple_filters();
void guards() {
#if defined(__aarch64__) || defined(__AVX2__)
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto* memory = static_cast<std::uint8_t*>(
        mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(memory != MAP_FAILED);
    assert(mprotect(memory + page, page, PROT_READ | PROT_WRITE) == 0);
    for (unsigned i = 0; i < 64; ++i)
        memory[page * 2 - 64 + i] = i * 7;
    // Tight full-window fast paths end exactly at an inaccessible page.
    auto layout = *tp::layout::make(1, std::array<tp::code, 1>{{{0, 3, 5}}});
    auto source = *tp::view::bind(layout, {memory + page * 2 - 64, 64}, 64, 1);
    auto plan = *tp::reader<64, 64>::make(layout, std::array<tp::byte, 1>{0});
    auto read = *tp::bind_reader(plan, source);
    auto filter = *tp::bind_filter(read);
    auto bits = filter.filter<C::less>(0, 15);
    assert(bits);
    std::uint64_t wanted = 0;
    for (unsigned i = 0; i < 64; ++i)
        if (source.row_unchecked(i)[0] >> 3 < 15)
            wanted |= std::uint64_t{1} << i;
    assert(*bits == wanted);
    // Only the first row exists in a wide sparse packet. Inactive rows must
    // never access the next page, even when comparing against zero.
    auto short_source = *tp::view::bind(layout, {memory + page * 2 - 1, 1}, 1, 1);
    auto short_read = *tp::bind_reader(plan, short_source);
    auto short_filter = *tp::bind_filter(short_read);
    assert(short_filter.filter<C::equal>(0, 0, 1) == unsigned(memory[page * 2 - 1] >> 3 == 0));
    assert(short_filter.filter<C::equal>(1, 0, 0) == 0);
    auto dense = sp::dense_source<sp::format<7>>{memory + page * 2 - 14, 16};
    auto empty = sp::dense_source<sp::format<7>>{memory + page * 2, 16};
    assert(sp::filter<C::equal>(empty, 0, 16, 0, 0) == 0);
    assert(sp::filter<C::greater_equal>(dense, 0, 16, 0) == 65535);
    assert(munmap(memory, page * 3) == 0);
#endif
}

void substitution() {
    using F = sp::format<12>;
    using R = sp::format<4>;
    alignas(64) std::array<std::uint8_t, 128> parent{}, child{};
    auto p = *sp::view<F, std::uint8_t>::attach(64, {{{parent, F::tile_bytes}, {}, {}}});
    auto c = *sp::view<R, std::uint8_t>::attach(64, {{{child, R::tile_bytes}, {}, {}}});
    for (unsigned i = 0; i < 64; ++i) {
        sp::set_unchecked(p, i, i * 37);
        sp::set_unchecked(c, i, i % 16);
    }
    auto original = sp::composition::describe(p);
    auto expression = sp::composition::with_payload(
        original, sp::composition::with_tail(original.payload, sp::composition::describe(c)));
    auto filter = *sp::composition::prepare(expression, 64);
    std::uint64_t expected = 0;
    for (unsigned i = 0; i < 64; ++i)
        if ((((i * 37) & ~15u) | i % 16) < 1000)
            expected |= std::uint64_t{1} << i;
    assert(filter.filter<C::less>(0, 64, 1000) == expected);
}
int main() {
    check_widths_1();
    check_widths_9();
    check_widths_17();
    check_widths_25();
    check_widths_33();
    check_widths_41();
    check_widths_49();
    check_widths_57();
    check_tuple_filters();
    [&]<C... Ops>(operations<Ops...>) {
        (tuple_case<8, 1, Ops>(), ...);
        (tuple_case<8, 8, Ops>(), ...);
#if defined(__aarch64__) || defined(__AVX2__)
        (tuple_case<64, 1, Ops>(), ...);
        (tuple_case<64, 16, Ops>(), ...);
        (tuple_case<64, 64, Ops>(), ...);
#endif
    }(operations<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
    guards();
    substitution();
    puts("filters: unsigned boundaries, layouts, selections and projection checks passed");
}
