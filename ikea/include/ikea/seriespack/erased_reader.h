#pragma once
#include <ikea/comparison.h>
#include <ikea/seriespack/status.h>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <type_traits>

namespace ikea::seriespack {
/// Whole range operations compiled once per physical layout (see
/// bind_reader(dense_input)). Results do not depend on the reader's output
/// type. Arguments are as the erased_reader methods that call them.
struct range_operations {
    std::uint64_t (*sum)(const void*, std::size_t first, std::size_t count);
    std::uint64_t (*sum_masked)(const void*, std::size_t first, std::size_t count, const std::uint16_t* masks,
                                std::size_t period);
    std::uint64_t (*count)(const void*, comparison, std::size_t first, std::size_t count, std::uint64_t rhs);
    void (*filter_words)(const void*, comparison, std::size_t first, std::size_t count, std::uint64_t rhs,
                         std::uint64_t* words);
    unsigned __int128 (*sum_exact)(const void*, std::size_t first, std::size_t count);
    unsigned __int128 (*sum_masked_exact)(const void*, std::size_t first, std::size_t count, const std::uint16_t* masks,
                                          std::size_t period);
    unsigned __int128 (*weighted_sum)(const void*, std::size_t first, std::size_t count, const std::uint8_t* weights,
                                      unsigned period);
};
/// Borrowed read operation. The source bytes and any named view used at binding
/// remain alive and unchanged in placement through each call. Coordinates count
/// original logical values, not bytes or filtered ranks. Output is disjoint from
/// occupied source bytes and binding metadata. No call allocates or suspends.
template <class U> class erased_reader {
    static_assert(std::is_integral_v<U> && std::is_unsigned_v<U> && !std::is_same_v<U, bool> &&
                  sizeof(U) <= 8);

  public:
    using function = void (*)(const void*, std::size_t, std::size_t, U*);
    using point_function = U (*)(const void*, std::size_t);
    using region_function = void (*)(const void*, std::size_t, U*);
    /// Adapter construction after placement, readable extents and kernel
    /// contracts have been admitted. Prefer bind_reader at ordinary call sites.
    /// operations, when given, are the layout's compiled range operations over
    /// the same resource; without them the range methods decode and reduce.
    erased_reader(const void* resource, std::size_t count, function range, region_function region,
            point_function point, const range_operations* operations = nullptr)
        : resource_(resource), range_(range), region_(region), point_(point), operations_(operations),
          count_(count) {}
    /// The same reader with a layout's compiled range operations over the same
    /// resource; adapter construction like the constructor.
    erased_reader with_operations(const range_operations* operations) const {
        auto out = *this;
        out.operations_ = operations;
        return out;
    }
    std::size_t size() const {
        return count_;
    }
    /// Checked point read. An out-of-range coordinate reads no source bytes.
    [[nodiscard]] std::expected<U, error> get(std::size_t row) const {
        if (row >= count_)
            return std::unexpected(error::range);
        return get_unchecked(row);
    }
    /// Checked range; output length determines count. Failure leaves output
    /// unchanged. Does not prove output/source disjointness or owner lifetime.
    [[nodiscard]] std::expected<void, error> read(std::size_t first, std::span<U> output) const {
        if (first > count_ || output.size() > count_ - first)
            return std::unexpected(error::range);
        read_unchecked(first, output.size(), output.data());
        return {};
    }
    /// Trusted range: first+count is within size(), and count output slots are
    /// writable and disjoint. Arbitrary scalar edges are supported.
    void read_unchecked(std::size_t first, std::size_t count, U* __restrict output) const {
        range_(resource_, first, count, output);
    }
    /// Trusted point: row<size(); no range or lifetime check is repeated.
    U get_unchecked(std::size_t row) const {
        return point_(resource_, row);
    }
    /// Trusted native region: first is divisible by 16 and all sixteen logical
    /// values are in range. All sixteen output slots are writable and disjoint.
    void read16_unchecked(std::size_t first, U* __restrict output) const {
        region_(resource_, first, output);
    }

    /// The point kernel and its resource, for a caller that fuses its own
    /// point path: point_kernel()(resource(), row) is get_unchecked(row).
    point_function point_kernel() const {
        return point_;
    }
    const void* resource() const {
        return resource_;
    }
    /// Whether the range methods run compiled range operations rather than
    /// decoding and reducing. The answers are the same either way.
    bool compiled_ranges() const {
        return operations_ != nullptr;
    }
    /// Trusted: the sum, modulo 2^64, of every row of [first, first + count).
    std::uint64_t sum_unchecked(std::size_t first, std::size_t count) const {
        if (operations_)
            return operations_->sum(resource_, first, count);
        std::uint64_t s = 0;
        decoded(first, count, [&](std::size_t, U x) { s += x; });
        return s;
    }
    /// Trusted: the sum, modulo 2^64, of the selected rows of [first, first +
    /// count). Bit j of masks[k] selects row 16 * (first / 16 + k) + j; with a
    /// nonzero period, masks[k % period] is used instead (a repeating
    /// pattern). masks covers every 16-row region the range touches.
    std::uint64_t sum_masked_unchecked(std::size_t first, std::size_t count, const std::uint16_t* masks,
                                       std::size_t period = 0) const {
        if (operations_)
            return operations_->sum_masked(resource_, first, count, masks, period);
        std::uint64_t s = 0;
        const std::size_t base = first / 16;
        decoded(first, count, [&](std::size_t row, U x) {
            const std::size_t k = row / 16 - base;
            s += (masks[period ? k % period : k] >> (row % 16) & 1) ? x : 0;
        });
        return s;
    }
    /// Trusted: the exact sum of every row of [first, first + count), as u128.
    unsigned __int128 sum_exact_unchecked(std::size_t first, std::size_t count) const {
        if (operations_)
            return operations_->sum_exact(resource_, first, count);
        unsigned __int128 s = 0;
        decoded(first, count, [&](std::size_t, U x) { s += x; });
        return s;
    }
    /// Trusted: sum_masked_unchecked's selection, summed exactly as u128.
    unsigned __int128 sum_masked_exact_unchecked(std::size_t first, std::size_t count, const std::uint16_t* masks,
                                                 std::size_t period = 0) const {
        if (operations_)
            return operations_->sum_masked_exact(resource_, first, count, masks, period);
        unsigned __int128 s = 0;
        const std::size_t base = first / 16;
        decoded(first, count, [&](std::size_t row, U x) {
            const std::size_t k = row / 16 - base;
            s += (masks[period ? k % period : k] >> (row % 16) & 1) ? x : 0;
        });
        return s;
    }
    /// Trusted: the exact sum, as u128, of weights[row % period] * value over
    /// [first, first + count). period is 16 or 64, weights holds period
    /// entries indexed by the absolute row; an empty range reads none. The
    /// caller proves the weighted total fits u128.
    unsigned __int128 weighted_sum_unchecked(std::size_t first, std::size_t count, const std::uint8_t* weights,
                                             unsigned period = 16) const {
        if (operations_)
            return operations_->weighted_sum(resource_, first, count, weights, period);
        unsigned __int128 s = 0;
        decoded(first, count, [&](std::size_t row, U x) { s += static_cast<unsigned __int128>(x) * weights[row % period]; });
        return s;
    }
    /// Trusted: rows of [first, first + count) satisfying op against rhs.
    std::uint64_t count_unchecked(comparison op, std::size_t first, std::size_t count, std::uint64_t rhs) const {
        if (operations_)
            return operations_->count(resource_, op, first, count, rhs);
        std::uint64_t n = 0;
        decoded(first, count, [&](std::size_t, U x) { n += compare(op, x, rhs); });
        return n;
    }
    /// Trusted: bit i of words[i / 64] is whether row first + i satisfies op;
    /// exactly count / 64 + (count % 64 != 0) words are written, the last
    /// word's unused high bits zero.
    void filter_words_unchecked(comparison op, std::size_t first, std::size_t count, std::uint64_t rhs,
                                std::uint64_t* __restrict words) const {
        if (operations_)
            return operations_->filter_words(resource_, op, first, count, rhs, words);
        std::fill(words, words + count / 64 + (count % 64 != 0), std::uint64_t{0});
        decoded(first, count, [&](std::size_t row, U x) {
            const auto i = row - first;
            words[i / 64] |= std::uint64_t{compare(op, x, rhs)} << (i % 64);
        });
    }
    [[nodiscard]] std::expected<std::uint64_t, error> sum(std::size_t first, std::size_t count) const {
        if (first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        return sum_unchecked(first, count);
    }
    [[nodiscard]] std::expected<std::uint64_t, error> count(comparison op, std::size_t first, std::size_t count,
                                                            std::uint64_t rhs) const {
        if (first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        return count_unchecked(op, first, count, rhs);
    }
    /// Checked filter_words_unchecked; failure leaves words unchanged.
    [[nodiscard]] std::expected<void, error> filter_words(comparison op, std::size_t first, std::size_t count,
                                                          std::uint64_t rhs, std::span<std::uint64_t> words) const {
        if (first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        if (words.size() < count / 64 + (count % 64 != 0))
            return std::unexpected(error::capacity);
        filter_words_unchecked(op, first, count, rhs, words.data());
        return {};
    }

  private:
    static bool compare(comparison op, std::uint64_t x, std::uint64_t rhs) {
        switch (op) {
        case comparison::equal: return x == rhs;
        case comparison::less: return x < rhs;
        case comparison::less_equal: return x <= rhs;
        case comparison::greater: return x > rhs;
        case comparison::greater_equal: return x >= rhs;
        }
        return false;
    }
    // Decoded rows of [first, first + count) in 256-row chunks.
    template <class F> void decoded(std::size_t first, std::size_t count, F&& f) const {
        U chunk[256];
        for (std::size_t at = 0; at < count; at += 256) {
            const auto n = std::min<std::size_t>(256, count - at);
            read_unchecked(first + at, n, chunk);
            for (std::size_t i = 0; i < n; ++i)
                f(first + at + i, chunk[i]);
        }
    }
    const void* resource_;
    function range_;
    region_function region_;
    point_function point_;
    const range_operations* operations_;
    std::size_t count_;
};
} // namespace ikea::seriespack
