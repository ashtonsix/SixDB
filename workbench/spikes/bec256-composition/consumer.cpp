#include "consumer.h"
#include <bit>
#include <cstring>
#include <stdexcept>

namespace bec_study {
const char *name(execution value) {
    switch (value) {
    case execution::inline_body:
        return "inline";
    case execution::shared_body:
        return "shared";
    case execution::materialized:
        return "materialized";
    case execution::cps:
        return "cps";
    case execution::cps_fused:
        return "cps_fused";
    }
    std::abort();
}
bitset::bitset(std::span<const bc::byte> input)
    : plain(input.begin(), input.end()), body(input.size() / 32 * 47 + 64) {
    assert(input.size() % 32 == 0);
    unsigned offset = 0;
    for (unsigned i = 0; i < input.size() / 32; ++i) {
        unsigned population = 0;
        for (unsigned j = 0; j < 32; ++j)
            population += std::popcount(std::to_integer<unsigned>(input[i * 32 + j]));
        auto encoded =
            bc::prepare(std::span<const bc::byte, 32>(input.data() + i * 32, 32), population);
        assert(encoded);
        catalog.push_back({population, encoded->bytes(), offset});
        if (encoded->bytes())
            std::memcpy(body.data() + offset, encoded->body().data(), encoded->bytes());
        offset += encoded->bytes();
    }
    body.resize(offset + 64);
}
namespace {
entry read_entry(const directory &d, unsigned i) {
    switch (d.kind()) {
    case layout::direct:
        return point<layout::direct>(d, i);
    case layout::direct32:
        return point<layout::direct32>(d, i);
    case layout::tuple_absolute:
        return point<layout::tuple_absolute>(d, i);
    case layout::tuple_checkpoint:
        return point<layout::tuple_checkpoint>(d, i);
    case layout::series_local:
        return point<layout::series_local>(d, i);
    case layout::series_scan:
        return point<layout::series_scan>(d, i);
    case layout::tuple_folded:
        return point<layout::tuple_folded>(d, i);
    case layout::series_folded_local:
        return point<layout::series_folded_local>(d, i);
    case layout::series_folded_scan:
        return point<layout::series_folded_scan>(d, i);
    }
    std::abort();
}
} // namespace
void bitset::admit(const directory &d) const {
    assert(d.size() == size());
    for (unsigned i = 0; i < size(); ++i) {
        const auto entry = read_entry(d, i);
        assert(entry.offset <= body.size() && body.size() - entry.offset >= 64);
        assert(bc::validate({body.data() + entry.offset, entry.bytes}, entry.population));
        assert(entry.population == catalog[i].population && entry.bytes == catalog[i].bytes &&
               entry.offset == catalog[i].offset);
    }
}
std::uint64_t count_reference(const bitset &bits, const bc::byte *query, request range) {
    std::uint64_t count = 0;
    for (unsigned i = range.first; i < range.first + range.count; i += range.every)
        if (!range.active || ((range.active[i / 64] >> (i % 64)) & 1))
            for (unsigned j = 0; j < 32; ++j)
                count += std::popcount(
                    std::to_integer<unsigned>(bits.plain[i * 32 + j] & query[i * 32 + j]));
    return count;
}
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
[[gnu::always_inline]] static bc::native::pair inline_pair_bits(const bc::byte *a, unsigned pa,
                                                                const bc::byte *b, unsigned pb,
                                                                const bc::byte *qa,
                                                                const bc::byte *qb) {
    auto values = bc::native::decode_pair_unchecked(a, pa, b, pb);
    return bc::native::intersection(values,
                                    bc::native::join(bc::native::load(qa), bc::native::load(qb)));
}
[[gnu::always_inline]] static std::uint64_t inline_pair(const bc::byte *a, unsigned pa,
                                                        const bc::byte *b, unsigned pb,
                                                        const bc::byte *qa, const bc::byte *qb) {
    return bc::native::population(inline_pair_bits(a, pa, b, pb, qa, qb));
}
__attribute__((noinline)) std::uint64_t consume_pair(const bc::byte *a, unsigned pa,
                                                     const bc::byte *b, unsigned pb,
                                                     const bc::byte *qa,
                                                     const bc::byte *qb) noexcept {
    return inline_pair(a, pa, b, pb, qa, qb);
}
struct pipeline_context {
    const bc::byte *a, *b, *qa, *qb;
    unsigned pa, pb;
    std::uint64_t count;
};
using chain = bc::chain<4>;
static chain::result decode_stage(void *raw, std::size_t, std::uint64_t active, bc::pipeline_bits,
                                  unsigned) {
    auto &c = *static_cast<pipeline_context *>(raw);
    return {bc::pipeline_bits::from(bc::native::decode_pair_unchecked(c.a, c.pa, c.b, c.pb)),
            active, false};
}
static chain::result intersect_stage(void *raw, std::size_t, std::uint64_t active,
                                     bc::pipeline_bits bits, unsigned) {
    auto &c = *static_cast<pipeline_context *>(raw);
    return {bc::pipeline_bits::from(bc::native::intersection(
                bits.get(), bc::native::join(bc::native::load(c.qa), bc::native::load(c.qb)))),
            active, false};
}
static void finish_stage(void *raw, std::size_t, std::uint64_t, bc::pipeline_bits bits) {
    static_cast<pipeline_context *>(raw)->count = bc::native::population(bits.get());
}
// Plans belong to preparation, not a per-pair lazy-initialization guard.
const auto pair_pipeline = *chain::prepare(
    std::array<chain::function, 2>{chain::stage<decode_stage>, chain::stage<intersect_stage>},
    chain::completion<finish_stage>);
using fused_chain = bc::chain<2>;
static fused_chain::result filter_stage(void *raw, std::size_t, std::uint64_t active,
                                        bc::pipeline_bits, unsigned) {
    auto &c = *static_cast<pipeline_context *>(raw);
    return {bc::pipeline_bits::from(inline_pair_bits(c.a, c.pa, c.b, c.pb, c.qa, c.qb)), active,
            false};
}
const auto fused_pair_pipeline = *fused_chain::prepare(
    std::array<fused_chain::function, 1>{fused_chain::stage<filter_stage>},
    fused_chain::completion<finish_stage>);
template <execution E>
[[gnu::always_inline]] std::uint64_t pair(const bc::byte *a, unsigned pa, const bc::byte *b,
                                          unsigned pb, const bc::byte *qa, const bc::byte *qb) {
    if constexpr (E == execution::inline_body)
        return inline_pair(a, pa, b, pb, qa, qb);
    if constexpr (E == execution::shared_body)
        return consume_pair(a, pa, b, pb, qa, qb);
    if constexpr (E == execution::cps || E == execution::cps_fused) {
        pipeline_context context{a, b, qa, qb, pa, pb, 0};
        if constexpr (E == execution::cps)
            pair_pipeline.run(&context, 0, 3, {});
        else
            fused_pair_pipeline.run(&context, 0, 3, {});
        return context.count;
    }
    if constexpr (E == execution::materialized) {
        alignas(64) bc::byte values[64];
        bc::native::store_pair(values, bc::native::decode_pair_unchecked(a, pa, b, pb));
        // Force the comparison's intentional buffered seam.
        asm volatile("" : : "r"(values) : "memory");
        return bc::native::population(
            bc::native::intersection(bc::native::load_pair(values),
                                     bc::native::join(bc::native::load(qa), bc::native::load(qb))));
    }
}
template <layout L, resolution R, execution E>
std::uint64_t count(const directory &d, const bitset &bits, const bc::byte *query, request range) {
    cursor<L, R> metadata(d);
    std::uint64_t result = 0;
    const unsigned end = range.first + range.count;
    unsigned pending_index = end;
    entry pending{};
    // One forward traversal retains at most one nonterminal child. Keeping the
    // metadata refill here avoids a per-child iterator call or duplicated
    // lookahead body, and preserves original ordinals under arbitrary masks.
    for (unsigned i = range.first; i < end; i += range.every) {
        if (range.active && !((range.active[i / 64] >> (i % 64)) & 1))
            continue;
        const auto current = metadata.get(i);
        if (current.population == 0)
            continue;
        if (current.population == 256) {
            result += bc::native::population(bc::native::load(query + i * 32));
            continue;
        }
        if (pending_index == end) {
            pending = current;
            pending_index = i;
            continue;
        }
        result += pair<E>(bits.body.data() + pending.offset, pending.population,
                          bits.body.data() + current.offset, current.population,
                          query + pending_index * 32, query + i * 32);
        pending_index = end;
    }
    if (pending_index != end) {
        auto values =
            bc::native::decode_unchecked(bits.body.data() + pending.offset, pending.population);
        alignas(32) static constexpr bc::byte zeros[32]{};
        result += bc::native::population(
            bc::native::intersection(bc::native::join(values, bc::native::load(zeros)),
                                     bc::native::join(bc::native::load(query + pending_index * 32),
                                                      bc::native::load(zeros))));
    }
    return result;
}
template <layout L, resolution R> counter choose_execution(execution e) {
    switch (e) {
    case execution::inline_body:
        return count<L, R, execution::inline_body>;
    case execution::shared_body:
        return count<L, R, execution::shared_body>;
    case execution::materialized:
        return count<L, R, execution::materialized>;
    case execution::cps:
        return count<L, R, execution::cps>;
    case execution::cps_fused:
        return count<L, R, execution::cps_fused>;
    }
    std::abort();
}
template <layout L> counter choose_resolution(resolution r, execution e) {
    switch (r) {
    case resolution::point:
        return choose_execution<L, resolution::point>(e);
    case resolution::buffered16:
        return choose_execution<L, resolution::buffered16>(e);
    case resolution::native16:
        return choose_execution<L, resolution::native16>(e);
    }
    std::abort();
}
counter bind_count(layout l, resolution r, execution e) {
    switch (l) {
    case layout::direct:
        return choose_resolution<layout::direct>(r, e);
    case layout::direct32:
        return choose_resolution<layout::direct32>(r, e);
    case layout::tuple_absolute:
        return choose_resolution<layout::tuple_absolute>(r, e);
    case layout::tuple_checkpoint:
        return choose_resolution<layout::tuple_checkpoint>(r, e);
    case layout::series_local:
        return choose_resolution<layout::series_local>(r, e);
    case layout::series_scan:
        return choose_resolution<layout::series_scan>(r, e);
    case layout::tuple_folded:
        return choose_resolution<layout::tuple_folded>(r, e);
    case layout::series_folded_local:
        return choose_resolution<layout::series_folded_local>(r, e);
    case layout::series_folded_scan:
        return choose_resolution<layout::series_folded_scan>(r, e);
    }
    std::abort();
}
#else
counter bind_count(layout, resolution, execution) {
    throw std::runtime_error("native study requires AVX512 or NEON");
}
#endif
} // namespace bec_study
