#include <ikea/tuplepack/transfer.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <source_location>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
namespace tp = ikea::tuplepack;
void need(bool yes, const char* why, std::source_location at = std::source_location::current()) {
    if (!yes) {
        std::fprintf(stderr, "%s:%u: transfer: %s\n", at.file_name(), at.line(), why);
        std::abort();
    }
}
template <class T> T value(std::expected<T, tp::error> x) {
    need(bool(x), "unexpected refusal");
    return *x;
}
template <unsigned Bytes> tp::layout format(unsigned variant = 0) {
    std::array<tp::code, Bytes> codes;
    for (unsigned i = 0; i < Bytes; ++i)
        codes[i] = {std::uint8_t(i), 0, std::uint8_t(variant ? 1 + i % 8 : 8)};
    return value(tp::layout::make(Bytes, codes));
}
struct Guarded {
    std::uint8_t* mapping;
    std::size_t page = std::size_t(sysconf(_SC_PAGESIZE));
    explicit Guarded() {
        mapping = static_cast<std::uint8_t*>(
            mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        need(mapping != MAP_FAILED, "mmap");
        need(mprotect(mapping + page, page, PROT_NONE) == 0, "protect");
    }
    ~Guarded() {
        need(munmap(mapping, page * 2) == 0, "unmap");
    }
    std::span<std::uint8_t> ending(std::size_t size) {
        need(size <= page, "fixture size");
        return {mapping + page - size, size};
    }
};
std::size_t cases = 0;
template <unsigned Bytes> void successful() {
    const auto f = format<Bytes>(1);
    constexpr unsigned rows = 11, moved = 9;
    for (unsigned extra : {0u, 1u, 3u, 7u}) {
        Guarded source, destination;
        const auto stride = Bytes + extra, offset = 3u;
        auto from = source.ending(offset + (rows - 1) * stride + Bytes);
        auto to = destination.ending(offset + (rows - 1) * (stride + 2) + Bytes);
        std::fill(from.begin(), from.end(), std::uint8_t(0xa7));
        std::fill(to.begin(), to.end(), std::uint8_t(0x5a));
        for (unsigned row = 0; row < rows; ++row)
            for (unsigned octet = 0; octet < Bytes; ++octet)
                from[offset + row * stride + octet] = std::uint8_t(0x71 + row * 37 + octet * 29);
        const std::vector<std::uint8_t> source_before(from.begin(), from.end());
        const std::vector<std::uint8_t> target_before(to.begin(), to.end());
        auto src = value(tp::const_view::attach(f, std::as_bytes(std::span{from}), rows, stride, offset));
        auto dst = value(tp::view::attach(f, std::as_writable_bytes(std::span{to}), rows, stride + 2, offset));
        const auto op = value(tp::bind_transfer<Bytes>(f, src, f, dst));
        // Shuffled, duplicate and final source rows are legal; only destination is dense.
        const std::array<std::uint16_t, moved> selected{10, 1, 8, 3, 3, 0, 9, 5, 2};
        std::array<ikea::journal::entry, moved> entries{};
        ikea::journal effects{entries};
        need(bool(op.copy(2, std::span<const std::uint16_t>{selected}, effects)), "copy refused");
        auto expected = target_before;
        for (unsigned i = 0; i < moved; ++i)
            for (unsigned octet = 0; octet < Bytes; ++octet)
                expected[offset + (2 + i) * (stride + 2) + octet] =
                    source_before[offset + selected[i] * stride + octet];
        need(std::equal(expected.begin(), expected.end(), to.begin()), "bytes/gaps/spares differ");
        need(std::equal(source_before.begin(), source_before.end(), from.begin()),
             "source changed");
        need(effects.used == moved, "gapped exact spans not retained");
        for (unsigned i = 0; i < moved; ++i)
            need(entries[i].source == &dst && entries[i].plane == 0 &&
                     entries[i].offset == offset + (i + 2) * (stride + 2) &&
                     entries[i].size == Bytes,
                 "wrong effect coordinates");
        ++cases;
    }
}
template <unsigned Bytes> void prewrite() {
    const auto f = format<Bytes>();
    std::array<std::uint8_t, Bytes * 2> source{}, destination{};
    std::fill(source.begin(), source.end(), 0xa7);
    std::fill(destination.begin(), destination.end(), 0x5a);
    auto src = value(tp::const_view::attach(f, std::as_bytes(std::span{source}), 2, Bytes));
    auto dst = value(tp::view::attach(f, std::as_writable_bytes(std::span{destination}), 2, Bytes));
    auto op = value(tp::bind_transfer<Bytes>(f, src, f, dst));
    const std::array<std::size_t, 2> selected{1, 0};
    struct Coverage {
        const tp::view* named;
        unsigned calls{};
        void before(const tp::view& view, unsigned plane, std::size_t offset, std::size_t size) noexcept {
            const ikea::detail::write_span bytes{plane, offset, size};
            need(&view == named && bytes.offset == 0 && bytes.size == Bytes * 2,
                 "prewrite identity");
            for (unsigned i = 0; i < Bytes * 2; ++i)
                need(view.storage()[bytes.offset + i] == std::byte{0x5a}, "effect after store");
            ++calls;
        }
    } coverage{&dst};
    need(bool(op.admit(0, std::span<const std::size_t>{selected}, 2)), "prewrite admit");
    op.copy_unchecked(0, std::span<const std::size_t>{selected}, coverage);
    need(coverage.calls == 1, "prewrite calls");
    ++cases;
}
void refusal_and_alias() {
    const auto f = format<20>();
    const auto other = format<20>(1);
    std::array<std::uint8_t, 100> source{}, destination{};
    std::fill(source.begin(), source.end(), 0xa7);
    std::fill(destination.begin(), destination.end(), 0x5a);
    auto src = value(tp::view::attach(f, std::as_writable_bytes(std::span{source}), 5, 20));
    auto dst = value(tp::view::attach(f, std::as_writable_bytes(std::span{destination}), 5, 20));
    need(!tp::bind_transfer<20>(f, src, other, dst), "different layout accepted");
    need(!tp::bind_transfer<19>(f, src, f, dst), "wrong static extent accepted");
    auto op = value(tp::bind_transfer<20>(f, src, f, dst));
    std::array<ikea::journal::entry, 3> entries{};
    ikea::journal effects{entries};
    effects.before(dst, 0, 0, 1);
    const auto saved = destination;
    const auto saved_effect = entries[0];
    auto check = [&](std::size_t first, const auto& rows, tp::error error) {
        const auto used_before = effects.used;
        const auto accepted = op.copy(first, std::span<const std::uint16_t>{rows}, effects);
        need(!accepted && accepted.error() == error, "wrong refusal");
        need(destination == saved && effects.used == used_before &&
                 entries[0].source == saved_effect.source &&
                 entries[0].size == saved_effect.size,
             "refusal changed data/effects");
        ++cases;
    };
    check(4, std::array<std::uint16_t, 2>{0, 1}, tp::error::range);
    effects.used = 3;
    check(0, std::array<std::uint16_t, 3>{0, 1, 2}, tp::error::effects);
    effects.used = 1;
    check(0, std::array<std::uint16_t, 2>{0, 5}, tp::error::range);
    check(0, std::array<std::uint16_t, 2>{5, 0}, tp::error::range);
    effects.used = 4;
    need(!op.copy(0, std::span<const std::uint16_t>{std::array<std::uint16_t, 1>{0}}, effects),
         "bad journal admitted");
    effects.used = 0;
    std::array<std::uint8_t, 180> arena{};
    std::iota(arena.begin(), arena.end(), std::uint8_t(0));
    const auto before = arena;
    auto aliased_source = value(tp::const_view::attach(f, std::as_bytes(std::span{arena}), 9, 20));
    auto aliased_destination = value(tp::view::attach(f, std::as_writable_bytes(std::span{arena}), 9, 20));
    auto alias = value(tp::bind_transfer<20>(f, aliased_source, f, aliased_destination));
    const std::array<std::uint16_t, 2> overlap{2, 4};
    const auto denied = alias.copy(3, std::span<const std::uint16_t>{overlap}, effects);
    need(!denied && denied.error() == tp::error::overlap && arena == before && effects.used == 0,
         "future source overlap mutated");
    ++cases;
    const std::array<std::uint16_t, 2> independent{8, 7};
    need(bool(alias.copy(0, std::span<const std::uint16_t>{independent}, effects)),
         "nonoverlapping same-buffer rejected");
    need(std::equal(before.begin() + 160, before.begin() + 180, arena.begin()) &&
             std::equal(before.begin() + 140, before.begin() + 160, arena.begin() + 20),
         "same-buffer copy differs");
    need(effects.used == 1 && entries[0].source == &aliased_destination &&
             entries[0].size == 40,
         "tight effects not coalesced");
    ++cases;
    const std::array<std::uint16_t, 0> empty{};
    const auto used = effects.used;
    need(bool(alias.copy(9, std::span<const std::uint16_t>{empty}, effects)) &&
             effects.used == used,
         "empty end touched");
    ++cases;
    const std::array<tp::code, 1> invalid{{{0, 7, 2}}};
    need(!tp::layout::make(1, invalid), "malformed format valid");
}
template <class S, class D>
concept can_bind = requires(const tp::layout& f, S&& s, D&& d) {
    tp::bind_transfer<4>(f, std::forward<S>(s), f, std::forward<D>(d));
};
static_assert(can_bind<tp::view&, tp::view&>);
static_assert(can_bind<tp::const_view&, tp::view&>);
static_assert(!can_bind<tp::view, tp::view&>);
static_assert(!can_bind<tp::const_view, tp::view&>);
static_assert(!can_bind<tp::view&, tp::view>);
static_assert(!can_bind<tp::view, tp::view>);

void contract_boundaries() {
    const std::array<tp::code, 2> codes{{{0, 3, 2}, {3, 1, 3}}};
    const auto f = value(tp::layout::make(8, codes));
    std::array<std::uint8_t, 64> source{}, destination{};
    for (unsigned i = 0; i < 64; ++i)
        source[i] = std::uint8_t(i * 37 + 17);
    destination.fill(0x5a);
    const auto source_before = source;
    auto src = value(tp::const_view::attach(f, std::as_bytes(std::span{source}), 4, 12, 1));
    auto dst = value(tp::view::attach(f, std::as_writable_bytes(std::span{destination}), 4, 13, 2));
    auto op = value(tp::bind_transfer<8>(f, src, f, dst));
    const std::array<unsigned, 2> rows{3, 1};
    struct Coverage {
        const tp::view& dst;
        unsigned calls{};
        void before(const tp::view& named, unsigned plane, std::size_t offset, std::size_t size) noexcept {
            const ikea::detail::write_span bytes{plane, offset, size};
            need(&named == &dst && bytes.plane == 0 && bytes.offset == 2 + (calls + 1) * 13 &&
                     bytes.size == 8,
                 "gapped prewrite coordinates");
            for (unsigned i = 0; i < 8; ++i)
                need(dst.row_unchecked(calls + 1)[i] == std::byte{0x5a}, "gapped effect followed store");
            ++calls;
        }
    } coverage{dst};
    need(bool(op.admit(1, std::span<const unsigned>{rows}, 2)), "gapped admission");
    op.copy_unchecked(1, std::span<const unsigned>{rows}, coverage);
    std::array<std::uint8_t, 64> expected;
    expected.fill(0x5a);
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned b = 0; b < 8; ++b)
            expected[2 + (i + 1) * 13 + b] = source_before[1 + rows[i] * 12 + b];
    need(destination == expected && source == source_before && coverage.calls == 2,
         "whole spare bytes/gaps/source differ");
    ++cases;

