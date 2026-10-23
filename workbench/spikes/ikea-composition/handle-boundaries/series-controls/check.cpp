#include "oracle.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
namespace series_controls {
namespace {
unsigned checking_width;
std::size_t checking_rows;
void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "series-controls width=%u rows=%zu: %s\n", checking_width, checking_rows, message);
        std::abort();
    }
}
template <class T> auto take(T result) { require(bool(result), "unexpected admission failure"); return std::move(*result); }
bool same(const ikea::journal::entry& a, const ikea::journal::entry& b) {
    return a.source == b.source && a.plane == b.plane && a.offset == b.offset && a.size == b.size;
}
void coverage(const destination& dst, const ikea::journal& effects) {
    std::vector<bool> occupied(dst.bytes.size());
    for (const auto& e : effects.entries()) if (e.source == &dst) {
        require(e.plane == 0 && e.offset <= occupied.size() && e.size <= occupied.size() - e.offset,
                "journal plane or byte range");
        std::fill(occupied.begin() + e.offset, occupied.begin() + e.offset + e.size, true);
    }
    require(std::all_of(occupied.begin(), occupied.end(), [](bool bit) { return bit; }),
            "journal union misses occupied bytes");
}
template <class U> void reads(shape control, ikea::bytes bytes, std::span<const word> expected) {
    if (control.width() > sizeof(U) * 8) {
        require(!shape::make(control.law(), sizeof(U) * 8), "narrow carrier admitted"); return;
    }
    auto shared = take(bind_reader<U>(control, bytes, expected.size()));
    auto copied = take(sp::bind_reader<U>(sp::dense_input{control.width(), control.geometry(), bytes, expected.size()}));
    std::array<U, 35> out;
    for (std::size_t row = 0; row < expected.size(); ++row) {
        require(take(shared.get(row)) == expected[row], "shared point");
        require(take(copied.get(row)) == expected[row], "copied point");
        require(read_columns<U>.points[control.index](bytes.data(), row) == expected[row], "column point");
    }
    require(!shared.get(expected.size()), "point range refusal");
    for (std::size_t first = 0; first <= std::min<std::size_t>(20, expected.size()); ++first)
        for (std::size_t count = 0; count <= std::min<std::size_t>(32, expected.size() - first); ++count) {
            out.fill(std::numeric_limits<U>::max());
            require(bool(shared.read(first, std::span(out).subspan(1, count))), "shared range admission");
            require(out.front() == std::numeric_limits<U>::max() && out[count + 1] == std::numeric_limits<U>::max(), "range output guard");
            for (std::size_t i = 0; i < count; ++i) require(out[i + 1] == expected[first + i], "range value");
        }
    for (std::size_t first = 0; first + 16 <= expected.size(); first += 16) {
        out.fill(std::numeric_limits<U>::max());
        require(bool(shared.read16(first, std::span(out).subspan(1))), "native16 grant");
        for (unsigned i = 0; i < 16; ++i) require(out[i + 1] == expected[first + i], "native16 value");
        require(out.front() == std::numeric_limits<U>::max() && out[17] == std::numeric_limits<U>::max(), "native16 output guard");
    }
    out.fill(19); const auto before = out;
    require(!shared.read(expected.size() + 1, std::span(out).first(1)) && out == before, "range refusal changed output");
    require(!shared.read16(expected.size() / 16 * 16, out) && out == before, "short logical window admitted");
    if (!bytes.empty()) require(!bind_reader<U>(control, bytes.first(bytes.size() - 1), expected.size()), "short physical grant admitted");
}
void initialization(shape control, std::span<const word> input, const std::vector<std::byte>& expected) {
    std::vector<std::byte> data(expected.size() + 2, std::byte{0x6d});
    destination dst{control, std::span(data).subspan(1, expected.size()), input.size()};
    // Conservative preflight can count body/tail fields separately even when
    // physical execution coalesces them. At257 rows,2*ceil(257/8)+2 old entries.
    std::array<ikea::journal::entry, 68> storage{};
    ikea::journal effects{storage};
    for (bool dispatch : {false, true}) {
        effects.used = 0; std::fill(dst.bytes.begin(), dst.bytes.end(), std::byte{0xdb});
        auto result = dispatch ? initialize_dispatch(dst, input, effects) : initialize_columns[control.index](dst, input, effects);
        if (!result) std::fprintf(stderr, "geometry=%u dispatch=%d initializer error=%u remaining=%zu\n",
            unsigned(control.geometry()), dispatch, unsigned(result.error()), effects.remaining());
        require(bool(result), "initializer failed");
        require(std::equal(dst.bytes.begin(), dst.bytes.end(), expected.begin()), "initializer bytes/slack");
        require(data.front() == std::byte{0x6d} && data.back() == std::byte{0x6d}, "initializer guard");
        if (input.empty()) require(!effects.used, "empty initializer emitted effects");
        for (auto e : effects.entries())
            require(e.source == &dst, "unstable source");
        coverage(dst, effects);
    }
    if (!input.empty()) {
        const auto before = data;
        auto bad = std::vector<word>(input.begin(), input.end());
        if (control.width() < 64) {
            bad.back() = word{1} << control.width(); effects.used = 0;
            auto result = initialize_columns[control.index](dst, bad, effects);
            require(!result && result.error() == sp::error::value && data == before && effects.used == 0, "value refusal effects");
        }
        ikea::journal none{std::span<ikea::journal::entry>{}};
        auto result = initialize_columns[control.index](dst, input, none);
        require(!result && result.error() == sp::error::effects && data == before && none.used == 0, "capacity refusal effects");
        auto short_dst = dst; short_dst.bytes = short_dst.bytes.first(short_dst.bytes.size() - 1);
        effects.used = 0; result = initialize_columns[control.index](short_dst, input, effects);
        require(!result && data == before && effects.used == 0, "extent refusal effects");
        std::vector<std::byte> other(expected.size()); destination next{control, other, input.size()};
        effects.used = 0;
        require(bool(initialize_columns[control.index](dst, input, effects)), "first owner");
        const auto first = std::vector(effects.entries().begin(), effects.entries().end());
        require(bool(initialize_columns[control.index](next, input, effects)), "second owner");
        require(effects.used >= first.size() && std::equal(first.begin(), first.end(), effects.storage.begin(), same),
                "next placement changed old journal entries");
        for (auto e : effects.entries()) require(e.source == &dst || e.source == &next, "successive owner identity");
        coverage(dst, effects); coverage(next, effects);
        unsigned unrelated = 91;
        effects.storage[0] = {&dst, 0, 0, expected.size()};
        effects.storage[1] = {&unrelated, 37, 19, 3}; effects.used = 2;
        require(bool(initialize_columns[control.index](dst, input, effects)), "existing source/type journal");
        require(effects.used >= 2 && same(effects.storage[0], {&dst, 0, 0, expected.size()}) &&
                same(effects.storage[1], {&unrelated, 37, 19, 3}),
                "whole journal translation rewrote an unrelated entry");
        for (auto e : effects.entries().subspan(2)) require(e.source == &dst, "new owner identity");
        coverage(dst, effects);
        effects.storage[0] = {&dst, 0, 0, expected.size()}; effects.used = 1;
        require(bool(initialize_columns[control.index](dst, input, effects)) && effects.used >= 1 &&
                same(effects.storage[0], {&dst, 0, 0, expected.size()}),
                "existing same owner lost identity during local-view execution");
        for (auto e : effects.entries()) require(e.source == &dst, "same owner identity");
        coverage(dst, effects);
        if (control.width() == 5 && control.geometry() == sp::geometry::striped && input.size() == 257) {
            std::array<ikea::journal::entry, 8> short_records{}; ikea::journal short_effects{short_records};
            result = initialize_columns[control.index](dst, input, short_effects);
            require(!result && result.error() == sp::error::effects && !short_effects.used && data == before,
                    "eight-entry conservative capacity refusal");
        }
    }
}
void protected_tail(shape control) {
    const auto truth = values(control.width(), 13, 0x718f29); const auto encoded = encode_reference(control, truth);
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    require(encoded.size() < page, "guard fixture fits page");
    auto* mapping = static_cast<std::byte*>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    require(mapping != MAP_FAILED, "mmap"); require(mprotect(mapping + page, page, PROT_NONE) == 0, "mprotect");
    auto* start = mapping + page - encoded.size(); std::copy(encoded.begin(), encoded.end(), start);
    auto read = take(bind_reader<word>(control, {start, encoded.size()}, truth.size()));
    std::array<word, 13> out{}; require(bool(read.read(0, out)) && std::equal(out.begin(), out.end(), truth.begin()), "protected exact tail");
    munmap(mapping, page * 2);
}
}
void check() {
    for (unsigned width = 1; width <= 64; ++width) for (auto geometry : {sp::geometry::local, sp::geometry::striped}) {
        if (geometry == sp::geometry::striped && !sp::striped_width(width)) continue;
        checking_width = width; auto control = take(shape::make({width, 0, geometry}));
        require(!shape::make({width, 8, geometry}), "headed law admitted by headless prototype");
        for (std::size_t count : {0u, 1u, 7u, 8u, 13u, 15u, 16u, 17u, 31u, 32u, 63u, 64u, 255u, 256u, 257u}) {
            checking_rows = count; const auto truth = values(width, count, 0x3291ed + count + width);
            const auto encoded = encode_reference(control, truth);
            require(take(control.bytes(count)) == encoded.size(), "independent occupied bytes");
            reads<std::uint8_t>(control, encoded, truth); reads<std::uint16_t>(control, encoded, truth);
            reads<std::uint32_t>(control, encoded, truth); reads<word>(control, encoded, truth);
            initialization(control, truth, encoded);
        }
        protected_tail(control);
    }
    require(!shape::make({0, 0, sp::geometry::local}) && !shape::make({65, 0, sp::geometry::local}) &&
            !shape::make({8, 0, sp::geometry::striped}), "invalid law");
    auto widest = take(shape::make({64, 0, sp::geometry::local}));
    require(!widest.bytes(std::numeric_limits<std::size_t>::max()), "extent overflow");
    std::puts("series-controls: all76 laws, independent wire/initialized slack, narrow carriers, arbitrary edges, native16 grants, protected exact tails, refusals and stable whole-journal sources passed");
}
}
