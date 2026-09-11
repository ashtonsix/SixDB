#include <ikea/seriespack.h>
#if defined(__aarch64__)
#include <ikea/seriespack/composition_neon.h>
namespace native = ikea::seriespack::neon;
#define SIXDB_EXAMPLE_NATIVE 1
#elif defined(__AVX512BW__) && defined(__AVX512VBMI__)
#include <ikea/seriespack/composition_x86.h>
namespace native = ikea::seriespack::avx512;
#define SIXDB_EXAMPLE_NATIVE 1
#elif defined(__AVX2__)
#include <ikea/seriespack/composition_x86.h>
namespace native = ikea::seriespack::avx2;
#define SIXDB_EXAMPLE_NATIVE 1
#else
#include <ikea/seriespack/composition.h>
#define SIXDB_EXAMPLE_NATIVE 0
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace pack = ikea::seriespack;
namespace comp = pack::composition;

template<class TagOps, class ValueOps, class Tags, class Values,
         class Rows, class Active>
auto sum_where_tag(TagOps& tag_ops, ValueOps& value_ops,
                   const Tags& tags, const Values& values,
                   Rows rows, Active active, std::uint64_t wanted) {
    auto tag_values = comp::read(tag_ops, tags, rows, active);
    auto matches = tag_ops.unsigned_equal(tag_values, wanted, active);
    auto counters = comp::read(value_ops, values, rows, matches);
    return value_ops.sum(counters, matches, pack::modulo_u64_sum{});
}

int main() {
    using Format = pack::format<12>;
    using TagFormat = pack::format<9>;
    const std::array<std::uint16_t, 10> values{7, 19, 42, 0, 4095, 3, 8, 91, 12, 6};
    const std::array<std::uint16_t, 10> tags{1, 2, 1, 3, 1, 2, 3, 1, 2, 1};

    // Two tiles each. Stride gaps belong to neither packed array.
    alignas(64) std::array<std::byte, 16 + Format::payload::tile_bytes> bytes{};
    alignas(64) std::array<std::byte, 12 + TagFormat::payload::tile_bytes> tag_bytes{};
    auto attached = pack::static_mutable_view<Format>::attach(
        values.size(), {{bytes, 16}, {}});
    auto tag_attached = pack::static_mutable_view<TagFormat>::attach(
        tags.size(), {{tag_bytes, 12}, {}});
    if (!attached || !tag_attached) return 1;
    if (!pack::encode(attached->as_dynamic(), std::span{values}) ||
        !pack::encode(tag_attached->as_dynamic(), std::span{tags})) return 2;

    std::array<std::uint16_t, 10> decoded{}, decoded_tags{};
    if (!pack::decode(attached->as_const().as_dynamic(), {0, values.size()},
                      std::span{decoded}, pack::execution_target::scalar) ||
        !pack::decode(tag_attached->as_const().as_dynamic(), {0, tags.size()},
                      std::span{decoded_tags}, pack::execution_target::scalar)) return 3;
    const auto reference = [&](std::uint64_t wanted, std::uint64_t candidates) {
        std::uint64_t total = 0;
        for (std::size_t i = 0; i != decoded.size(); ++i)
            if (((candidates >> i) & 1) && decoded_tags[i] == wanted)
                total += decoded[i];
        return total;
    };

#if SIXDB_EXAMPLE_NATIVE
    auto source = attached->as_const();
    auto tag_source = tag_attached->as_const();
    auto parts = comp::describe(source);
    auto tag_parts = comp::describe(tag_source);
    using ValueOps = native::composition_ops<Format, 0>;
    using TagOps = native::composition_ops<TagFormat, 0>;
    static_assert(ValueOps::lanes == 8 && TagOps::lanes == 8);
    static_assert(ValueOps::L == TagOps::L);
    ValueOps value_ops;
    TagOps tag_ops;

    // Bit i of candidates names original position i in this ten-value example.
    const auto run = [&](std::uint64_t wanted, std::uint64_t candidates) {
        std::uint64_t total = 0;
        for (std::size_t origin = 0; origin < values.size(); origin += 8) {
            const auto count = std::min<std::size_t>(8, values.size() - origin);
            const auto bits = (candidates >> origin) & ((std::uint64_t{1} << count) - 1);
            if (bits == 0) continue;
            total += sum_where_tag(tag_ops, value_ops, tag_parts, parts,
                native::tile_position{origin / 8}, TagOps::active_bits(bits), wanted);
        }
        return total;
    };

#endif
    constexpr std::uint64_t all = (std::uint64_t{1} << values.size()) - 1;
    struct query { std::uint64_t wanted, candidates, expected; };
    constexpr std::array queries{
        query{1, all, 4241}, query{1, all & ~(std::uint64_t{1} << 4), 146},
        query{512, all, 0}, query{1, 0, 0}};
    for (auto q : queries) {
        if (reference(q.wanted, q.candidates) != q.expected) return 4;
#if SIXDB_EXAMPLE_NATIVE
        if (run(q.wanted, q.candidates) != q.expected) return 5;
#endif
    }
    std::puts("all candidates: 4241; excluding position 4: 146");
#if SIXDB_EXAMPLE_NATIVE
    std::puts("Materialized and native results agree, including empty and out-of-domain queries.");
#else
    std::puts("Materialized results checked; native example skipped in this build.");
#endif
}
