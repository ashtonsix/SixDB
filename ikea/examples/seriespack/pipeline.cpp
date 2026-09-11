#include <ikea/seriespack.h>
#include <ikea/seriespack/author/chain.h>
#include <cassert>

namespace sp = ikea::seriespack;
using F = sp::format<8>;
using Packet = sp::pipeline_packet<8, 32>;
using Plan = sp::packet_chain<8, 32>;

struct bindings {
    const sp::view<F>& source;
    std::uint8_t rank_cutoff;
    std::uint64_t sum = 0;
};

// Engine chooses signed-eight-bit interpretation. SeriesPack stores raw bits.
// This explicitly specialized body ranks -128..127 as unsigned 0..255.
Plan::result rank_signed(void*, std::size_t, std::uint64_t active, Packet values, unsigned) {
    for (auto& vector : values.v) {
#if defined(__aarch64__)
        vector = veorq_u8(vector, vdupq_n_u8(0x80));
#else
        vector = _mm_xor_si128(vector, _mm_set1_epi8(char(0x80)));
#endif
    }
    return {values, active, false};
}

// Reduction consumes ranks here only to make the example observable. A schema
// must not mistake a sum of ranks for an aggregate of the original signed values.
Plan::result selected_rank_sum(void* context, std::size_t, std::uint64_t active, Packet values,
                               unsigned) {
    auto& bound = *static_cast<bindings*>(context);
    const auto accumulate = [&]<unsigned I>() {
        const auto value = values.get<I>();
        const auto keep = sp::native::less(value, bound.rank_cutoff, active >> (I * 16));
        bound.sum += sp::native::sum(value, keep).finish();
    };
    accumulate.template operator()<0>();
    accumulate.template operator()<1>();
    return {values, active, false};
}
void finished(void*, std::size_t, std::uint64_t, Packet) {}

Packet load(const bindings& bound, std::size_t row) {
    Packet values;
    const auto& plane = bound.source.stream(0);
    values.set<0>(sp::native::read16<F, false>(plane.bytes.data(), plane.stride, row));
    values.set<1>(sp::native::read16<F, false>(plane.bytes.data(), plane.stride, row + 16));
    return values;
}

int main() {
    alignas(64) std::array<std::uint8_t, 64> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = std::uint8_t(i + 96);
    const auto source = sp::view<F>::attach(bytes.size(), {{{bytes, F::tile_bytes}, {}, {}}});
    assert(source);
    bindings direct{*source, 128}, continued{*source, 128};
    const std::array<Plan::function, 2> stages{&Plan::stage<&rank_signed>,
                                               &Plan::stage<&selected_rank_sum>};
    const auto plan = Plan::prepare(stages, &Plan::completion<&finished>);
    assert(plan);
    for (std::size_t row = 0; row < bytes.size(); row += 32) {
        constexpr std::uint64_t active = 0xffffffff;
        // Exactly the same body is directly inlined or wrapped in a CPS stage.
        const auto mapped = rank_signed(&direct, row, active, load(direct, row), 0);
        selected_rank_sum(&direct, row, mapped.active, mapped.values, 1);
        plan->run(&continued, row, active, load(continued, row));
    }
    std::uint64_t expected = 0;
    for (auto byte : bytes) {
        const auto rank = byte ^ 0x80;
        if (rank < 128)
            expected += rank;
    }
    assert(direct.sum == expected && continued.sum == expected);
}
