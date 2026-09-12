#pragma once
#include <ikea/tuplepack/author/execution.h>

#if defined(__aarch64__) || defined(__AVX2__)
namespace ikea::tuplepack {
/// Native 16-byte projections over four tuples, or 32-byte projections over two.
/// Output remains row-major within the packet. Placement/stride are independent
/// of shape. Maps stay runtime-bound; a compact source-window proof selects the
/// gathered body, otherwise the same ordered projection has a general fallback.
template <unsigned Rows> class batch_reader {
    static_assert(Rows == 2 || Rows == 4);
    reader<64> reader_;
    detail::shuffle group_;
    bool compact_;
    batch_reader(reader<64> read, detail::shuffle group, bool compact)
        : reader_(std::move(read)), group_(std::move(group)), compact_(compact) {}

  public:
    static constexpr unsigned rows = Rows, bytes_per_row = 64 / Rows;
    [[nodiscard]] static std::expected<batch_reader, error> make(const layout& format,
                                                                 std::span<const byte> map) {
        if (map.size() > bytes_per_row)
            return std::unexpected(error::map);
        auto read = reader<64>::make(format, map);
        if (!read)
            return std::unexpected(read.error());
        const auto& p = read->controls();
        detail::shuffle_description description;
        for (unsigned row = 0; row < Rows; ++row)
            for (unsigned i = 0; i < map.size(); ++i) {
                const auto c = p.codes[i];
                if (!c.width)
                    continue;
                unsigned chunk = 0;
                while (chunk < p.count && p.chunks[chunk].offset / 16 != c.offset / 16)
                    ++chunk;
                const auto out = row * bytes_per_row + i;
                description.index[out] = row * bytes_per_row + chunk * 16 + c.offset % 16;
                description.shift[out] = -c.shift;
                description.mask[out] = (1u << c.width) - 1;
            }
        const bool compact = p.count <= bytes_per_row / 16;
        return batch_reader(
            std::move(*read),
            compact ? detail::compile_shuffle(description, false) : detail::shuffle{}, compact);
    }
    unsigned unit_bytes() const noexcept {
        return reader_.unit_bytes();
    }
    std::uint64_t read_bytes() const noexcept {
        return reader_.read_bytes();
    }
    bool compact_source() const noexcept {
        return compact_;
    }
    /// Active bit r refers to input pointer r and output bytes r*bytes_per_row.
    /// Inactive pointers may be null and are never read; their output is zero.
    /// Each active pointer supplies the reader's admitted physical unit.
    [[gnu::always_inline]] native::packet
    gather_unchecked(const std::array<const byte*, Rows>& pointers, unsigned active) const {
        using namespace native::native_detail;
        auto gathered = join(source_part<0>(pointers, active), source_part<1>(pointers, active),
                             source_part<2>(pointers, active), source_part<3>(pointers, active));
        if (!compact_)
            return gathered;
        if constexpr (Rows == 4) {
            // Each 16-byte lane is independent: no cross-lane table selection.
#if defined(__aarch64__)
            auto apply = [&](auto data) {
                auto v = vqtbl1q_u8(data, vld1q_u8(group_.index.data()));
                if (group_.shifting)
                    v = vshlq_u8(v, vld1q_s8(group_.shift.data()));
                return vandq_u8(v, vld1q_u8(group_.mask.data()));
            };
            return {apply(gathered.a), apply(gathered.b), apply(gathered.c), apply(gathered.d)};
#elif defined(__AVX512VBMI__)
            auto v = _mm512_shuffle_epi8(gathered, _mm512_loadu_si512(group_.index.data()));
            if (group_.shifting)
                v = _mm512_multishift_epi64_epi8(_mm512_loadu_si512(group_.bit_index.data()), v);
            return _mm512_and_si512(v, _mm512_loadu_si512(group_.mask.data()));
#else
            // The lane-local permutation is already complete; use the direct
            // shift/mask body rather than a second general table lookup.
            auto v = native::packet{
                _mm256_shuffle_epi8(gathered.a, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
                                                    group_.index.data()))),
                _mm256_shuffle_epi8(gathered.b, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
                                                    group_.index.data() + 32)))};
            return shift_group(v);
#endif
        } else
            return native::transform<false>(gathered, group_);
    }
    /// Trusted range window. Only active rows must be in range; inactive output
    /// coordinates stay in their original positions. No pointer is formed for them.
    template <class Byte>
    [[gnu::always_inline]] native::packet read_unchecked(const basic_view<Byte>& source,
                                                         std::size_t first,
                                                         unsigned active = (1u << Rows) - 1) const {
        std::array<const byte*, Rows> pointers{};
        for (unsigned i = 0; i < Rows; ++i)
            if (active & (1u << i))
                pointers[i] = source.row_unchecked(first + i);
        return gather_unchecked(pointers, active);
    }

  private:
    template <unsigned I>
    [[gnu::always_inline]] native::vector16
    source_part(const std::array<const byte*, Rows>& pointers, unsigned active) const {
        using namespace native::native_detail;
        constexpr unsigned row = I / (bytes_per_row / 16), chunk = I % (bytes_per_row / 16);
        const auto& p = reader_.controls();
        if (!(active & (1u << row)))
            return zero16();
        if (compact_) {
            if (chunk >= p.count)
                return zero16();
            return load16(pointers[row] + p.chunks[chunk].offset, p.chunks[chunk].bytes);
        }
        return split<chunk>(native::read_body(p, pointers[row]));
    }
#if defined(__AVX2__) && !defined(__AVX512VBMI__)
    [[gnu::always_inline]] native::packet shift_group(native::packet source) const {
        auto part = [&](auto value, unsigned i) {
            if (group_.shifting) {
                const auto low = _mm256_set1_epi16(255), high = _mm256_set1_epi16(short(0xff00));
                const auto ef = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(group_.even_factor.data() + i));
                const auto of = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(group_.odd_factor.data() + i));
                const auto even =
                    _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_and_si256(value, low), ef), 8);
                const auto odd =
                    _mm256_and_si256(_mm256_mullo_epi16(_mm256_srli_epi16(value, 8), of), high);
                value = _mm256_or_si256(even, odd);
            }
            return _mm256_and_si256(value, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
                                               group_.mask.data() + i * 2)));
        };
        return {part(source.a, 0), part(source.b, 16)};
    }
#endif
};
} // namespace ikea::tuplepack
#endif
