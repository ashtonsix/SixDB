#pragma once
#include <ikea/comparison.h>
#include <ikea/tuplepack/author/filter.h>
#include <ikea/tuplepack/read.h>
#include <ikea/tuplepack/detail/native/read.h>

namespace ikea::tuplepack {
/// Prepared single-code comparison in original-row coordinates. Copies the
/// reader's plan/view pointers and the code projection, so the reader wrapper
/// may expire. The named plan/view and their values stay stable through use.
/// Prepare once for repeated filters; general readers retain only two pointers.
template <unsigned N, class Byte, unsigned Rows = 1> class predicate {
    reader<N, Byte, Rows> read_;
    code code_;
    template <unsigned, class, unsigned> friend class reader;
    explicit predicate(const reader<N, Byte, Rows>& source)
        : read_(source), code_(source.plan().controls().codes[0]) {}

  public:
    static constexpr unsigned rows = Rows;
    [[nodiscard]] static std::expected<predicate, error>
    bind(const reader<N, Byte, Rows>& source) {
        if (source.plan().ordering().map_size() != 1 || !source.plan().read_bytes())
            return std::unexpected(error::map);
        return predicate(source);
    }
    std::size_t size() const noexcept { return read_.size(); }
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
                ikea::to_u8(read_.source().row_unchecked(first))[code_.offset] & (limit << code_.shift);
            return ikea::compare<Op>(encoded, rhs << code_.shift) ? active : 0;
        }
#if defined(__aarch64__) || defined(__AVX2__)
        // A whole tight byte-column admits an encoded comparison. The shift
        // is order-preserving; mask away neighboring fields, not the query.
        if constexpr (Rows >= 16) {
            if (active == detail::all_rows<Rows> && read_.source().stride() == 1) {
                const auto* p = ikea::to_u8(read_.source().row_unchecked(first));
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
        return author::filter_values<Op, N>(detail::read_native(read_.plan(), read_.source(), first, active), rhs, active);
    }
    template <comparison Op>
    [[nodiscard]] std::expected<std::uint64_t, error>
    filter(std::size_t first, std::uint64_t rhs,
           std::uint64_t active = detail::all_rows<Rows>) const {
        if (!detail::valid_window<Rows>(read_.size(), first, active))
            return std::unexpected(error::range);
        return filter_unchecked<Op>(first, rhs, active);
    }
};

template <unsigned N, class Byte, unsigned Rows>
[[nodiscard]] auto bind_predicate(const reader<N, Byte, Rows>& source) {
    return predicate<N, Byte, Rows>::bind(source);
}
template <unsigned N, class Byte, unsigned Rows>
[[nodiscard]] std::expected<predicate<N, Byte, Rows>, error>
bind_predicate(const read_plan<N, Rows>& plan, const basic_view<Byte>& source) {
    auto operation = bind_reader(plan, source);
    if (!operation)
        return std::unexpected(operation.error());
    return bind_predicate(*operation);
}
template <unsigned N, class Byte, unsigned Rows>
auto bind_predicate(const read_plan<N, Rows>&&, const basic_view<Byte>&) = delete;
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
    return predicate<N, Byte, Rows>(*this).template filter_unchecked<Op>(first, rhs, active);
}
} // namespace ikea::tuplepack
