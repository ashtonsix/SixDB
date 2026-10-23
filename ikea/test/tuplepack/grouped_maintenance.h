#pragma once
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/maintenance.h>

// Both children mutate bits in the same byte; their packet orders differ. A
// separate grouped projection must see one complete before/after author::observation.
template <unsigned N> struct grouped_law {
    static constexpr bool needs_before = true, needs_after = true;
    unsigned calls = 0;
    tp::packet<N> old{}, next{};
    void observe_batch(std::size_t first, std::uint64_t active, tp::packet<N> before,
                       tp::packet<N> after) {
        require(first == 0 && active == 2, 2, 0, "grouped author::observation original coordinates");
        ++calls;
        old = before;
        next = after;
    }
};
template <unsigned N> void grouped_maintenance() {
    const auto f = *tp::layout::make(2, std::array<tp::code, 3>{
        tp::code{0, 0, 8}, tp::code{1, 0, 4}, tp::code{1, 4, 4}});
    const std::array<std::uint8_t, 3> rm{0, 1, 2}, wm{0, 1, tp::hole};
    const std::array<unsigned, 2> groups{2, 1};
    auto rp = *tp::read_plan<N, 2>::make(f, rm, groups);
    auto abp = *tp::write_plan<N, 2>::make(f, wm, groups);
    auto cp = *tp::write_plan<N, 2>::make(f, std::array<std::uint8_t, 1>{2});
    std::array<std::uint8_t, 4> bytes{5, 0xd2, 7, 0xe3};
    auto view = *tp::view::attach(f, std::as_writable_bytes(std::span{bytes}), 2, 2);
    auto read = *tp::bind_reader(rp, view);
    auto ab = *tp::bind_writer(abp, view), c = *tp::bind_writer(cp, view);
    const auto child = *tp::author::bind_group(ab);
    const auto parent = *tp::author::bind_group(child, c);
    grouped_law<N> law;
    auto observation = tp::author::observation(read, law);
    std::array<std::uint8_t, N> ab_input{}, c_input{}, before{}, after{};
    ab_input.fill(255); // Inactive and ignored slots, including explicit holes.
    c_input.fill(255);
    ab_input[2] = 29;
    ab_input[3] = 6;
    c_input[1] = 9;
    before[2] = 7; before[3] = 3; before[5] = 14;
    after[2] = 29; after[3] = 6; after[5] = 9;
    std::array<ikea::journal::entry, 4> entries;
    ikea::journal effects{entries};
    require(bool(parent.set(0, {{buffered<N>(ab_input)}, buffered<N>(c_input)}, effects, 2,
                            observation)), 2, 0, "grouped shared-byte composition");
    require(bytes == std::array<std::uint8_t, 4>{5, 0xd2, 29, 0x96} && law.calls == 1 &&
                law.old == buffered<N>(before) && law.next == buffered<N>(after),
            2, 0, "complete grouped dependencies after both children");
    for (unsigned i = 0; i < effects.used; ++i)
        require(entries[i].source == &view && entries[i].offset >= 2 &&
                    entries[i].offset + entries[i].size <= 4,
                2, 0, "grouping leaves byte coverage in original owner coordinates");
}
