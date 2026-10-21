#include <ikea/streampack.h>
#include <ikea/streampack/author/native.h>
#include <ikea/boundlist/author/search.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace sp = ikea::streampack;
namespace bl = ikea::boundlist;
namespace sc = ikea::boundlist;
struct fixture {
    sp::layout format;
    alignas(64) std::array<std::byte, 4096> directory{}, lengths{};
    std::array<std::byte, 6> control{};
    std::vector<std::byte> payload;
    sp::view source;
    fixture(sp::description d, unsigned bytes) : fixture(*sp::layout::prepare(d), bytes) {}
    fixture(sp::layout layout, unsigned bytes)
        : format(layout), payload(bytes),
          source(*sp::view::bind(format,
                                 {payload, std::span(directory).first(format.directory_bytes()),
                                  std::span(lengths).first(format.lengths_bytes()),
                                  std::span(control).first(format.control_bytes())})) {}
};
void history(sp::description d, unsigned n) {
    std::vector<unsigned> sizes, used, ends{0};
    const unsigned bound = std::min(unsigned(d.maximum), d.byte_limit / std::max(1u, n));
    for (unsigned i = 0; i < n; ++i) {
        const unsigned r =
            d.fixed_reservation.value_or(i % 17 == 0 ? bound : std::min(bound, i % 31));
        sizes.push_back(r);
        used.push_back(d.full ? r : r / 2);
        ends.push_back(ends.back() + r);
    }
    fixture f(d, ends.back());
    ikea::no_coverage effects;
    assert(sp::initialize(f.source, effects));
    std::vector<std::byte> input(d.maximum, std::byte{0x71});
    for (unsigned r = 0; r < n; ++r)
        assert(sp::append(f.source, std::span(input).first(used[r]), sizes[r], effects));
    assert(f.source.validate() && f.source.extent() == ends.back());
    sp::author::dispatch(f.source, [&](auto a) {
        for (unsigned r = 0; r < n; ++r) {
            const auto p = a.locate(r);
            assert(p.offset == ends[r] && p.reservation == sizes[r] && p.length == used[r]);
            auto value = a.get(r);
            assert(value && value->size() == used[r]);
            for (auto x : *value)
                assert(x == std::byte{0x71});
        }
        for (auto q : ends)
            for (auto p : {q ? q - 1 : 0, q, q + 1}) {
                unsigned expected = 0;
                while (expected < n && ends[expected + 1] <= p)
                    ++expected;
                const auto found = sp::reverse(f.source, p);
                assert(bool(found) == (expected < n));
                if (found)
                    assert(found->rank == expected && !found->tombstone);
                assert(a.template boundary<bl::edge::after>(p) == expected);
            }
#if defined(__aarch64__) || defined(__AVX2__)
        for (unsigned group = 0; group < (n + 15) / 16; ++group) {
            const auto packet = sp::native::read16(a, group);
            std::array<unsigned, 16> offsets{}, lengths{};
            for (unsigned c = 0; c < sp::native::chunks; ++c) {
                bl::native::store(offsets.data() + c * bl::native::lanes, packet.values.offsets(c));
                bl::native::store(lengths.data() + c * bl::native::lanes, packet.values.lengths(c));
            }
            for (unsigned j = 0; j < std::min(16u, n - group * 16); ++j) {
                assert(offsets[j] == ends[group * 16 + j] && lengths[j] == used[group * 16 + j]);
                assert(packet.live & (1u << j));
            }
        }
#endif
    });
    if (!d.immortal)
        for (unsigned r = 0; r < n; r += 5)
            assert(sp::retire(f.source, r, effects));
    if (!d.fixed_reservation || !*d.fixed_reservation) {
        assert(sp::soft_pack(f.source, true, effects) && f.source.validate());
        unsigned end = 0;
        sp::author::dispatch(f.source, [&](auto a) {
            for (unsigned r = 0; r < n; ++r) {
                const bool live = d.immortal || r % 5;
                assert(a.start(r) == end && a.reserved(r) == (live ? used[r] : 0));
                end += live ? used[r] : 0;
            }
        });
        assert(f.source.extent() == end);
    }
}
void generic_relative() {
    for (unsigned n : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 64u, 128u, 256u})
        for (std::uint64_t origin : {0ull, 1ull << 40, 1ull << 63}) {
            alignas(64) std::array<std::uint16_t, 256> local{};
            std::array<std::uint64_t, 16> checkpoints{};
            std::vector<std::uint64_t> expected;
            auto position = origin;
            unsigned sum = 0;
            for (unsigned r = 0; r < n; ++r) {
                if (!(r % 16)) {
                    checkpoints[r / 16] = position;
                    sum = 0;
                }
                const unsigned width = r % 5 ? r % 13 : 0;
                sum += width;
                local[r] = sum;
                position += width;
                expected.push_back(position);
            }
            const bl::relative_end_source<std::uint16_t, std::uint64_t> source{
                local.data(), checkpoints.data(), n, 16, position};
            auto queries = expected;
            queries.push_back(0);
            queries.push_back(origin);
            queries.push_back(position + 1);
            for (auto q : queries) {
                const auto lo =
                    std::lower_bound(expected.begin(), expected.end(), q) - expected.begin();
                const auto hi =
                    std::upper_bound(expected.begin(), expected.end(), q) - expected.begin();
                assert(bl::lower_bound(source, q) == lo && bl::upper_bound(source, q) == hi);
                assert(bl::author::prepare_lower_bound(source, q).finish() == lo);
                assert(bl::author::prepare_upper_bound(source, q).finish() == hi);
            }
            assert(bl::lower_bound(source, -1) == 0);
        }
}
void legacy_conversion() {
    const std::array<std::byte, 8> wire{std::byte{0x41}, std::byte{0xff}, std::byte{0xff},
                                        std::byte{0xff}, std::byte{},     std::byte{},
                                        std::byte{},     std::byte{}};
    const auto legacy = *sp::layout::decode(wire);
    fixture old(legacy, 3), modern(legacy.schema(), 3);
    assert(old.format.encode() == wire && old.format.width_bytes() == 2 &&
           old.format.lengths_bytes(1) == 64);
    assert(modern.format.width_bytes() == 1 && modern.format.lengths_bytes(1) == 32);
    old.lengths[0] = std::byte{3};
    old.lengths[32] = std::byte{1};
    old.control[0] = std::byte{1};
    old.control[2] = std::byte{3};
    old.payload[0] = std::byte{'x'};
    assert(old.source.validate() && sp::value(old.source, 0)->size() == 1);
    const std::array<std::uint16_t, 1> selected{0};
    ikea::no_coverage effects;
    const auto original = old.lengths;
    assert(sp::transfer(old.source, selected, modern.source, false, effects));
    assert(modern.source.validate() && sp::value(modern.source, 0)->size() == 1);
    assert(sp::value(modern.source, 0)->front() == std::byte{'x'} && old.lengths == original);
    auto current = wire;
    current[0] = std::byte{0xc1};
    assert(modern.format.encode() == current);
    current[0] = std::byte{0xcd};
    assert(!sp::layout::decode(current)); // U omitted: planar is not a choice.
    auto geometry = wire;
    geometry[0] = std::byte{0x61};
    const auto old_geometry = *sc::layout::decode(geometry);
    const auto new_geometry = *sc::layout::prepare(old_geometry.schema());
    assert(old_geometry.width_bytes() == 2 && new_geometry.width_bytes() == 1);
    assert(old_geometry.encode() == geometry);
    geometry[0] = std::byte{0x81};
    assert(new_geometry.encode() == geometry);
}
void literal_planar_relative() {
    // New v3 bytes are independently authored: R-first, u16 local ends,
    // u8 U, and a second checkpoint. Neither writer nor addressing helper
    // supplies the literal positions being checked.
    const std::array<std::byte, 8> descriptor{std::byte{0xc6}, std::byte{255}, std::byte{0},
                                              std::byte{0},    std::byte{255}, std::byte{0},
                                              std::byte{0},    std::byte{0}};
    const auto layout = sp::layout::decode(descriptor);
    assert(layout);
    assert(layout->width_bytes() == 2 && layout->used_bytes() == 1 && layout->used_offset() == 512);
    assert(layout->geometry_lengths_offset() == 0 && layout->lengths_bytes(17) == 544);
    fixture f(*layout, 18);
    auto word = [&](std::byte *p, std::uint16_t value) { std::memcpy(p, &value, 2); };
    word(f.directory.data() + 34, 16);
    word(f.control.data(), 17);
    const std::uint32_t extent = 18;
    std::memcpy(f.control.data() + 2, &extent, 4);
    for (unsigned r = 0; r < 16; ++r) {
        word(f.lengths.data() + r * 2, r + 1);
        f.lengths[512 + r] = std::byte(r % 2 ? 0 : 1);
        f.payload[r] = std::byte(r + 3);
    }
    word(f.lengths.data() + 32, 2);
    f.lengths[528] = std::byte{1};
    f.payload[16] = std::byte{99};
    assert(f.source.validate());
    for (unsigned r = 0; r < 16; ++r) {
        const auto v = sp::value(f.source, r);
        assert(v && v->size() == (r % 2 ? 0 : 1));
        if (!v->empty())
            assert((*v)[0] == std::byte(r + 3));
    }
    assert(sp::value(f.source, 16)->size() == 1 && (*sp::value(f.source, 16))[0] == std::byte{99});
    assert(sp::reverse(f.source, 17)->rank == 16 && !sp::reverse(f.source, 18));
}

