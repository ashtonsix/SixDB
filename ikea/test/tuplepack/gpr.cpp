#include "packet_wire.h"
#include "grouped_maintenance.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/maintenance.h>
#include <sys/mman.h>
#include <unistd.h>
void require(bool valid, unsigned rows, unsigned trial, const char *scenario) {
    if (!valid) {
        std::fprintf(stderr, "TuplePack GPR: rows=%u trial=%u %s\n", rows, trial, scenario);
        std::abort();
    }
}
// Every short/full word transfer, bit width and mask, ending at a guard page.
// Random wire checks above cover holes, repeats on read, and scattered mappings.
template <unsigned Rows> void transfers() {
    constexpr unsigned B = 8 / Rows;
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto allocation = static_cast<tp::byte *>(
        mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    require(allocation != MAP_FAILED && !mprotect(allocation + page, page, PROT_NONE), Rows, 0,
            "guard setup");
    for (unsigned bytes = 1; bytes <= B; ++bytes)
        for (unsigned shift = 0; shift < 8; ++shift)
            for (unsigned width = 1; width <= 8 - shift; ++width) {
                std::vector<tp::code> codes(bytes);
                std::vector<tp::byte> map(bytes);
                for (unsigned i = 0; i < bytes; ++i) {
                    codes[i] = {tp::byte(i), tp::byte(shift), tp::byte(width)};
                    map[i] = i;
                }
                auto f = *tp::layout::make(bytes, codes);
                auto rp = *tp::reader<8, Rows>::make(f, map);
                auto wp = *tp::writer<8, Rows>::make(f, map);
                require(rp.read_bytes() == (1u << bytes) - 1 &&
                            wp.write_bytes() == (1u << bytes) - 1 &&
                            wp.read_bytes() == (width == 8 ? 0 : (1u << bytes) - 1),
                        Rows, bytes, "transfer coverage");
                for (unsigned count : {1u, Rows})
                    for (unsigned stride : {bytes, 17u}) {
                        auto data = std::span(allocation + page - (count - 1) * stride - bytes,
                                              (count - 1) * stride + bytes);
                        auto v = *tp::view::bind(f, data, count, stride);
                        auto read = *tp::bind_reader(rp, v);
                        auto write = *tp::bind_writer(wp, v);
                        const auto nr = tp::native_reader(read);
                        const auto nw = tp::native_writer(write);
                        for (std::uint64_t active = 0; active < (1u << count); ++active) {
                            std::fill(data.begin(), data.end(), 0xa5);
                            std::uint64_t expected = 0, input = 0;
                            for (unsigned r = 0; r < count; ++r)
                                if (active & (1u << r))
                                    for (unsigned i = 0; i < bytes; ++i) {
                                        expected |=
                                            std::uint64_t((0xa5 >> shift) & ((1u << width) - 1))
                                            << (8 * (r * bytes + i));
                                        input |= std::uint64_t((1u << width) - 1)
                                                 << (8 * (r * bytes + i));
                                    }
                            require(read.get(0, active) == expected &&
                                        nr.get_unchecked(0, active) == expected &&
                                        tp::native::read<Rows>(rp.controls(), data.data(), stride,
                                                               active) == expected,
                                    Rows, bytes, "bounded full/short transfer read");
                            auto fixed = [&]<unsigned Bytes>() {
                                if constexpr (Bytes <= B) {
                                    const auto shape = nr.template admit_transfer<Bytes>();
                                    require(bool(shape) == (Bytes == bytes), Rows, bytes,
                                            "fixed transfer shape admission");
                                    if (shape) {
                                        require(bool(nr.admit(0, active)), Rows, bytes,
                                                "fixed transfer window admission");
                                        require(nr.template get_transfer_unchecked<Bytes>(0, active) == expected,
                                                Rows, bytes, "fixed guarded transfer independent value");
                                        require(nr.template get_transfer_unchecked<Bytes>(
                                                    std::numeric_limits<std::size_t>::max(), 0) == 0,
                                                Rows, bytes, "empty fixed transfer performs no address access");
                                        require(!nr.admit(count, 1), Rows, bytes,
                                                "fixed transfer retains row bounds");
                                    }
                                }
                            };
                            fixed.template operator()<1>(); fixed.template operator()<2>();
                            fixed.template operator()<3>(); fixed.template operator()<4>();
                            fixed.template operator()<5>(); fixed.template operator()<6>();
                            fixed.template operator()<7>(); fixed.template operator()<8>();
                            auto address = [&](unsigned r) {
                                require(active & (1u << r), Rows, bytes, "only active callbacks");
                                return data.data() + r * stride;
                            };
                            require(tp::native::read_body<Rows>(rp.controls(), address, active) ==
                                        expected,
                                    Rows, bytes, "unhinted address callback");
                            for (bool native : {false, true}) {
                                std::fill(data.begin(), data.end(), 0xa5);
                                std::array<ikea::owner_write, 8> storage;
                                ikea::source_write_journal effects{storage};
                                require(bool(native ? nw.set(0, input, effects, active)
                                                    : write.set(0, input, effects, active)),
                                        Rows, bytes, "bounded transfer write");
                                for (unsigned r = 0; r < count; ++r)
                                    for (unsigned i = 0; i < bytes; ++i)
                                        require(data[r * stride + i] ==
                                                    ((active & (1u << r))
                                                         ? 0xa5 | (((1u << width) - 1) << shift)
                                                         : 0xa5),
                                                Rows, bytes, "inactive and padding preserved");
                                for (unsigned e = 0; e < effects.used; ++e)
                                    for (std::size_t i = storage[e].bytes.offset;
                                         i < storage[e].bytes.offset + storage[e].bytes.size; ++i)
                                        require(i < data.size() && i % stride < bytes &&
                                                    (active & (1u << (i / stride))),
                                                Rows, bytes, "effects exclude gaps/inactive rows");
                            }
                            const std::vector<tp::byte> wanted(data.begin(), data.end());
                            std::fill(data.begin(), data.end(), 0xa5);
                            tp::native::write<Rows>(wp.controls(), data.data(), stride, input,
                                                    active);
                            require(std::equal(data.begin(), data.end(), wanted.begin()), Rows,
                                    bytes, "compiled raw endpoint preserves complete allocation");
                        }
                    }
            }
    require(!munmap(allocation, 2 * page), Rows, 0, "guard release");
}
struct law {
    static constexpr bool needs_before = true, needs_after = true;
    unsigned calls = 0;
    std::uint64_t old = 0, next = 0;
    template <class Before, class After>
    void observe_batch(std::size_t first, std::uint64_t active, const Before &before,
                       const After &after) {
        require(first == 1 && active == 3, 2, 0, "original row observation");
        ++calls;
        old = std::get<0>(before);
        next = std::get<0>(after);
    }
};
void composition() {
    const std::array<tp::code, 2> codes{{{0, 0, 3}, {0, 3, 2}}};
    const std::array<tp::code, 1> complete{{{0, 0, 8}}};
    const std::array<tp::byte, 1> a{0}, b{1};
    auto f = *tp::layout::make(1, codes), observed = *tp::layout::make(1, complete);
    std::array<tp::byte, 3> bytes{0xa5, 0xa5, 0xa5}, replacement{0xe3, 0xe3, 0xe3};
    auto v = *tp::view::bind(f, bytes, 3, 1), v2 = *tp::view::bind(f, replacement, 3, 1);
    auto ov = *tp::view::bind(observed, bytes, 3, 1);
    auto aw = *tp::writer<8, 2>::make(f, a), bw = *tp::writer<8, 2>::make(f, b);
    auto rp = *tp::reader<8, 2>::make(observed, a);
    auto ar = *tp::bind_reader(rp, ov);
    auto ao = *tp::bind_writer(aw, v), bo = *tp::bind_writer(bw, v),
         moved = *tp::bind_writer(aw, v2);
    auto child = *tp::composition::bind_group(tp::native_writer(ao));
    auto parent = *tp::composition::bind_group(child, tp::native_writer(bo));
    auto projected = tp::composition::projection(tp::native_reader(ar));
    law summary;
    auto maintenance = tp::observation(projected, summary);
    std::array<ikea::owner_write, 8> records;
    ikea::source_write_journal effects{records};
    const std::uint64_t av = 7ull | (7ull << 8), bv = 3ull | (3ull << 8);
    require(bool(parent.set(1, {{av}, bv}, effects, 3, maintenance)), 2, 0, "nested word mutation");
    require(summary.calls == 1 && summary.old == (0xa5ull | (0xa5ull << 8)) &&
                summary.next == (0xbfull | (0xbfull << 8)),
            2, 0, "complete before and after");
    auto substituted = *tp::composition::bind_group(tp::native_writer(moved));
    auto rebuilt = *tp::composition::bind_group(substituted, tp::native_writer(bo));
    require(bool(rebuilt.set(1, {{0}, 0}, effects, 3)), 2, 0, "nested substitution");
    require(bytes == std::array<tp::byte, 3>{0xa5, 0xa7, 0xa7} &&
                replacement == std::array<tp::byte, 3>{0xe3, 0xe0, 0xe0},
            2, 0, "separate owner placement");
    auto saved = bytes;
    effects.used = 0;
    std::array<std::uint64_t, 2> input{av, 8};
    auto native = tp::native_writer(ao);
    tp::no_maintenance none;
    tp::erased_mutation<std::uint64_t, tp::no_maintenance, 2> erased(native);
    require(!erased.replace(0, input, effects, tp::selection::all(), none), 2, 0,
            "late erased width failure");
    require(bytes == saved && !effects.used, 2, 0, "whole range failure unchanged");
    input[1] = 7;
    require(bool(erased.replace(0, input, effects, tp::selection::all(), none)), 2, 0,
            "erased final one-row tail");
}
void rejected_transfers() {
    const std::array<tp::code, 4> codes{{{0, 0, 8}, {1, 0, 8}, {2, 0, 8}, {3, 0, 8}}};
    auto layout = *tp::layout::make(4, codes);
    const std::array<tp::byte, 4> data{0x12, 0x34, 0x56, 0x78};
    auto view = *tp::const_view::bind(layout, data, 1, 4);
    auto check = [&](std::span<const tp::byte> map, std::span<const unsigned> groups,
                     std::uint64_t expected) {
        auto plan = *tp::reader<8>::make(layout, map, groups);
        auto read = *tp::bind_reader(plan, view);
        auto native = tp::native_reader(read);
        require(!native.admit_transfer<1>() && !native.admit_transfer<2>() &&
                    !native.admit_transfer<3>() && !native.admit_transfer<4>(),
                1, 0, "unsupported projection rejects specialization");
        require(read.get(0) == expected && native.get_unchecked(0) == expected,
                1, 0, "unsupported projection keeps prepared and native fallback");
    };
    check({}, {}, 0);
    check(std::array<tp::byte, 2>{0, 2}, {}, 0x5612);
    check(std::array<tp::byte, 2>{1, 0}, {}, 0x1234);
    check(std::array<tp::byte, 2>{0, 0}, {}, 0x1212);
    check(std::array<tp::byte, 2>{0, tp::hole}, {}, 0x12);
    // Grouping one row does not change byte order; preparation normalizes it.
    auto one_plan = *tp::reader<8>::make(layout, std::array<tp::byte, 2>{0, 1},
                                       std::array<unsigned, 2>{1, 1});
    auto one_read = *tp::bind_reader(one_plan, view);
    auto one = tp::native_reader(one_read);
    require(bool(one.admit_transfer<2>()) && one.get_transfer_unchecked<2>(0) == 0x3412,
            1, 0, "equivalent one-row groups retain fixed transfer");
    auto pair_layout = *tp::layout::make(2, std::array<tp::code, 2>{{{0, 0, 8}, {1, 0, 8}}});
    auto pair_view = *tp::const_view::bind(pair_layout, data, 2, 2);
    auto pair_plan = *tp::reader<8, 2>::make(pair_layout, std::array<tp::byte, 2>{0, 1},
                                          std::array<unsigned, 2>{1, 1});
    auto pair_read = *tp::bind_reader(pair_plan, pair_view);
    auto pair = tp::native_reader(pair_read);
    require(!pair.admit_transfer<1>() && !pair.admit_transfer<2>() &&
                !pair.admit_transfer<3>() && !pair.admit_transfer<4>() &&
                pair_read.get(0) == 0x78345612 && pair.get_unchecked(0) == 0x78345612,
            2, 0, "group-major rows reject transfer and preserve packet order");
    auto shifted_layout = *tp::layout::make(2, std::array<tp::code, 2>{{{0, 1, 7}, {1, 2, 6}}});
    auto shifted_view = *tp::const_view::bind(shifted_layout, std::span(data).first(2), 1, 2);
    auto shifted_plan = *tp::reader<8>::make(shifted_layout, std::array<tp::byte, 2>{0, 1});
    auto shifted = *tp::bind_reader(shifted_plan, shifted_view);
    require(!tp::native_reader(shifted).admit_transfer<2>() && shifted.get(0) == 0x0d09,
            1, 0, "unequal shifts reject fixed transfer and preserve fallback");
}
void pipeline_check();
void carrier_check();
int main() {
    grouped_maintenance<8>();
    std::mt19937 random(420312);
    wire<8, 1>(random);
    wire<8, 2>(random);
    wire<8, 4>(random);
    wire<8, 8>(random);
    transfers<1>();
    transfers<2>();
    transfers<4>();
    transfers<8>();
    composition();
    rejected_transfers();
    pipeline_check();
    carrier_check();
    std::puts("TuplePack GPR: four shapes, wire/effects, guarded transfers, erasure, nested "
              "maintenance and CPS passed");
}