    const std::array<tp::code, 2> reversed{{codes[1], codes[0]}};
    const auto reordered = value(tp::layout::make(8, reversed));
    const auto shifted =
        value(tp::layout::make(8, std::array<tp::code, 2>{{{0, 4, 2}, {3, 1, 3}}}));
    const auto moved = value(tp::layout::make(8, std::array<tp::code, 2>{{{1, 3, 2}, {3, 1, 3}}}));
    for (const auto& different : {reordered, shifted, moved}) {
        auto result = tp::bind_transfer<8>(f, src, different, dst);
        need(!result && result.error() == tp::error::description,
             "rank/shift/offset mismatch admitted");
        ++cases;
    }
    const auto small = format<4>();
    auto short_src = value(tp::const_view::attach(small, std::as_bytes(std::span{source}), 4, 4));
    auto short_dst = value(tp::view::attach(small, std::as_writable_bytes(std::span{destination}), 4, 4));
    need(!tp::bind_transfer<8>(f, short_src, f, dst), "source view extent mismatch admitted");
    need(!tp::bind_transfer<8>(f, src, f, short_dst), "destination view extent mismatch admitted");
    ++cases;

    // Source in a destination gap is deliberately refused even though stores
    // would not touch it; touching interval endpoints are disjoint and accepted.
    std::array<std::uint8_t, 40> arena{};
    std::iota(arena.begin(), arena.end(), std::uint8_t(11));
    auto gap_src = value(tp::const_view::attach(small, std::as_bytes(std::span{arena}), 5, 4));
    auto gap_dst = value(tp::view::attach(small, std::as_writable_bytes(std::span{arena}), 2, 12));
    auto gap_op = value(tp::bind_transfer<4>(small, gap_src, small, gap_dst));
    const std::array<unsigned, 2> in_gap{4, 1};
    std::array<ikea::journal::entry, 3> entries{
        {{&src, 0, 3, 2}, {&dst, 0, 23, 1}, {nullptr, 0, 97, 7}}};
    const auto saved_entries = entries;
    ikea::journal effects{entries, 1};
    const auto before = arena;
    auto denied = gap_op.copy(0, std::span<const unsigned>{in_gap}, effects);
    need(!denied && denied.error() == tp::error::overlap && arena == before && effects.used == 1,
         "late gap overlap changed output");
    for (unsigned i = 0; i < 3; ++i)
        need(entries[i].source == saved_entries[i].source &&
                 entries[i].plane == saved_entries[i].plane &&
                 entries[i].offset == saved_entries[i].offset &&
                 entries[i].size == saved_entries[i].size,
             "refusal touched journal storage");
    ++cases;
    const std::array<unsigned, 1> at_end{1};
    need(bool(gap_op.copy(0, std::span<const unsigned>{at_end}, effects)),
         "touching endpoint refused");
    need(std::equal(before.begin() + 4, before.begin() + 8, arena.begin()),
         "adjacent source copy differs");
    ++cases;

