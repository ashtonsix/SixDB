#pragma once
#include <ikea/bec256/author/native.h>
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <cassert>
#include <memory>
#include <optional>
#include <vector>

namespace bec_study {
namespace bc = ikea::bec256;
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
enum class layout {
    direct,
    direct32,
    tuple_absolute,
    tuple_checkpoint,
    series_local,
    series_scan,
    tuple_folded,
    series_folded_local,
    series_folded_scan
};
inline constexpr std::array layouts{layout::direct,
                                    layout::direct32,
                                    layout::tuple_absolute,
                                    layout::tuple_checkpoint,
                                    layout::series_local,
                                    layout::series_scan,
                                    layout::tuple_folded,
                                    layout::series_folded_local,
                                    layout::series_folded_scan};
constexpr bool folded(layout l) {
    return l == layout::tuple_folded || l == layout::series_folded_local ||
           l == layout::series_folded_scan;
}
constexpr bool tuple(layout l) {
    return l == layout::tuple_absolute || l == layout::tuple_checkpoint ||
           l == layout::tuple_folded;
}
constexpr bool series(layout l) {
    return l == layout::series_local || l == layout::series_scan ||
           l == layout::series_folded_local || l == layout::series_folded_scan;
}
constexpr bool scan(layout l) {
    return l == layout::series_scan || l == layout::series_folded_scan;
}
constexpr bool absolute(layout l) {
    return l == layout::direct || l == layout::direct32 || l == layout::tuple_absolute;
}
constexpr unsigned population_code(unsigned p) { return std::min(p, 255u); }
constexpr unsigned expand_population(unsigned p, unsigned length) {
    return p + (p == 255 && length == 0);
}
enum class resolution { point, buffered16, native16 };
const char *name(layout);
const char *name(resolution);
struct entry {
    unsigned population, bytes, offset;
};
struct alignas(64) line {
    std::uint8_t data[64];
};
class bytes {
    std::vector<line> lines_;
    std::size_t size_;

  public:
    explicit bytes(std::size_t n) : lines_((n + 63) / 64), size_(n) {}
    auto data() { return reinterpret_cast<std::uint8_t *>(lines_.data()); }
    auto data() const { return reinterpret_cast<const std::uint8_t *>(lines_.data()); }
    std::size_t size() const { return size_; }
    std::span<std::uint8_t> span() { return {data(), size_}; }
};
using populations = sp::format<9>;
using folded_populations = sp::format<8>;
using local_lengths = sp::format<6>;
using scan_lengths = sp::format<6, sp::geometry::striped>;
template <class F>
using write_binding = typename decltype(sp::bind_mutation(
    std::declval<const sp::view<F, std::uint8_t> &>()))::value_type;

// This experimental owner is deliberately immovable: plans borrow named views
// within it. No owner catalogue is consulted by timed reads.
class directory {
    layout kind_;
    unsigned count_, capacity_, checkpoint_;
    std::vector<std::uint64_t> direct_;
    std::vector<std::uint32_t> direct32_;
    bytes tuples_, populations_, lengths_;
    std::vector<std::uint32_t> checkpoints_;
    std::optional<tp::layout> tuple_layout_;
    std::optional<tp::view> tuple_view_;
    std::optional<tp::reader<8>> tuple_plan_;
    std::optional<tp::reader<64, 16>> frame_plan_;
    std::optional<tp::writer<8>> tuple_writer_;
    std::optional<tp::read_operation<8, tp::byte>> tuple_read_;
    std::optional<tp::read_operation<64, tp::byte, 16>> frame_read_;
    std::optional<sp::view<populations, std::uint8_t>> population_view_;
    std::optional<sp::view<folded_populations, std::uint8_t>> folded_view_;
    std::optional<sp::view<local_lengths, std::uint8_t>> local_view_;
    std::optional<sp::view<scan_lengths, std::uint8_t>> scan_view_;
    std::optional<sp::decoder<std::uint16_t>> population_reader_;
    std::optional<sp::decoder<std::uint8_t>> folded_reader_;
    std::optional<sp::decoder<std::uint8_t>> length_reader_;
    std::optional<tp::mutation_operation<8>> tuple_write_;
    std::optional<write_binding<populations>> population_write_;
    std::optional<write_binding<folded_populations>> folded_write_;
    std::optional<write_binding<local_lengths>> local_write_;
    std::optional<write_binding<scan_lengths>> scan_write_;