int main() {
    literal_planar_relative();
    unsigned cases = 0;
    for (auto family :
         {sp::geometry::absolute_ends, sp::geometry::sizes, sp::geometry::relative_ends})
        for (const auto [maximum, limit] :
             {std::pair{0u, 0u}, std::pair{15u, 3840u}, std::pair{255u, 255u},
              std::pair{255u, 65280u}, std::pair{4095u, 1048320u}, std::pair{4096u, 1048576u},
              std::pair{65535u, 65535u}, std::pair{65535u, 16776960u}})
            for (bool full : {false, true})
                for (auto placement : {sp::placement::grouped, sp::placement::separated})
                    for (unsigned n : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 128u, 255u, 256u}) {
                        history({family, placement, std::uint16_t(maximum), limit, {}, full, false},
                                n);
                        ++cases;
                    }
    generic_relative();
    legacy_conversion();
    for (auto family :
         {sp::geometry::absolute_ends, sp::geometry::sizes, sp::geometry::relative_ends})
        for (bool immortal : {false, true})
            for (bool full : {false, true})
                for (unsigned fixed : {0u, 8u})
                    history({family, sp::placement::grouped, 255, 65280, std::uint16_t(fixed), full,
                             immortal},
                            33);
    std::cout << cases
              << " encoding histories plus independent wide-origin relative searches passed\n";
}

static_assert(
    sizeof(bl::relative_end_source<std::uint16_t, std::uint64_t>::pending<bl::edge::after>) == 16);
static_assert(
    sizeof(bl::relative_end_source<std::uint32_t, std::uint64_t>::pending<bl::edge::after>) == 16);
