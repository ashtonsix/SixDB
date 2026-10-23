#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
#include <ikea/comparison.h>
#include <ikea/detail/filter/compare.h>
#include <ikea/tuplepack/read.h>
#include <ikea/tuplepack/detail/native/packet.h>

namespace ikea::tuplepack {
/// Prepared single-code comparison in original-row coordinates. Copies the
/// code projection and named view pointer. Layout/plan/reader may expire after bind;
/// the named view and its values stay stable through use. No plan is retained.
template <class Byte, unsigned Rows = 1> class predicate {
    static_assert(Rows <= 64 && std::has_single_bit(Rows));
    const basic_view<Byte>* source_;
    code code_;
    template <unsigned, class, unsigned> friend class reader;
    template <unsigned N>
    explicit predicate(const reader<N, Byte, Rows>& source)
        : source_(&source.source()), code_(source.plan().controls().codes[0]) {}
    predicate(const basic_view<Byte>& source, code selected)
        : source_(&source), code_(selected) {}

  public:
    static constexpr unsigned rows = Rows;
    /// Copies one code from a layout equal to the one attached to these bytes;
    /// the layout may expire after binding. No payload is accessed. A different
    /// law rejects with description before an invalid code index with map.
    [[nodiscard]] static std::expected<predicate, error>
    bind(const layout& format, std::size_t code_index, const basic_view<Byte>& source) {
        if (format.identity() != source.layout_identity())
            return std::unexpected(error::description);
        if (code_index >= format.codes().size())
            return std::unexpected(error::map);
        return predicate(source, format.codes()[code_index]);
    }
    static auto bind(const layout&, std::size_t, const basic_view<Byte>&&) = delete;
    template <unsigned N>
    [[nodiscard]] static std::expected<predicate, error>
    bind(const reader<N, Byte, Rows>& source) {
        if (source.plan().ordering().map_size() != 1 || !source.plan().read_bytes())
            return std::unexpected(error::map);
        return predicate(source);
    }
    std::size_t size() const noexcept { return source_->size(); }
    template <comparison Op>
    /// Requires first <= source size, activity within Rows, and every active
    /// row in bounds. Empty activity requires no payload storage.
    [[gnu::always_inline]] std::uint64_t
    filter_unchecked(std::size_t first, std::uint64_t rhs,
                     std::uint64_t active = detail::all_rows<Rows>) const {
        if (!active)
            return 0;
        const unsigned limit = (1u << code_.width) - 1;
        if (rhs > limit)
            return ikea::compare<Op>(0, rhs) ? active : 0;
        if constexpr (Rows == 1) {
            const unsigned encoded =
                ikea::to_u8(source_->row_unchecked(first))[code_.offset] & (limit << code_.shift);
            return ikea::compare<Op>(encoded, rhs << code_.shift) ? active : 0;
        }
#if defined(__aarch64__) || defined(__AVX2__)
        // A whole tight byte-column admits an encoded comparison. The shift
        // is order-preserving; mask away neighboring fields, not the query.
        if constexpr (Rows >= 16) {
            if (active == detail::all_rows<Rows> && source_->stride() == 1) {
                const auto* p = ikea::to_u8(source_->row_unchecked(first));
                const auto mask = limit << code_.shift;
                const auto query = rhs << code_.shift;
#if defined(__aarch64__)
                std::uint64_t bits = 0;
#pragma clang loop unroll(full)
                for (unsigned i = 0; i < Rows / 16; ++i)
                    bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                                vandq_u8(vld1q_u8(p + i * 16), vdupq_n_u8(mask)), query))
                            << (i * 16);
                return bits;
#else
                if constexpr (Rows == 16)
                    return ikea::detail::filter::bits<1, Op>(
                        _mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)),
                                      _mm_set1_epi8(mask)),
                        query);
#if defined(__AVX512BW__)
                else if constexpr (Rows == 64)
                    return ikea::detail::filter::bits<1, Op>(
                        _mm512_and_si512(_mm512_loadu_si512(p), _mm512_set1_epi8(mask)), query);
#endif
                else {
                    std::uint64_t bits = 0;
                    [&]<std::size_t... I>(std::index_sequence<I...>) {
                        ((bits |=
                          std::uint64_t(ikea::detail::filter::bits<1, Op>(
                              _mm256_and_si256(
                                  _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + I * 32)),
                                  _mm256_set1_epi8(mask)),
                              query))
                          << (I * 32)),
                         ...);
                    }(std::make_index_sequence<Rows / 32>{});
                    return bits;
                }
#endif
            }
        }