  public:
    directory(layout, std::span<const entry>, unsigned checkpoint);
    directory(const directory &) = delete;
    layout kind() const { return kind_; }
    unsigned size() const { return count_; }
    unsigned checkpoint() const { return checkpoint_; }
    unsigned checkpoint_offset(unsigned first) const { return checkpoints_[first / checkpoint_]; }
    // Sum of exposed plane extents (including tile padding) and checkpoints.
    // Excludes owner/plan objects and the test allocator's cache-line rounding.
    std::size_t storage_bytes() const;
    std::uint64_t direct(unsigned i) const { return direct_[i]; }
    std::uint32_t direct32(unsigned i) const { return direct32_[i]; }
    std::uint64_t tuple(unsigned i) const { return tuple_read_->get_unchecked(i); }
    const auto &tuple_view() const { return *tuple_view_; }
    const auto &frame_plan() const { return *frame_plan_; }
    const auto &frame_read() const { return *frame_read_; }
    const auto *population_data() const { return populations_.data(); }
    const auto *length_data() const { return lengths_.data(); }
    unsigned population(unsigned i) const {
        return folded(kind_) ? folded_reader_->get_unchecked(i)
                             : population_reader_->get_unchecked(i);
    }
    unsigned length(unsigned i) const { return length_reader_->get_unchecked(i); }
    void buffered(unsigned first, unsigned *populations, unsigned *lengths) const;
    // Writes the selected metadata only; it neither moves following bodies nor
    // repairs their checkpoints. Timed callers rewrite existing entries. A real
    // length-changing mutation must repair those dependencies before publication.
    // Reports leaf identities and returns actual issued bytes for measurement.
    std::size_t update(unsigned row, entry replacement);
};

template <layout L> [[gnu::always_inline]] inline entry point(const directory &d, unsigned i) {
    if constexpr (L == layout::direct || L == layout::direct32) {
        auto value = [&] {
            if constexpr (L == layout::direct32)
                return std::uint64_t(d.direct32(i));
            else
                return d.direct(i);
        }();
        return {unsigned(value & 511), unsigned((value >> 9) & 63), unsigned(value >> 15)};
    } else {
        unsigned population, length, offset;
        if constexpr (tuple(L)) {
            auto value = d.tuple(i);
            population = value & (folded(L) ? 255 : 511);
            length = (value >> (folded(L) ? 8 : 16)) & 63;
            if constexpr (L == layout::tuple_absolute)
                return {population, length, unsigned(value >> 24)};
        } else {
            population = d.population(i);
            length = d.length(i);
        }
        if constexpr (folded(L))
            population = expand_population(population, length);
        const unsigned first = i / d.checkpoint() * d.checkpoint();
        offset = d.checkpoint_offset(first);
        for (unsigned j = first; j < i; ++j) {
            if constexpr (tuple(L))
                offset += (d.tuple(j) >> (folded(L) ? 8 : 16)) & 63;
            else
                offset += d.length(j);
        }
        return {population, length, offset};
    }
}

#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
// Native 16-record frame. It holds populations and exclusive byte prefixes;
// the absolute checkpoint remains scalar. Payload stays live between body pairs.
struct frame {
#if defined(IKEA_BEC256_AVX512)
    __m256i populations, offsets;
    unsigned base, next;
    [[gnu::always_inline]] entry at(unsigned i) const {
        const auto control = _mm256_set1_epi32(i / 2);
        const unsigned p = _mm_cvtsi128_si32(
            _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(populations, control)));
        const unsigned o = _mm_cvtsi128_si32(
            _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(offsets, control)));
        const unsigned begin = (o >> (16 * (i % 2))) & 65535;
        const auto next_control = _mm256_set1_epi32((i + 1) / 2);
        const unsigned following = _mm_cvtsi128_si32(
            _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(offsets, next_control)));
        const unsigned end = i == 15 ? next - base : (following >> (16 * ((i + 1) % 2))) & 65535;
        return {(p >> (16 * (i % 2))) & 511, end - begin, base + begin};
    }
