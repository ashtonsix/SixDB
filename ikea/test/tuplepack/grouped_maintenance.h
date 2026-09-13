#pragma once
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/maintenance.h>

// Both children mutate bits in the same byte; their packet orders differ. A
// separate grouped projection must see one complete before/after observation.
template <unsigned N> struct grouped_law {
    static constexpr bool needs_before = true, needs_after = true;
    unsigned calls = 0;
    tp::packet<N> old{}, next{};
    void observe_batch(std::size_t first, std::uint64_t active, tp::packet<N> before,
                       tp::packet<N> after) {
        require(first == 0 && active == 2, 2, 0, "grouped observation original coordinates");
        ++calls;
        old = before;
        next = after;
    }
};
template <unsigned N> void grouped_maintenance() {
    const auto f = *tp::layout::make(2, std::array<tp::code, 3>{
        tp::code{0, 0, 8}, tp::code{1, 0, 4}, tp::code{1, 4, 4}});
    const std::array<tp::byte, 3> rm{0, 1, 2}, wm{0, 1, tp::hole};
    const std::array<unsigned, 2> groups{2, 1};
    auto rp = *tp::reader<N, 2>::make(f, rm, groups);
    auto abp = *tp::writer<N, 2>::make(f, wm, groups);
    auto cp = *tp::writer<N, 2>::make(f, std::array<tp::byte, 1>{2});
    std::array<tp::byte, 4> bytes{5, 0xd2, 7, 0xe3};
    auto view = *tp::view::bind(f, bytes, 2, 2);
    auto read = *tp::bind_reader(rp, view);
    auto ab = *tp::bind_writer(abp, view), c = *tp::bind_writer(cp, view);
    const auto child = *tp::composition::bind_group(ab);
    const auto parent = *tp::composition::bind_group(child, c);
    grouped_law<N> law;
    auto observation = tp::observation(read, law);
    std::array<tp::byte, N> ab_input{}, c_input{}, before{}, after{};
    ab_input.fill(255); // Inactive and ignored slots, including explicit holes.
    c_input.fill(255);
    ab_input[2] = 29;
    ab_input[3] = 6;
    c_input[1] = 9;
    before[2] = 7; before[3] = 3; before[5] = 14;
    after[2] = 29; after[3] = 6; after[5] = 9;
    std::array<ikea::owner_write, 4> entries;
    ikea::source_write_journal effects{entries};
    require(bool(parent.set(0, {{buffered<N>(ab_input)}, buffered<N>(c_input)}, effects, 2,
                            observation)), 2, 0, "grouped shared-byte composition");
    require(bytes == std::array<tp::byte, 4>{5, 0xd2, 29, 0x96} && law.calls == 1 &&
                law.old == buffered<N>(before) && law.next == buffered<N>(after),
            2, 0, "complete grouped dependencies after both children");
    for (unsigned i = 0; i < effects.used; ++i)
        require(entries[i].source == &view && entries[i].bytes.offset >= 2 &&
                    entries[i].bytes.offset + entries[i].bytes.size <= 4,
                2, 0, "grouping leaves byte coverage in original owner coordinates");
}
