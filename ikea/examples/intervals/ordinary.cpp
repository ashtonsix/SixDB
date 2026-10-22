#include <ikea/intervals.h>
#include <ikea/effects.h>
#include <array>
#include <cassert>

namespace in = ikea::intervals;
int main() {
    // The cut adapter borrows existing storage. Its count excludes the origin.
    const std::array cuts{0u, 8u, 18u, 26u, 35u, 46u, 56u};
    const auto geometry = in::from_cuts(std::span(cuts));
    const auto bounds = in::locate(geometry, 2);
    assert(geometry.size() == 6 && bounds && bounds->first == 18 && bounds->limit == 26);
    assert(in::reverse(geometry, 18u) == 2 && in::reverse(geometry, 56u) == 6);
    const auto window = in::overlap(geometry, 9u, 36u);
    assert((window == in::rank_window{1, 5}));
    const std::array eligible{std::byte{0x3b}}; // rank 2 excluded
    const in::selected_source selected{geometry, in::rank_selection{eligible}};
    assert(in::reverse(selected, 20u) == 6 && in::reverse(selected, 26u) == 3);
    unsigned rank_sum = 0;
    assert(in::range(selected, window.first, window.limit - window.first,
                     [&](unsigned rank, auto) { rank_sum += rank; }));
    assert(rank_sum == 1 + 3 + 4);
    assert(in::lower_bound(geometry, 18u) == 1 && in::upper_bound(geometry, 18u) == 2);

    // A borrowed editor has no format header or bitmap. The owner supplies
    // initialized empty controls, storage, disjointness and write authority.
    std::array<std::uint8_t, 64> widths{};
    std::array<std::uint32_t, 1> checkpoints{};
    unsigned assigned = 0;
    std::uint32_t extent = 0;
    const in::size_editor<64, std::uint8_t> editor{
        widths, checkpoints, assigned, extent, 0, 4096, 255};
    assert(editor.validate());
    std::array<ikea::owner_write, 32> records;
    ikea::source_write_journal effects{records};
    assert(editor.append(3, effects) && editor.append(0, effects) && editor.append(5, effects));
    assert(editor.validate());
    const auto source = editor.source();
    assert(in::reverse(source, 3) == 2); // Zero-size rank 1 retains its identity.
    const std::array live{std::byte{0b011}};
    assert(in::reverse(source, 3, in::rank_selection{live}) == 3); // Owner excluded.
    unsigned visited = 0;
    assert(in::range(source, 0, 3, [&](unsigned, auto) { ++visited; }));
    assert(visited == 3);
    assert(editor.resize(0, 4, effects)); // Ends [3,3,8] become [4,4,9].
    assert(editor.validate() && extent == 9 && editor.source().at(2) == 9);
    for (const auto &record : effects.entries()) assert(record.source == &editor);

    // Immutable end-minus-one bytes retain the logical endpoint 256.
    const std::array<std::uint8_t, 3> ends_minus_one{0, 3, 255};
    const in::run_end_source runs{ends_minus_one.data(), 3};
    assert(in::reverse(runs, 255) == 2 && runs.extent() == 256);
}
