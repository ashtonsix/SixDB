#pragma once
#include <ikea/seriespack/author/indexof.h>
#include <ikea/seriespack/detail/physical_source.h>
#include <ikea/seriespack/detail/placed_read.h>

namespace ikea::seriespack {
namespace detail {
template <class> inline constexpr bool is_dense_source = false;
template <class L> inline constexpr bool is_dense_source<dense_source<L>> = true;
struct read_admitted_value {};
struct read_admission {
    std::size_t count;
    bool valid = true;
    template <class S, author::field_kind F>
    read_admitted_value read(const author::leaf<S, F>& source, std::size_t, bool) {
        const auto extent = [&] {
            if constexpr (requires { source.source.count; })
                return source.source.count;
            else
                return source.source.size();
        }();
        valid &= count <= extent;
        return {};
    }
    template <unsigned> read_admitted_value join(read_admitted_value, read_admitted_value) {
        return {};
    }
};
}

/// Typed whole read operation. Owns its expression and borrows its named leaf
/// views. Keep this object stationary while an erased operation borrows it.
/// Physical whole-view reads retain the specialized dense/placed range bodies.
template <class Expression> class reader {
    Expression expression_;
    std::size_t count_;
    using physical_type = decltype(author::range_detail::physical_source(
        std::declval<const Expression&>()));
    physical_type physical_;
    reader(Expression e, std::size_t count)
        : expression_(std::move(e)), count_(count),
          physical_(author::range_detail::physical_source(expression_)) {}

    template <class U> void region_unchecked(std::size_t first, U* output) const {
#if defined(__aarch64__) || defined(__AVX2__)
        author::native_ops ops;
        native::store16(output, author::read(ops, expression_, first, std::uint16_t{0xffff}));
#else
        for (unsigned i = 0; i < 16; ++i)
            output[i] = get_unchecked(first + i);
#endif
    }

  public:
    using value_type = uint_for<Expression::bit_width>;
    /// Owner-proved expression extents; no data access or allocation.
    static reader bind_unchecked(Expression e, std::size_t count) {
        return reader(std::move(e), count);
    }
    [[nodiscard]] static std::expected<reader, error> bind(Expression e, std::size_t count) {
        detail::read_admission check{count};
        author::read(check, e, std::size_t{0}, true);
        if (!check.valid)
            return std::unexpected(error::range);
        return bind_unchecked(std::move(e), count);
    }
    std::size_t size() const noexcept { return count_; }
    const Expression& expression() const noexcept { return expression_; }
    value_type get_unchecked(std::size_t row) const {
        if constexpr (std::is_pointer_v<physical_type>)
            if (physical_) {
                if constexpr (requires { physical_->data; }) {
                    using V = std::remove_cv_t<std::remove_pointer_t<physical_type>>;
                    return detail::dense_point<typename V::layout_type, value_type>(physical_->data, row);
                }
                else
                    return seriespack::get_unchecked(*physical_, row);
            }
        author::scalar_ops ops;
        return author::read(ops, expression_, row, true).value;
    }
    [[nodiscard]] std::expected<value_type, error> get(std::size_t row) const {
        if (row >= count_)
            return std::unexpected(error::range);
        return get_unchecked(row);
    }
    template <class U>
    void read_unchecked(std::size_t first, std::size_t count, U* __restrict output) const {
        static_assert(std::is_unsigned_v<U> && !std::is_same_v<U, bool> &&
                      sizeof(U) * 8 >= Expression::bit_width);
        if constexpr (std::is_pointer_v<physical_type>) {
            if (physical_) {
                using V = std::remove_cv_t<std::remove_pointer_t<physical_type>>;
                using L = typename V::layout_type;
                if constexpr (requires { physical_->data; }) {
                    detail::dense_range<L, U>(physical_->data, first, count, output);
                    return;
                } else {
                    if constexpr (L::heads == 0)
                        if (physical_->stream(0).stride == L::tile_bytes) {
                            detail::dense_range<L, U>(physical_->stream(0).bytes.data(), first,
                                                     count, output);
                            return;
                        }
                    detail::placed_range<V, U>(physical_, first, count, output);
                    return;
                }
            }
        }
        while (count && first % 16) {
            *output++ = get_unchecked(first++);
            --count;
        }
        while (count >= 16) {
            region_unchecked(first, output);
            first += 16;
            output += 16;
            count -= 16;
        }
        while (count--)
            *output++ = get_unchecked(first++);
    }
    template <class U>
    [[nodiscard]] std::expected<void, error> read(std::size_t first, std::span<U> output) const {
        if (first > count_ || output.size() > count_ - first)
            return std::unexpected(error::range);
        read_unchecked(first, output.size(), output.data());
        return {};
    }
  private:
    // A whole-view headless source at its tight stride is a dense array; its
    // reconstruction then takes the dense native groups (e.g. GFNI Local
    // residuals), which a general placed stride cannot use. The temporary
    // expression never escapes: f returns a value.
    template <class F> auto with_physical(F&& f) const {
        if constexpr (std::is_pointer_v<physical_type>) {
            if (physical_) {
                using V = std::remove_cv_t<std::remove_pointer_t<physical_type>>;
                using L = typename V::layout_type;
                if constexpr (!requires { physical_->data; } && L::heads == 0)
                    if (physical_->stream(0).stride == L::tile_bytes) {
                        const dense_source<L> dense{physical_->stream(0).bytes.data(), count_};
                        return f(author::expression(dense));
                    }
            }
        }
        return f(expression_);
    }

  public:
    template <comparison Op>
    std::uint64_t filter_unchecked(std::size_t first, unsigned count, std::uint64_t rhs,
                                   std::uint64_t active = ~std::uint64_t{0}) const {
        return with_physical([&](const auto& e) { return author::filter_unchecked<Op>(e, first, count, rhs, active); });
    }
    template <comparison Op>
    [[nodiscard]] std::expected<std::uint64_t, error>
    filter(std::size_t first, unsigned count, std::uint64_t rhs,
           std::uint64_t active = ~std::uint64_t{0}) const {
        if (count > 64 || first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        return filter_unchecked<Op>(first, count, rhs, active);
    }
#if defined(__aarch64__) || defined(__AVX2__)
    /// Rows of [first, first + count) satisfying Op against rhs, in one pass.
    template <comparison Op> std::uint64_t count_unchecked(std::size_t first, std::size_t count, std::uint64_t rhs) const {
        return with_physical([&](const auto& e) {
            std::uint64_t n = 0;
            // Bit-sliced Local blocks count in vectors; the rest emit words.
            using F = typename author::dense_layout<std::remove_cvref_t<decltype(e)>>::type;
            if constexpr (!std::is_void_v<F>)
                if constexpr (detail::local_planes<F>::supported)
                    if (first % 64 == 0 && rhs < (std::uint64_t{1} << F::payload)) {
                        const std::size_t blocks = count / detail::local_planes<F>::rows;
                        n = detail::local_planes<F>::template count<Op>(
                            ikea::to_u8(e.payload.body.source.data) + first / 8 * F::tile_bytes, blocks, rhs);
                        first += blocks * detail::local_planes<F>::rows;
                        count -= blocks * detail::local_planes<F>::rows;
                    }
            author::filter_regions<Op>(e, first, count, rhs,
                                       [&](std::size_t, std::uint64_t bits) __attribute__((always_inline)) { n += std::popcount(bits); });
            return n;
        });
    }
    /// The filter bits of [first, first + count) into (count + 63) / 64 words.
    template <comparison Op>
    void filter_words_unchecked(std::size_t first, std::size_t count, std::uint64_t rhs, std::uint64_t* __restrict words) const {
        with_physical([&](const auto& e) {
            author::filter_regions<Op>(e, first, count, rhs,
                                       [&](std::size_t w, std::uint64_t bits) __attribute__((always_inline)) { words[w] = bits; });
            return 0;
        });
    }
    template <comparison Op>
    [[nodiscard]] std::expected<std::uint64_t, error> count(std::size_t first, std::size_t count,
                                                            std::uint64_t rhs) const {
        if (first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        return count_unchecked<Op>(first, count, rhs);
    }
    /// Bit i of word i / 64 is row first + i. Writes exactly the first
    /// count / 64 + (count % 64 != 0) words, the last word's unused high bits
    /// zero; later words, and the output on failure, are left unchanged.
    template <comparison Op>
    [[nodiscard]] std::expected<void, error> filter_words(std::size_t first, std::size_t count, std::uint64_t rhs,
                                                          std::span<std::uint64_t> words) const {
        if (first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        if (words.size() < count / 64 + (count % 64 != 0))
            return std::unexpected(error::capacity);
        filter_words_unchecked<Op>(first, count, rhs, words.data());
        return {};
    }
#endif
    unsigned indexof_unchecked(std::size_t first, unsigned count, std::uint64_t rhs,
                              std::uint64_t active = ~std::uint64_t{0}) const {
        return author::indexof_unchecked(expression_, first, count, rhs, active);
    }
    [[nodiscard]] std::expected<unsigned, error>
    indexof(std::size_t first, unsigned count, std::uint64_t rhs,
            std::uint64_t active = ~std::uint64_t{0}) const {
        if (count > 64 || first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        return indexof_unchecked(first, count, rhs, active);
    }
    template <class Prefilter>
    std::uint64_t sum_unchecked(std::size_t first, std::size_t count, std::uint64_t cutoff,
                                Prefilter&& mask) const {
#if defined(__aarch64__) || defined(__AVX2__)
        return with_physical([&](const auto& e) { return author::sum_regions(e, first, count, cutoff, mask); });
#else
        std::uint64_t result = 0;
        for (std::size_t row = first; row < first + count; ++row)
            if (mask(row - row % 16) & (1u << (row % 16))) {
                const auto value = get_unchecked(row);
                result += value < cutoff ? value : 0;
            }
        return result;
#endif
    }
#if defined(__aarch64__) || defined(__AVX2__)
    /// The sum, modulo 2^64, of every row of [first, first + count). Unlike the
    /// cutoff overload, no row is excluded (the maximum value included).
    std::uint64_t sum_unchecked(std::size_t first, std::size_t count) const {
        return with_physical([&](const auto& e) { return author::total_regions(e, first, count); });
    }
    [[nodiscard]] std::expected<std::uint64_t, error> sum(std::size_t first, std::size_t count) const {
        if (first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        return sum_unchecked(first, count);
    }
    /// The exact sum of every row of [first, first + count), as u128.
    unsigned __int128 sum_exact_unchecked(std::size_t first, std::size_t count) const {
        return with_physical([&](const auto& e) {
            return author::exact_regions(e, first, count, nullptr, 0, false, [](std::size_t) { return std::uint16_t{0xffff}; });
        });
    }
    /// The exact sum of the rows mask(origin) selects, as u128 (no cutoff).
    template <class Prefilter>
    unsigned __int128 sum_exact_unchecked(std::size_t first, std::size_t count, Prefilter&& mask) const {
        return with_physical([&](const auto& e) { return author::exact_regions(e, first, count, nullptr, 0, true, mask); });
    }
    /// The exact sum, as u128, of weights[row % period] * value over [first,
    /// first + count); period is 16 or 64 and weights holds period entries.
    /// Trusted: the caller proves the weighted total fits u128 (an
    /// unweighted sum always does; u8 weights can exceed it).
    unsigned __int128 weighted_sum_unchecked(std::size_t first, std::size_t count, const std::uint8_t* weights,
                                             unsigned period = 16) const {
        return with_physical([&](const auto& e) {
            return author::exact_regions(e, first, count, weights, period, false, [](std::size_t) { return std::uint16_t{0xffff}; });
        });
    }
#endif
    template <class Prefilter>
    [[nodiscard]] std::expected<std::uint64_t, error>
    sum(std::size_t first, std::size_t count, std::uint64_t cutoff, Prefilter&& mask) const {
        if (first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        return sum_unchecked(first, count, cutoff, std::forward<Prefilter>(mask));
    }
    template <class U> erased_reader<U> erase() const& {
        static_assert(sizeof(U) * 8 >= Expression::bit_width);
        if constexpr (std::is_pointer_v<physical_type>)
            if (physical_) {
                if constexpr (requires { physical_->data; }) {
                    auto source = *physical_;
                    source.count = count_;
                    return detail::erase_dense_reader<U>(source);
                } else
                    return detail::erase_placed_reader<U>(*physical_, count_);
            }
        return {this, count_,
                [](const void* p, std::size_t first, std::size_t count, U* out) {
                    static_cast<const reader*>(p)->read_unchecked(first, count, out);
                },
                [](const void* p, std::size_t first, U* out) {
                    static_cast<const reader*>(p)->region_unchecked(first, out);
                },
                [](const void* p, std::size_t row) -> U {
                    return static_cast<const reader*>(p)->get_unchecked(row);
                }};
    }
    template <class U> auto erase() const&& = delete;
};

template <class E>
[[nodiscard]] auto bind_reader(E expression, std::size_t count) {
    return reader<E>::bind(std::move(expression), count);
}
template <class L, class B> auto bind_reader(const basic_view<L, B>& source) {
    using E = decltype(author::expression(source));
    return reader<E>::bind_unchecked(author::expression(source), source.size());
}
template <class L, class B> auto bind_reader(const basic_view<L, B>&&) = delete;

/// Explicit output type selects an erased physical operation directly. The
/// placed form borrows the named view; the dense form copies its pointer/count.
template <class U, class L, class B>
erased_reader<U> bind_reader(const basic_view<L, B>& source) {
    return detail::erase_placed_reader<U>(source);
}
template <class U, class L, class B>
auto bind_reader(const basic_view<L, B>&&) = delete;
template <class U, class L> erased_reader<U> bind_reader(dense_source<L> source) {
    return detail::erase_dense_reader<U>(source);
}

/// Dense placement is a copied pointer/count proof, so no temporary source
/// wrapper or reader object needs to survive its erased read operation.
template <class L> class reader<dense_source<L>> {
    dense_source<L> source_;
    auto composed() const {
        using E = decltype(author::expression(source_));
        return reader<E>::bind_unchecked(author::expression(source_), source_.count);
    }

  public:
    using value_type = uint_for<L::width>;
    explicit reader(dense_source<L> source) : source_(source) {}
    std::size_t size() const noexcept { return source_.count; }
    value_type get_unchecked(std::size_t row) const {
        return detail::dense_point<L, value_type>(source_.data, row);
    }
    [[nodiscard]] std::expected<value_type, error> get(std::size_t row) const {
        if (row >= size())
            return std::unexpected(error::range);
        return get_unchecked(row);
    }
    template <class U>
    void read_unchecked(std::size_t first, std::size_t count, U* __restrict output) const {
        static_assert(std::is_unsigned_v<U> && !std::is_same_v<U, bool> && sizeof(U) * 8 >= L::width);
        detail::dense_range<L, U>(source_.data, first, count, output);
    }
    template <class U>
    [[nodiscard]] std::expected<void, error> read(std::size_t first, std::span<U> output) const {
        if (first > size() || output.size() > size() - first)
            return std::unexpected(error::range);
        read_unchecked(first, output.size(), output.data());
        return {};
    }
    template <comparison Op>
    std::uint64_t filter_unchecked(std::size_t first, unsigned count, std::uint64_t rhs,
                                   std::uint64_t active = ~std::uint64_t{0}) const {
        return composed().template filter_unchecked<Op>(first, count, rhs, active);
    }
    template <comparison Op>
    [[nodiscard]] std::expected<std::uint64_t, error>
    filter(std::size_t first, unsigned count, std::uint64_t rhs,
           std::uint64_t active = ~std::uint64_t{0}) const {
        return composed().template filter<Op>(first, count, rhs, active);
    }
    unsigned indexof_unchecked(std::size_t first, unsigned count, std::uint64_t rhs,
                              std::uint64_t active = ~std::uint64_t{0}) const {
        return composed().indexof_unchecked(first, count, rhs, active);
    }
    [[nodiscard]] std::expected<unsigned, error>
    indexof(std::size_t first, unsigned count, std::uint64_t rhs,
            std::uint64_t active = ~std::uint64_t{0}) const {
        return composed().indexof(first, count, rhs, active);
    }
    template <class Prefilter>
    std::uint64_t sum_unchecked(std::size_t first, std::size_t count, std::uint64_t cutoff,
                                Prefilter&& mask) const {
        return composed().sum_unchecked(first, count, cutoff, std::forward<Prefilter>(mask));
    }
    template <class Prefilter>
    [[nodiscard]] std::expected<std::uint64_t, error>
    sum(std::size_t first, std::size_t count, std::uint64_t cutoff, Prefilter&& mask) const {
        return composed().sum(first, count, cutoff, std::forward<Prefilter>(mask));
    }
#if defined(__aarch64__) || defined(__AVX2__)
    template <comparison Op>
    std::uint64_t count_unchecked(std::size_t first, std::size_t count, std::uint64_t rhs) const {
        return composed().template count_unchecked<Op>(first, count, rhs);
    }
    template <comparison Op>
    [[nodiscard]] std::expected<std::uint64_t, error> count(std::size_t first, std::size_t count,
                                                            std::uint64_t rhs) const {
        return composed().template count<Op>(first, count, rhs);
    }
    template <comparison Op>
    void filter_words_unchecked(std::size_t first, std::size_t count, std::uint64_t rhs,
                                std::uint64_t* __restrict words) const {
        composed().template filter_words_unchecked<Op>(first, count, rhs, words);
    }
    template <comparison Op>
    [[nodiscard]] std::expected<void, error> filter_words(std::size_t first, std::size_t count, std::uint64_t rhs,
                                                          std::span<std::uint64_t> words) const {
        return composed().template filter_words<Op>(first, count, rhs, words);
    }
    std::uint64_t sum_unchecked(std::size_t first, std::size_t count) const {
        return composed().sum_unchecked(first, count);
    }
    [[nodiscard]] std::expected<std::uint64_t, error> sum(std::size_t first, std::size_t count) const {
        return composed().sum(first, count);
    }
    unsigned __int128 sum_exact_unchecked(std::size_t first, std::size_t count) const {
        return composed().sum_exact_unchecked(first, count);
    }
    template <class Prefilter>
    unsigned __int128 sum_exact_unchecked(std::size_t first, std::size_t count, Prefilter&& mask) const {
        return composed().sum_exact_unchecked(first, count, std::forward<Prefilter>(mask));
    }
    unsigned __int128 weighted_sum_unchecked(std::size_t first, std::size_t count, const std::uint8_t* weights,
                                             unsigned period = 16) const {
        return composed().weighted_sum_unchecked(first, count, weights, period);
    }
#endif
    template <class U> erased_reader<U> erase() const {
        return detail::erase_dense_reader<U>(source_);
    }
};
template <class Source>
    requires detail::is_dense_source<Source>
auto bind_reader(Source source) {
    return reader<Source>(source);
}

/// Runtime-width dense input; the parent supplies coherent bytes, layout and
/// logical count. Binding validates placement and selects one compiled body.
struct dense_input {
    unsigned width;
    geometry storage;
    ikea::bytes bytes;
    std::size_t count;
};
template <class U>
[[nodiscard]] std::expected<erased_reader<U>, error> bind_reader(const dense_input& source);
extern template std::expected<erased_reader<std::uint8_t>, error> bind_reader(const dense_input&);
extern template std::expected<erased_reader<std::uint16_t>, error> bind_reader(const dense_input&);
extern template std::expected<erased_reader<std::uint32_t>, error> bind_reader(const dense_input&);
extern template std::expected<erased_reader<std::uint64_t>, error> bind_reader(const dense_input&);
/// The same runtime binding with the layout's compiled range operations
/// (erased_reader::compiled_ranges()): sum, masked and weighted sums, counts
/// and filter words run native kernels rather than decoding. Linking it
/// retains every layout's tables (several MB of code); bind_reader does not.
template <class U>
[[nodiscard]] std::expected<erased_reader<U>, error> bind_range_reader(const dense_input& source);
extern template std::expected<erased_reader<std::uint8_t>, error> bind_range_reader(const dense_input&);
extern template std::expected<erased_reader<std::uint16_t>, error> bind_range_reader(const dense_input&);
extern template std::expected<erased_reader<std::uint32_t>, error> bind_range_reader(const dense_input&);
extern template std::expected<erased_reader<std::uint64_t>, error> bind_range_reader(const dense_input&);
} // namespace ikea::seriespack
