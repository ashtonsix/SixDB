#include <cassert>
#include <cstdio>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>

namespace tp = ikea::tuplepack;
#if defined(__aarch64__) || defined(__AVX2__)
namespace {
constexpr unsigned rows = 32;
using pipeline = tp::chain<8>;
struct context {
    const tp::native_reader<64, tp::byte, rows> &read;
    const tp::native_writer<64, rows> &write;
    ikea::source_write_journal &effects;
    bool okay = true;
    std::size_t completed = 0;
};
[[gnu::always_inline]] inline tp::native::packet change(tp::native::packet v) {
    // The caller's computation: replace each projected code with 1 - (code&1).
#if defined(__aarch64__)
    auto one = vdupq_n_u8(1);
    return {veorq_u8(vandq_u8(v.a, one), one), veorq_u8(vandq_u8(v.b, one), one),
            veorq_u8(vandq_u8(v.c, one), one), veorq_u8(vandq_u8(v.d, one), one)};
#elif defined(__AVX512VBMI__)
    auto one = _mm512_set1_epi8(1);
    return _mm512_xor_si512(_mm512_and_si512(v, one), one);
#else
    auto one = _mm256_set1_epi8(1);
    return {_mm256_xor_si256(_mm256_and_si256(v.a, one), one),
            _mm256_xor_si256(_mm256_and_si256(v.b, one), one)};
#endif
}
[[gnu::always_inline]] inline pipeline::result decode(void *opaque, std::size_t first,
                                                      std::uint64_t active,
                                                      tp::native::pipeline_values, unsigned) {
    auto &c = *static_cast<context *>(opaque);
    return {tp::native::pipeline_values::from(c.read.get_unchecked(first, active)), active,
            !active};
}
[[gnu::always_inline]] inline pipeline::result update(void *opaque, std::size_t first,
                                                      std::uint64_t active,
                                                      tp::native::pipeline_values values,
                                                      unsigned) {
    auto &c = *static_cast<context *>(opaque);
    c.okay = bool(c.write.set(first, change(values.get()), c.effects, active));
    return {values, active, !c.okay};
}
void complete(void *opaque, std::size_t first, std::uint64_t, tp::native::pipeline_values) {
    auto &c = *static_cast<context *>(opaque);
    if (c.okay)
        c.completed = std::min(c.write.size(), first + rows);
}
} // namespace
#endif
int main() {
#if defined(__aarch64__) || defined(__AVX2__)
    // Three codes share one physical byte. Each operation projects/replaces
    // two of them: 32 physical bytes become 64 code bytes held in registers.
    std::array<tp::code, 3> codes{{{0, 0, 3}, {0, 3, 2}, {0, 5, 3}}};
    auto format = tp::layout::make(1, codes);
    assert(format);
    std::array<tp::byte, 2> map{0, 1};
    // One byte-code group at a time: all first codes, then all second codes.
    // The same reader/writer interface defaults to interleaved rows.
    const std::array<unsigned, 2> groups{1, 1};
    auto read_plan = tp::reader<64, rows>::make(*format, map, groups);
    assert(read_plan);
    auto write_plan = tp::writer<64, rows>::make(*format, map, groups);
    assert(write_plan);
    std::array<tp::byte, 65> bytes{}, initial{}, expected{};
    for (unsigned r = 0; r < bytes.size(); ++r) {
        initial[r] = expected[r] = tp::byte(r * 7);
        if (r < 32 || r % 2 == 0)
            expected[r] =
                (initial[r] & 0xe0) | ((initial[r] & 1) ^ 1) | ((((initial[r] >> 3) & 1) ^ 1) << 3);
    }
    auto source = tp::view::bind(*format, bytes, bytes.size(), 1);
    assert(source);
    auto bound_read = tp::bind_reader(*read_plan, *source);
    assert(bound_read);
    auto bound_write = tp::bind_writer(*write_plan, *source);
    assert(bound_write);
    const auto read = tp::native_reader(*bound_read);
    const auto write = tp::native_writer(*bound_write);
    std::array<ikea::owner_write, 65> entries{};
    ikea::source_write_journal effects{entries};
    context state{read, write, effects};
    std::array<pipeline::function, 2> stages{pipeline::stage<decode>, pipeline::stage<update>};
    auto chain = pipeline::prepare(stages, pipeline::completion<complete>);
    assert(chain);
    auto empty = tp::native::pipeline_values::from(tp::native::row_mask<rows>(0));
    for (bool cps : {false, true}) {
        bytes = initial;
        effects.used = 0;
        state.completed = 0;
        state.okay = true;
        for (std::size_t first = 0; first < bytes.size(); first += rows) {
            const std::uint64_t active = first == 0    ? 0xffffffffull
                                         : first == 32 ? 0x55555555ull
                                                       : 1;
            assert(read.admit(first, active));
            if (cps)
                chain->run(&state, first, active, empty);
            else {
                auto decoded = decode(&state, first, active, empty, 0);
                if (!decoded.stop)
                    update(&state, first, active, decoded.values, 1);
                complete(&state, first, active, empty);
            }
            assert(state.okay);
            // An owner can suspend/cancel here, retaining the named plans/view,
            // storage, effects and completed frontier. Publication is separate.
        }
        assert(bytes == expected && state.completed == bytes.size());
    }
    std::puts("TuplePack: one-byte tuples, shared inline/CPS packet update, sparse rows and a "
              "final tail passed");
#else
    std::puts("TuplePack native packet example requires NEON or AVX2");
#endif
}