#else
    uint8x16x4_t values;
    unsigned base, next;
    [[gnu::always_inline]] entry at(unsigned i) const {
        const uint8x8_t indices{std::uint8_t(2 * i),
                                std::uint8_t(2 * i + 1),
                                std::uint8_t(32 + 2 * i),
                                std::uint8_t(33 + 2 * i),
                                std::uint8_t(34 + 2 * i),
                                std::uint8_t(35 + 2 * i),
                                255,
                                255};
        const auto word = vget_lane_u64(vreinterpret_u64_u8(vqtbl4_u8(values, indices)), 0);
        const unsigned begin = (word >> 16) & 65535;
        const unsigned end = i == 15 ? next - base : (word >> 32) & 65535;
        return {unsigned(word & 511), end - begin, base + begin};
    }
#endif
};
frame make_frame(const unsigned *populations, const unsigned *lengths, unsigned base);
template <layout L>
[[gnu::always_inline]] frame native_frame(const directory &d, unsigned first, unsigned base) {
    static_assert(!absolute(L));
#if defined(IKEA_BEC256_AVX512)
    __m256i pops, lengths;
    if constexpr (tuple(L)) {
        auto words = tp::native_reader(d.frame_read()).get_unchecked(first);
        pops = _mm512_cvtepi32_epi16(
            _mm512_and_si512(words, _mm512_set1_epi32(folded(L) ? 255 : 511)));
        lengths = _mm512_cvtepi32_epi16(_mm512_srli_epi32(words, folded(L) ? 8 : 16));
    } else {
        if constexpr (folded(L))
            pops =
                _mm256_cvtepu8_epi16(sp::native::read16<folded_populations, true>(
                                         d.population_data(), folded_populations::tile_bytes, first)
                                         .v[0]);
        else
            pops = sp::native::read16<populations, true>(d.population_data(),
                                                         populations::tile_bytes, first)
                       .v[0];
        using F = std::conditional_t<scan(L), scan_lengths, local_lengths>;
        lengths = _mm256_cvtepu8_epi16(
            sp::native::read16<F, true>(d.length_data(), F::tile_bytes, first).v[0]);
    }
    if constexpr (folded(L))
        pops = _mm256_sub_epi16(
            pops, _mm256_and_si256(_mm256_cmpeq_epi16(pops, _mm256_set1_epi16(255)),
                                   _mm256_cmpeq_epi16(lengths, _mm256_setzero_si256())));
    auto sum = _mm256_add_epi16(lengths, _mm256_bslli_epi128(lengths, 2));
    sum = _mm256_add_epi16(sum, _mm256_bslli_epi128(sum, 4));
    sum = _mm256_add_epi16(sum, _mm256_bslli_epi128(sum, 8));
    const auto carry = _mm256_permute2x128_si256(sum, sum, 0x08);
    sum = _mm256_add_epi16(sum, _mm256_shuffle_epi8(carry, _mm256_set1_epi16(0x0f0e)));
    return {pops, _mm256_sub_epi16(sum, lengths), base,
            base + unsigned(_mm_extract_epi16(_mm256_extracti128_si256(sum, 1), 7))};
#else
    uint16x8_t p0, p1, l0, l1;
    if constexpr (tuple(L)) {
        auto words = tp::native_reader(d.frame_read()).get_unchecked(first);
        auto pop = [](uint8x16_t v) {
            return vmovn_u32(
                vandq_u32(vreinterpretq_u32_u8(v), vdupq_n_u32(folded(L) ? 255 : 511)));
        };
        auto len = [](uint8x16_t v) {
            return vmovn_u32(vshrq_n_u32(vreinterpretq_u32_u8(v), folded(L) ? 8 : 16));
        };
        p0 = vcombine_u16(pop(words.a), pop(words.b));
        p1 = vcombine_u16(pop(words.c), pop(words.d));
        l0 = vcombine_u16(len(words.a), len(words.b));
        l1 = vcombine_u16(len(words.c), len(words.d));
    } else {
        if constexpr (folded(L)) {
            auto pops = sp::native::read16<folded_populations, true>(
                            d.population_data(), folded_populations::tile_bytes, first)
                            .v[0];
            p0 = vmovl_u8(vget_low_u8(pops));
            p1 = vmovl_high_u8(pops);
        } else {
            auto pops = sp::native::read16<populations, true>(d.population_data(),
                                                              populations::tile_bytes, first);
            p0 = vreinterpretq_u16_u8(pops.v[0]);
            p1 = vreinterpretq_u16_u8(pops.v[1]);
        }
        using F = std::conditional_t<scan(L), scan_lengths, local_lengths>;
        auto lengths = sp::native::read16<F, true>(d.length_data(), F::tile_bytes, first).v[0];
        l0 = vmovl_u8(vget_low_u8(lengths));
        l1 = vmovl_high_u8(lengths);
    }
    if constexpr (folded(L)) {
        p0 = vsubq_u16(p0,
                       vandq_u16(vceqq_u16(p0, vdupq_n_u16(255)), vceqq_u16(l0, vdupq_n_u16(0))));
        p1 = vsubq_u16(p1,
                       vandq_u16(vceqq_u16(p1, vdupq_n_u16(255)), vceqq_u16(l1, vdupq_n_u16(0))));
    }
    auto prefix = [](uint16x8_t v) {
        auto z = vdupq_n_u16(0);
        v = vaddq_u16(v, vextq_u16(z, v, 7));
        v = vaddq_u16(v, vextq_u16(z, v, 6));
        return vaddq_u16(v, vextq_u16(z, v, 4));
    };
    auto s0 = prefix(l0), s1 = vaddq_u16(prefix(l1), vdupq_n_u16(vgetq_lane_u16(s0, 7)));
    return {{{vreinterpretq_u8_u16(p0), vreinterpretq_u8_u16(p1),
              vreinterpretq_u8_u16(vsubq_u16(s0, l0)), vreinterpretq_u8_u16(vsubq_u16(s1, l1))}},
            base,
            base + vgetq_lane_u16(s1, 7)};
#endif
}

template <layout L, resolution R> class cursor {
    const directory &directory_;
    unsigned group_ = ~0u;
    frame frame_{};

  public:
    explicit cursor(const directory &d) : directory_(d) {}
    [[gnu::always_inline]] entry get(unsigned i) {
        if constexpr (R == resolution::point || absolute(L))
            return point<L>(directory_, i);
        else {
            const unsigned group = i & ~15u;
            if (group != group_) {
                // At a checkpoint use its actual address; within a checkpoint
                // group sequential traversal retains the previous byte frontier.
                const unsigned base =
                    group % directory_.checkpoint() == 0    ? directory_.checkpoint_offset(group)
                    : group_ != ~0u && group == group_ + 16 ? frame_.next
                                                            : point<L>(directory_, group).offset;
                if constexpr (R == resolution::native16)
                    frame_ = native_frame<L>(directory_, group, base);
                else {
                    unsigned populations[16], lengths[16];
                    directory_.buffered(group, populations, lengths);
                    frame_ = make_frame(populations, lengths, base);
                }
                group_ = group;
            }
            return frame_.at(i % 16);
        }
    }
};
#endif
} // namespace bec_study