    // Empty views at a guard page must not form or touch a payload address.
    Guarded guard;
    auto empty_storage = guard.ending(0);
    auto empty_src = value(tp::const_view::attach(small, std::as_bytes(std::span{empty_storage}), 0, 4));
    auto empty_dst = value(tp::view::attach(small, std::as_writable_bytes(std::span{empty_storage}), 0, 4));
    auto empty_op = value(tp::bind_transfer<4>(small, empty_src, small, empty_dst));
    ikea::journal empty_effects{{}};
    need(bool(empty_op.copy(0, std::span<const std::size_t>{}, empty_effects)) &&
             empty_effects.used == 0,
         "empty guarded access");
    auto past_end = empty_op.copy(1, std::span<const std::size_t>{}, empty_effects);
    need(!past_end && past_end.error() == tp::error::range, "empty beyond end accepted");
    ++cases;
}
template <unsigned... I> void all(std::integer_sequence<unsigned, I...>) {
    ((successful<I + 1>(), prewrite<I + 1>()), ...);
}
int main() {
    all(std::make_integer_sequence<unsigned, 64>{});
    refusal_and_alias();
    contract_boundaries();
    std::printf("transfer: %zu exact-unit/guarded-padding/prewrite/refusal/alias cases passed\n",
                cases);
}