#endif
        // One admitted code needs no packet-order shuffle. The existing
        // transfer machinery retains tight and masked paths, with exact
        // active-byte gathers for strided placement and partial windows.
        detail::packet_placement place;
        place.first = code_.offset;
        place.count = 1;
        const auto base = ikea::to_u8(source_->row_unchecked(first));
        const auto stride = source_->stride();
        const auto value = native::packet_detail::gather_bound<Rows>(
            place, [&](unsigned row) { return base + row * stride; }, active, stride);
        const auto mask = limit << code_.shift;
        const auto query = rhs << code_.shift;
#if defined(__aarch64__)
        std::uint64_t bits = ikea::detail::filter::bits<1, Op>(
            vandq_u8(value.a, vdupq_n_u8(mask)), query);
        if constexpr (Rows > 16)
            bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                vandq_u8(value.b, vdupq_n_u8(mask)), query)) << 16;
        if constexpr (Rows > 32) {
            bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                vandq_u8(value.c, vdupq_n_u8(mask)), query)) << 32;
            bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                vandq_u8(value.d, vdupq_n_u8(mask)), query)) << 48;
        }
#elif defined(__AVX512VBMI__)
        std::uint64_t bits;
        if constexpr (Rows <= 16)
            bits = ikea::detail::filter::bits<1, Op>(
                _mm_and_si128(_mm512_castsi512_si128(value), _mm_set1_epi8(mask)), query);
        else if constexpr (Rows <= 32)
            bits = ikea::detail::filter::bits<1, Op>(
                _mm256_and_si256(_mm512_castsi512_si256(value), _mm256_set1_epi8(mask)), query);
        else
            bits = ikea::detail::filter::bits<1, Op>(
                _mm512_and_si512(value, _mm512_set1_epi8(mask)), query);
#else
        std::uint64_t bits;
        if constexpr (Rows <= 16)
            bits = ikea::detail::filter::bits<1, Op>(
                _mm_and_si128(_mm256_castsi256_si128(value.a), _mm_set1_epi8(mask)), query);
        else {
            bits = ikea::detail::filter::bits<1, Op>(
                _mm256_and_si256(value.a, _mm256_set1_epi8(mask)), query);
            if constexpr (Rows > 32)
                bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                    _mm256_and_si256(value.b, _mm256_set1_epi8(mask)), query)) << 32;
        }
#endif
        return bits & active;
    }
    template <comparison Op>
    [[nodiscard]] std::expected<std::uint64_t, error>
    filter(std::size_t first, std::uint64_t rhs,
           std::uint64_t active = detail::all_rows<Rows>) const {
        if (!detail::valid_window<Rows>(source_->size(), first, active))
            return std::unexpected(error::range);
        return filter_unchecked<Op>(first, rhs, active);
    }
};

template <unsigned Rows = 1, class Byte>
[[nodiscard]] auto bind_predicate(const layout& format, std::size_t code_index,
                                  const basic_view<Byte>& source) {
    return predicate<Byte, Rows>::bind(format, code_index, source);
}
template <unsigned Rows = 1, class Byte>
auto bind_predicate(const layout&, std::size_t, const basic_view<Byte>&&) = delete;

template <unsigned N, class Byte, unsigned Rows>
[[nodiscard]] auto bind_predicate(const reader<N, Byte, Rows>& source) {
    return predicate<Byte, Rows>::bind(source);
}
template <unsigned N, class Byte, unsigned Rows>
[[nodiscard]] std::expected<predicate<Byte, Rows>, error>
bind_predicate(const read_plan<N, Rows>& plan, const basic_view<Byte>& source) {
    auto operation = bind_reader(plan, source);
    if (!operation)
        return std::unexpected(operation.error());
    return bind_predicate(*operation);
}
template <unsigned N, class Byte, unsigned Rows>
auto bind_predicate(const read_plan<N, Rows>&, const basic_view<Byte>&&) = delete;

template <unsigned N, class Byte, unsigned Rows>
template <comparison Op>
std::expected<std::uint64_t, error> reader<N, Byte, Rows>::filter(
    std::size_t first, std::uint64_t rhs, std::uint64_t active) const {
    auto prepared = bind_predicate(*this);
    if (!prepared)
        return std::unexpected(prepared.error());
    return prepared->template filter<Op>(first, rhs, active);
}
template <unsigned N, class Byte, unsigned Rows>
template <comparison Op>
[[gnu::always_inline]] std::uint64_t reader<N, Byte, Rows>::filter_unchecked(
    std::size_t first, std::uint64_t rhs, std::uint64_t active) const {
    return predicate<Byte, Rows>(*this).template filter_unchecked<Op>(first, rhs, active);
}
} // namespace ikea::tuplepack
