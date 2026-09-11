#pragma once

#include <ikea/seriespack/view.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <type_traits>

namespace ikea::seriespack {

/// Half-open original-array positions. size() assumes begin <= end.
struct index_range {
    std::size_t begin = 0;
    std::size_t end = 0;
    constexpr std::size_t size() const { return end - begin; } // admitted range
    constexpr bool empty() const { return begin == end; }
};

[[nodiscard]] constexpr std::expected<void, error>
validate_range(index_range rows, std::size_t size) {
    if (rows.begin > rows.end || rows.end > size)
        return std::unexpected(error::invalid_range);
    return {};
}

/// Borrowed positional bitmap: bit zero names origin. Keep words stable during use.
class selection {
public:
    static constexpr selection all() { return {}; }
    /// Unchecked construction; validate coverage before calling contains.
    static constexpr selection bitmap(std::size_t origin, std::size_t count,
                                      std::span<const std::uint64_t> words) {
        return selection(origin, count, words);
    }
    constexpr bool is_all() const { return all_; }
    constexpr std::size_t origin() const { return origin_; }
    constexpr std::size_t count() const { return count_; }
    constexpr std::span<const std::uint64_t> words() const { return words_; }
    /// Unchecked original index within the validated bitmap coverage.
    constexpr bool contains(std::size_t index) const {
        if (all_) return true;
        auto bit = index - origin_;
        return (words_[bit / 64] >> (bit % 64)) & 1;
    }
    /// Checks descriptor and row coverage, not a view's logical length.
    [[nodiscard]] constexpr std::expected<void, error> validate(index_range rows) const {
        if (rows.begin > rows.end) return std::unexpected(error::invalid_range);
        if (all_) return {};
        if (count_ > std::numeric_limits<std::size_t>::max() - origin_ ||
            words_.size() < count_ / 64 + (count_ % 64 != 0))
            return std::unexpected(error::invalid_selection);
        if (rows.begin < origin_ || rows.end > origin_ + count_)
            return std::unexpected(error::invalid_selection);
        return {};
    }
private:
    constexpr selection() = default;
    constexpr selection(std::size_t origin, std::size_t count,
                        std::span<const std::uint64_t> words)
        : all_(false), origin_(origin), count_(count), words_(words) {}
    bool all_ = true;
    std::size_t origin_ = 0;
    std::size_t count_ = 0;
    std::span<const std::uint64_t> words_;
};

/// Issued-write coverage, including shared RMW bytes; excludes stride gaps.
/// Duplicate spans are allowed. Addresses borrow destination residency.
struct byte_span {
    std::byte* data = nullptr;
    std::size_t size = 0;
};
/// Appends to the existing prefix; rejection leaves it unchanged. Record storage
/// and this state must be disjoint from destination, inputs and selection.
struct effect_output {
    std::span<byte_span> storage;
    std::size_t size = 0; // Existing record count.
};

enum class element_width : unsigned { u8 = 8, u16 = 16, u32 = 32, u64 = 64 };
template<class T>
concept unsigned_element = std::same_as<T, std::uint8_t> ||
    std::same_as<T, std::uint16_t> || std::same_as<T, std::uint32_t> ||
    std::same_as<T, std::uint64_t>;
/// Borrowed unsigned elements. Narrower carriers are valid; values must fit stored k.
struct input_values {
    const void* data;
    std::size_t size;
    element_width width;
    template<class T, std::size_t N> requires unsigned_element<std::remove_const_t<T>>
    constexpr input_values(std::span<T, N> values)
        : data(values.data()), size(values.size()),
          width(static_cast<element_width>(sizeof(T) * 8)) {}
};
/// Borrowed element capacity; its carrier must cover the complete stored k-bit domain.
struct output_values {
    void* data;
    std::size_t size;
    element_width width;
    template<unsigned_element T, std::size_t N>
    constexpr output_values(std::span<T, N> values)
        : data(values.data()), size(values.size()),
          width(static_cast<element_width>(sizeof(T) * 8)) {}
};

/// Compiled availability, not runtime CPU probing; explicit unavailable targets fail.
enum class execution_target { automatic, scalar, avx2, avx512, neon };
/// Point-addressing strategy, independent of the bulk execution target.
enum class point_reader { arithmetic, constant_offsets };

/// Upper bound on additional construction-effect records, including final-tile slack.
[[nodiscard]] std::expected<std::size_t, error> encode_effect_capacity(const_view destination);
/// Upper bound on additional write-effect records; validates rows/selection and counts bits.
[[nodiscard]] std::expected<std::size_t, error> write_effect_capacity(const_view destination,
    index_range rows, selection selected = selection::all());

// Checked calls reject before any output mutation.
[[nodiscard]] std::expected<std::uint64_t, error> get(const_view source, std::size_t index);
[[nodiscard]] std::expected<void, error> set(mutable_view destination, std::size_t index,
                                           std::uint64_t value, effect_output* effects = nullptr);
/// Materializes original position i at output[i - rows.begin].
[[nodiscard]] std::expected<void, error> decode(const_view source, index_range rows,
    output_values output, execution_target target = execution_target::automatic);
/// Constructs every value and zeroes final-tile slack; preserves stride gaps.
[[nodiscard]] std::expected<void, error> encode(mutable_view destination, input_values input,
    effect_output* effects = nullptr, execution_target target = execution_target::automatic);
/// Replaces selected i from input[i - rows.begin]; input covers rows.size() slots.
/// Only selected values are read and checked. Unselected values remain unchanged.
[[nodiscard]] std::expected<void, error> write(mutable_view destination, index_range rows,
    input_values input, selection selected = selection::all(), effect_output* effects = nullptr);

/// Selected compiled endpoints over a borrowed view. Calls are trusted: binding
/// checks endpoint availability, not future arguments or storage lifetime.
class bound_reader {
public:
    using point_function = std::uint64_t (*)(const const_view&, std::size_t);
    using decode_function = void (*)(const const_view&, index_range, void*, element_width);
    /// Unchecked construction; endpoints must implement the actual source description.
    static bound_reader assume_valid(const_view source, execution_target target,
                                     point_function point, decode_function bulk,
                                     point_reader strategy = point_reader::arithmetic) {
        return bound_reader(source, target, point, bulk, strategy);
    }
    /// Requires index < source().size().
    std::uint64_t get(std::size_t index) const { return point_(source_, index); }
    /// Requires valid rows, sufficient output capacity/domain, and disjoint storage.
    /// output.size() is not checked; mapping matches the checked decode function.
    void decode(index_range rows, output_values output) const {
        bulk_(source_, rows, output.data, output.width);
    }
    execution_target target() const { return target_; }
    point_reader point_strategy() const { return point_strategy_; }
    const const_view& source() const { return source_; }
private:
    bound_reader(const_view source, execution_target target, point_function point,
                 decode_function bulk, point_reader strategy)
        : source_(source), target_(target), point_strategy_(strategy), point_(point), bulk_(bulk) {}
    const_view source_;
    execution_target target_;
    point_reader point_strategy_;
    point_function point_;
    decode_function bulk_;
};
[[nodiscard]] std::expected<bound_reader, error> bind_reader(const_view source,
    execution_target target = execution_target::automatic,
    point_reader reader = point_reader::arithmetic);

/// Trusted full construction over borrowed storage; initializes final-tile slack.
class bound_encoder {
public:
    using encode_function = void (*)(const mutable_view&, const void*, element_width, effect_output*);
    /// Unchecked construction; the endpoint must implement the actual destination description.
    static bound_encoder assume_valid(mutable_view destination, execution_target target,
                                      encode_function encode) {
        return bound_encoder(destination, target, encode);
    }
    /// Requires sufficient input with values fitting k, disjoint input/destination,
    /// and isolation of shared writes. Optional effects need sufficient disjoint slots.
    /// See encode_effect_capacity; no checks are repeated here.
    void encode(input_values input, effect_output* effects = nullptr) const {
        encode_(destination_, input.data, input.width, effects);
    }
    execution_target target() const { return target_; }
    const mutable_view& destination() const { return destination_; }
private:
    bound_encoder(mutable_view destination, execution_target target, encode_function encode)
        : destination_(destination), target_(target), encode_(encode) {}
    mutable_view destination_;
    execution_target target_;
    encode_function encode_;
};
[[nodiscard]] std::expected<bound_encoder, error> bind_encoder(mutable_view destination,
    execution_target target = execution_target::automatic);

/// Arithmetic law modulo 2^64; does not prescribe a scalar or native result type.
struct modulo_u64_sum {};

} // namespace ikea::seriespack
