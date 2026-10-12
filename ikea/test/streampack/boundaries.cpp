#include <array>
#include <cassert>
#include <cstring>
#include <ikea/streampack.h>
#include <ikea/streampack/author/native.h>
#include <iostream>
#include <memory>
#include <new>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace sp = ikea::streampack;
using sp::byte;
struct guarded {
    byte* allocation;
    std::size_t page, size;
    explicit guarded(unsigned n) : page(sysconf(_SC_PAGESIZE)), size(n) {
        assert(n <= page);
        allocation = static_cast<byte*>(
            mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        assert(allocation != MAP_FAILED && !mprotect(allocation + page, page, PROT_NONE));
    }
    ~guarded() {
        munmap(allocation, 2 * page);
    }
    std::span<byte> bytes() {
        return {allocation + page - size, size};
    }
};
void tails(sp::geometry family) {
    auto l = *sp::layout::prepare(
        {family, sp::placement::grouped, 128, 65535, 128 * 65535, {}, false, false});
    guarded directory(l.directory_bytes()), lengths(l.lengths_bytes(1)), control(l.control_bytes());
    std::vector<byte> payload(16 * 65535);
    auto v = *sp::view::bind(l, {payload, directory.bytes(), lengths.bytes(), control.bytes()});
    std::vector<byte> maximal(65535, byte{0x91});
    ikea::no_coverage effects;
    assert(sp::initialize(v, effects));
    for (unsigned n = 1; n <= 16; ++n) {
        assert(sp::append(v, maximal, 65535, effects));
        assert(v.extent() == n * 65535 && v.validate());
        for (unsigned r = 0; r < n; ++r) {
            assert(sp::value(v, r)->size() == 65535);
            for (auto at : {r * 65535, (r + 1) * 65535 - 1})
                assert(sp::reverse(v, at)->rank == r);
        }
#if defined(__aarch64__) || defined(__AVX2__)
        sp::author::dispatch(v, [&](auto a) {
            const auto group = sp::native::read16(a, 0);
            std::array<unsigned, 16> offsets;
            sp::native::each_chunk([&](auto i) {
                sp::native::store(offsets.data() + i * sp::native::lanes, group.values.offsets(i));
            });
            for (unsigned r = 0; r < n; ++r)
                assert(offsets[r] == r * 65535);
        });
#endif
    }
    auto before = payload;
    assert(!sp::append(v, {}, 0, effects)); // Group 2 has no admitted length storage.
    assert(v.size() == 16 && before == payload);
}
void limits(unsigned maximum, unsigned limit, sp::geometry family) {
    auto l = *sp::layout::prepare(
        {family, sp::placement::grouped, 128, std::uint16_t(maximum), limit, {}, false, false});
    alignas(64) std::array<byte, 4096> directory{}, lengths{};
    std::array<byte, 6> control{};
    std::vector<byte> payload(limit + 8, byte{0x72});
    auto v = *sp::view::bind(l, {payload, std::span(directory).first(l.directory_bytes()),
                                 std::span(lengths).first(l.lengths_bytes()),
                                 std::span(control).first(l.control_bytes())});
    std::array<ikea::owner_write, 32> records;
    ikea::source_write_journal journal{records};
    assert(sp::initialize(v, journal));
    journal.used = 0;
    auto remaining = limit;
    while (remaining) {
        const auto size = std::min(remaining, maximum);
        std::vector<byte> input(size, byte{0xb6});
        assert(sp::append(v, input, size, journal));
        journal.used = 0;
        remaining -= size;
    }
    assert(v.extent() == limit && v.validate());
    const auto old_payload = payload;
    const auto old_dir = directory;
    const auto old_lengths = lengths;
    const auto old_control = control;
    const std::array input{byte{3}};
    assert(!sp::append(v, input, 1, journal));
    assert(payload == old_payload && directory == old_dir && lengths == old_lengths &&
           control == old_control && journal.used == 0);
    // Even an identical payload store must publish damage; no-space refusal must
    // precede it. A one-slot journal cannot cover both payload and used metadata.
    ikea::source_write_journal short_journal{std::span(records).first(1)};
    const auto unchanged_input = *sp::value(v, 0);
    assert(!sp::replace(v, 0, unchanged_input, short_journal));
    assert(!short_journal.used && payload == old_payload && directory == old_dir &&
           lengths == old_lengths && control == old_control);
    assert(sp::replace(v, 0, unchanged_input, journal));
    assert(journal.used == 2 && journal.entries()[0].source == &v &&
           journal.entries()[0].bytes.plane == 0);
    journal.used = 0;
    // Input inside metadata is rejected before any store or journal entry.
    assert(!sp::replace(v, 0, std::span<const byte>(control).first(1), journal));
    assert(payload == old_payload && directory == old_dir && lengths == old_lengths &&
           control == old_control && journal.used == 0);
}
void zero_and_extent() {
    auto l = *sp::layout::prepare(
        {sp::geometry::width, sp::placement::grouped, 128, 0, 0, {}, true, true});
    std::array<byte, 2> control{};
    auto v = *sp::view::bind(l, {{}, {}, {}, control});
    std::array<ikea::owner_write, 1> records;
    ikea::source_write_journal effects{records};
    assert(sp::initialize(v, effects));
    effects.used = 0;
    assert(sp::append(v, {}, 0, effects));
    assert(effects.used == 1 && effects.entries()[0].bytes.plane == unsigned(sp::plane::control));
    assert(v.size() == 1 && v.extent() == 0 && sp::value(v, 0)->empty() && v.validate());
    assert(!sp::retire(v, 0, effects));

    auto width = *sp::layout::prepare(
        {sp::geometry::width, sp::placement::grouped, 128, 255, 128 * 255, {}, false, false});
    std::array<byte, 16> body;
    std::array<byte, 32> directory;
    alignas(64) std::array<byte, 32> lengths;
    std::array<byte, 6> wcontrol;
    auto w = *sp::view::bind(width, {body, directory, lengths, wcontrol});
    ikea::no_coverage private_write;
    assert(sp::initialize(w, private_write));
    const std::array data{byte{1}, byte{2}};
    assert(sp::append(w, data, 8, private_write));
    const auto checkpoint_before = directory;
    std::array<ikea::owner_write, 16> changes;
    ikea::source_write_journal log{changes};
    assert(sp::soft_pack(w, true, log));
    assert(w.extent() == 2 && w.size() == 1 && directory == checkpoint_before);
    bool extent_written = false;
    for (auto e : log.entries())
        if (e.bytes.plane == unsigned(sp::plane::control)) {
            assert(e.bytes.offset == 2 && e.bytes.size == 4);
            extent_written = true;
        }
    assert(extent_written && w.validate());
}
void transfer_effect_alias() {
    const auto l = *sp::layout::prepare(
        {sp::geometry::direct, sp::placement::grouped, 128, 255, 32640, {}, false, false});
    alignas(64) std::array<byte, 1024> body{}, dbody{};
    alignas(64) std::array<byte, 4096> directory{}, lengths{}, ddirectory{}, dlengths{};
    std::array<byte, 2> control{}, dcontrol{};
    auto src = *sp::view::bind(l, {body, directory, lengths, control});
    auto dst = *sp::view::bind(l, {dbody, ddirectory, dlengths, dcontrol});
    ikea::no_coverage no_effects;
    assert(sp::initialize(src, no_effects));
    const std::array value{byte{8}};
    assert(sp::append(src, value, 1, no_effects));
    auto* record = std::construct_at(reinterpret_cast<ikea::owner_write*>(body.data() + 512));
    ikea::source_write_journal effects{{record, 1}};
    const auto old_source = body, old_destination = dbody;
    const auto old_dir = ddirectory, old_len = dlengths;
    const auto old_control = dcontrol;
    const std::array<std::uint16_t, 1> ranks{0};
    auto result = sp::transfer(src, ranks, dst, true, effects);
    assert(!result && result.error() == sp::error::alias);
    assert(!effects.used && body == old_source && dbody == old_destination &&
           ddirectory == old_dir && dlengths == old_len && dcontrol == old_control);
    std::destroy_at(record);
}
// A portable byte fixture is independent of the production writer/reader pair.
// Seventeen slots cross a group boundary; zero-width ranks and a retired value
// exercise checkpoint, U, D and partial-tail laws without a terminal sentinel.
void wire_fixture(sp::geometry family) {
    const auto format =
        *sp::layout::prepare({family, sp::placement::grouped, 128, 255, 255, {}, false, false});
    std::array<byte, 16> encoded{byte{1},   byte(family), byte{0},   byte{0},  byte{128}, byte{0},
                                 byte{255}, byte{0},      byte{255}, byte{0},  byte{0},   byte{0},
                                 byte{255}, byte{255},    byte{255}, byte{255}};
    assert(format.encode() == encoded && sp::layout::decode(encoded));
    encoded[3] = byte{4};
    assert(!sp::layout::decode(encoded));

    std::array<byte, 9> body{byte{'a'}, byte{'b'}, byte{0},   byte{'c'}, byte{0},
                             byte{'d'}, byte{'e'}, byte{'f'}, byte{0}};
    std::vector<byte> directory(family == sp::geometry::width ? 24 : 144);
    alignas(64) std::array<byte, 64> lengths{};
    std::array<byte, 6> control{byte{17}, byte{0}, byte{9}};
    directory[0] = byte{4}; // Rank two is retired; its reservation remains.
    if (family == sp::geometry::width) {
        directory[17] = byte{5}; // Checkpoint for group one, after sixteen ranks.
        lengths[0] = byte{3};
        lengths[2] = byte{2};
        lengths[16] = byte{2};
        lengths[18] = byte{1};
        lengths[32] = byte{4};
        lengths[48] = byte{3};
    } else {
        directory[16] = directory[17] = byte{3};
        std::fill(directory.begin() + 18, directory.begin() + 32, byte{5});
        directory[32] = byte{9};
        lengths[0] = byte{2};
        lengths[2] = byte{1};
        lengths[16] = byte{3};
    }
    auto v = *sp::view::bind(
        format, {body, directory, std::span(lengths).first(family == sp::geometry::width ? 64 : 32),
                 std::span(control).first(family == sp::geometry::width ? 6 : 2)});
    assert(v.validate() && v.size() == 17 && v.extent() == 9);
    assert(sp::value(v, 0)->size() == 2 && sp::value(v, 1)->empty());
    assert(!sp::value(v, 2) && sp::value(v, 16)->size() == 3);
    assert(sp::reverse(v, 4)->rank == 2 && sp::reverse(v, 4)->tombstone);
    assert(sp::reverse(v, 5)->rank == 16);
    const auto golden_body = body;
    const auto golden_lengths = lengths;
    const auto golden_control = control;
    const auto golden_directory = directory;
    ikea::no_coverage effects;
    assert(sp::initialize(v, effects));
    const std::array<byte, 3> value{byte{'d'}, byte{'e'}, byte{'f'}};
    for (unsigned rank = 0; rank < 17; ++rank) {
        if (rank == 0)
            assert(sp::append(v, std::span(golden_body).first(2), 3, effects));
        else if (rank == 2)
            assert(sp::append(v, std::span(golden_body).subspan(3, 1), 2, effects));
        else if (rank == 16)
            assert(sp::append(v, value, 4, effects));
        else
            assert(sp::append(v, {}, 0, effects));
    }
    assert(sp::retire(v, 2, effects));
    assert(body == golden_body && directory == golden_directory && lengths == golden_lengths &&
           control == golden_control);
    // Cold admission must catch corrupt geometry, length and count independently.
    directory[17] = byte{2}; // Wrong group checkpoint / descending endpoint.
    assert(!v.validate());
    std::copy(golden_directory.begin(), golden_directory.end(), directory.begin());
    lengths[family == sp::geometry::width ? 16 : 0] = byte{4};
    assert(!v.validate());
    lengths = golden_lengths;
    control[0] = byte{129};
    assert(!v.validate());
}

int main() {
    wire_fixture(sp::geometry::width);
    wire_fixture(sp::geometry::direct);
    // Omitted byte limit follows the selected capacity and reservation maximum.
    // Designated schema construction must not accidentally inherit a 16 MiB cap.
    const auto automatic = sp::layout::prepare({.capacity = 128, .maximum = 255});
    assert(automatic && automatic->schema().byte_limit == 128 * 255);
    const auto empty = sp::layout::prepare({.capacity = 512, .maximum = 0});
    assert(empty && !empty->schema().byte_limit && !empty->lengths_bytes());
    for (auto family : {sp::geometry::direct, sp::geometry::width}) {
        tails(family);
        for (auto bound : {255u, 256u, 4095u, 65535u, 65536u})
            limits(std::min(bound, 65535u), bound, family);
    }
    zero_and_extent();
    transfer_effect_alias();
    std::cout << "StreamPack exact tails, maximal widths, byte limits, refusal and control effects "
                 "passed\n";
}
