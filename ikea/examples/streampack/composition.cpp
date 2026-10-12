#include <array>
#include <cassert>
#include <ikea/streampack.h>
#include <ikea/streampack/author/chain.h>

namespace sp = ikea::streampack;
#if defined(__aarch64__) || defined(__AVX2__)
using point = sp::point_chain<4>;
using batch = sp::chain<4>;
template <class Access> struct state {
    Access source;
    unsigned length = 0, batches = 0;
};
template <class Access>
point::result locate(void* raw, std::size_t rank, bool active, sp::pipeline_location, unsigned) {
    auto& s = *static_cast<state<Access>*>(raw);
    const auto position = s.source.locate(rank);
    return {sp::pipeline_location::from(position), active, false};
}
template <class Access> void consume(void* raw, std::size_t, bool active, sp::pipeline_location p) {
    auto& s = *static_cast<state<Access>*>(raw);
    if (active) {
        auto value = s.source.source().windows().payload.subspan(p.offset(), p.length());
        assert(value[0] == std::byte{7});
        s.length = value.size();
    }
}
template <class Access>
batch::result decode(void* raw, std::size_t first, std::uint16_t active, sp::native::packet,
                     unsigned) {
    auto& s = *static_cast<state<Access>*>(raw);
    const auto decoded = sp::native::read16(s.source, first / 16);
    return {decoded.values, std::uint16_t(active & decoded.live), false};
}
template <class Access>
void lengths(void* raw, std::size_t, std::uint16_t active, sp::native::packet p) {
    auto& s = *static_cast<state<Access>*>(raw);
    std::array<unsigned, 16> result;
    sp::native::each_chunk(
        [&](auto i) { sp::native::store(result.data() + i * sp::native::lanes, p.lengths(i)); });
    for (unsigned i = 0; i < 16; ++i)
        if (active & (1u << i)) {
            assert(result[i] == i + 1);
            ++s.batches;
        }
}
#endif
int main() {
#if defined(__aarch64__) || defined(__AVX2__)
    const auto format = sp::layout::prepare({.maximum = 255, .byte_limit = 4096});
    assert(format);
    std::array<std::byte, 4096> payload;
    std::array<std::byte, 64> directory;
    alignas(64) std::array<std::byte, 256> widths;
    std::array<std::byte, 6> control;
    auto source = sp::view::bind(*format, {payload, directory, widths, control});
    assert(source);
    ikea::no_coverage effects;
    assert(sp::initialize(*source, effects));
    std::array<std::byte, 16> value;
    value.fill(std::byte{7});
    for (unsigned n = 1; n <= 16; ++n)
        assert(sp::append(*source, std::span(value).first(n), n + 1, effects));
    // Resolve the schema before creating the chain; native stages carry the
    // concrete reader and never re-dispatch a group through an aggregate return.
    sp::author::dispatch(*source, [&](auto reader) {
        using Access = decltype(reader);
        state<Access> owner{reader};
        const std::array<point::function, 1> point_stages{point::stage<locate<Access>>};
        const auto point_plan = point::prepare(point_stages, point::completion<consume<Access>>);
        assert(point_plan);
        point_plan->run(&owner, 3, true, {});
        assert(owner.length == 4);
        const std::array<batch::function, 1> batch_stages{batch::stage<decode<Access>>};
        const auto batch_plan = batch::prepare(batch_stages, batch::completion<lengths<Access>>);
        assert(batch_plan);
        batch_plan->run(&owner, 0, 0xffff, {});
        assert(owner.batches == 16);
    });
#endif
}
