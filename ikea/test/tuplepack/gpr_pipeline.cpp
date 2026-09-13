#include <cassert>
#include <ikea/tuplepack/author/execution.h>
namespace tp = ikea::tuplepack;
namespace {
using chain = tp::packet_chain<8, 8>;
using values = tp::native::pipeline_values_for<8>;
struct state {
    tp::native_reader<8, tp::byte, 2> read;
    tp::native_writer<8, 2> write;
    ikea::source_write_journal &effects;
    unsigned completed = 0, stages = 0;
    std::uint64_t returned = 0;
    bool stop = false;
};
chain::result decode(void *p, std::size_t first, std::uint64_t active, values, unsigned ordinal) {
    auto &s = *static_cast<state *>(p);
    assert(ordinal == 0);
    ++s.stages;
    return {values::from(s.read.get_unchecked(first, active)), active, s.stop};
}
chain::result update(void *p, std::size_t first, std::uint64_t active, values v, unsigned ordinal) {
    auto &s = *static_cast<state *>(p);
    assert(ordinal == 1);
    ++s.stages;
    v.v[0] ^= 0x0101010101010101ull;
    assert(s.write.set(first, v.get(), s.effects, active));
    return {v, active};
}
void complete(void *p, std::size_t, std::uint64_t, values v) {
    auto &s = *static_cast<state *>(p);
    ++s.completed;
    s.returned = v.get();
}
} // namespace
void pipeline_check() {
    std::array<tp::code, 4> codes{{{0, 1, 7}, {1, 1, 7}, {2, 1, 7}, {3, 1, 7}}};
    const std::array<tp::byte, 4> map{0, 1, 2, 3};
    auto f = *tp::layout::make(4, codes);
    const std::array<unsigned, 2> groups{2, 2};
    auto rp = *tp::reader<8, 2>::make(f, map, groups);
    auto wp = *tp::writer<8, 2>::make(f, map, groups);
    std::array<tp::byte, 8> bytes;
    auto view = *tp::view::bind(f, bytes, 2, 4);
    auto read = *tp::bind_reader(rp, view);
    auto write = *tp::bind_writer(wp, view);
    std::array<ikea::owner_write, 2> storage;
    ikea::source_write_journal effects{storage};
    state s{tp::native_reader(read), tp::native_writer(write), effects};
    const std::array<chain::function, 2> stages{chain::stage<decode>, chain::stage<update>};
    auto pipeline = *chain::prepare(stages, chain::completion<complete>);
    for (bool cps : {false, true})
        for (bool stop : {false, true})
            for (auto active : {0ull, 1ull, 2ull, 3ull}) {
                bytes.fill(0x25);
                effects.used = 0;
                s.stop = stop;
                s.stages = s.completed = 0;
                if (cps)
                    pipeline.run(&s, 0, active, values{});
                else {
                    auto v = decode(&s, 0, active, values{}, 0);
                    if (!v.stop)
                        v = update(&s, 0, active, v.values, 1);
                    complete(&s, 0, active, v.values);
                }
                assert(s.completed == 1 && s.stages == (stop ? 1 : 2));
                const auto mask = tp::native::row_mask(rp, active);
                assert(s.returned ==
                       ((0x1212121212121212ull & mask) ^ (stop ? 0 : 0x0101010101010101ull)));
                for (unsigned i = 0; i < 8; ++i)
                    assert(bytes[i] == (!stop && (active & (1u << (i / 4))) ? 0x27 : 0x25));
            }
}
