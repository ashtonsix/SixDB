#include "support.h"
#include <ikea/seriespack/author/store.h>
void check_widths_1();
void check_widths_9();
void check_widths_17();
void check_widths_25();
void check_widths_33();
void check_widths_41();
void check_widths_49();
void check_widths_57();
void check_tuple_filters();
void check_guarded_filters();
void check_filter_masks();
void check_filter_tiles();
void check_authored_filters();
void check_local_equal();
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
    auto source = *tp::view::attach(layout, std::as_writable_bytes(std::span{memory + page * 2 - 64, 64}), 64, 1);
    auto plan = *tp::read_plan<64, 64>::make(layout, std::array<std::uint8_t, 1>{0});
    auto read = *tp::bind_reader(plan, source);
    auto filter = *tp::bind_predicate(read);
    auto bits = filter.filter<C::less>(0, 15);
    assert(bits);
    std::uint64_t wanted = 0;
    for (unsigned i = 0; i < 64; ++i)
        if (std::to_integer<unsigned>(source.row_unchecked(i)[0]) >> 3 < 15)
            wanted |= std::uint64_t{1} << i;
    assert(*bits == wanted);
    // Only the first row exists in a wide sparse packet. Inactive rows must
    // never access the next page, even when comparing against zero.
    auto short_source = *tp::view::attach(layout, std::as_writable_bytes(std::span{memory + page * 2 - 1, 1}), 1, 1);
    auto short_read = *tp::bind_reader(plan, short_source);
    auto short_filter = *tp::bind_predicate(short_read);
    assert(short_filter.filter<C::equal>(0, 0, 1) == unsigned(memory[page * 2 - 1] >> 3 == 0));
    assert(short_filter.filter<C::equal>(1, 0, 0) == 0);
    auto dense = sp::dense_source<sp::layout<7>>{reinterpret_cast<const std::byte*>(memory + page * 2 - 14), 16};
    auto empty = sp::dense_source<sp::layout<7>>{reinterpret_cast<const std::byte*>(memory + page * 2), 16};
    assert(sp::filter<C::equal>(empty, 0, 16, 0, 0) == 0);
    assert(sp::indexof(empty, 0, 16, 0, 0) == 16);
    assert(sp::indexof(empty, 0, 16, 1ull << 63) == 16);
    assert(sp::filter<C::greater_equal>(dense, 0, 16, 0) == 65535);
    // Runtime guards on both selected x86 mixed-width families. Constant
    // activity would let an inlined caller erase even an unwanted load.
    const auto absent_local = [&]<unsigned K>() {
        const sp::dense_source<sp::layout<K>> absent{reinterpret_cast<const std::byte*>(memory + page * 2), 16};
        volatile std::uint64_t inactive = 0, outside = std::uint64_t{1} << K;
        assert(sp::indexof(absent, 0, 16, 0, inactive) == 16);
        assert(sp::indexof(absent, 0, 16, outside) == 16);
        volatile std::size_t bad_first = 17;
        assert(!sp::indexof(absent, bad_first, 1, 0));
    };
    absent_local.template operator()<30>();
    absent_local.template operator()<38>();
    assert(munmap(memory, page * 3) == 0);
#endif
}

void substitution() {
    using F = sp::layout<12>;
    using R = sp::layout<4>;
    alignas(64) std::array<std::uint8_t, 128> parent{}, child{};
    auto p = *sp::view<F>::attach(64, {{{std::as_writable_bytes(std::span{parent}), F::tile_bytes},{},{}}});
    auto c = *sp::view<R>::attach(64, {{{std::as_writable_bytes(std::span{child}), R::tile_bytes},{},{}}});
    for (unsigned i = 0; i < 64; ++i) {
        sp::author::store_unchecked(p, i, i * 37);
        sp::author::store_unchecked(c, i, i % 16);
    }
    auto original = sp::author::expression(p);
    auto expression = sp::author::with_payload(
        original, sp::author::with_tail(original.payload, sp::author::expression(c)));
    auto filter = *sp::bind_reader(expression, 64);
    for (unsigned first : {0u, 8u})
        for (unsigned count : {8u, 16u, 24u, 40u, 56u}) {
            std::uint64_t expected = 0;
            for (unsigned i = 0; i < count; ++i) {
                const auto row = first + i;
                if ((((row * 37) & ~15u) | row % 16) < 1000)
                    expected |= std::uint64_t{1} << i;
            }
            assert(filter.filter<C::less>(first, count, 1000) == expected);
            const auto rank = first + count - 1;
            const auto target = ((rank * 37) & ~15u) | rank % 16;
            assert(filter.indexof(first, count, target) == count - 1);
        }
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
    check_guarded_filters();
    check_filter_masks();
    check_filter_tiles();
    check_authored_filters();
    check_local_equal();
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
    if constexpr (!ikea::test::full_scope)
        puts("Fast test scope: width matrices cover sampled widths only");
    puts("filters: unsigned boundaries, layouts, selections and projection checks passed");
}
