#pragma once
#include <ikea/bec/bec256.h>
#include <ikea/bec/detail/bec4096.h>
#include <ikea/bitset4096.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace ikea::bec4096 {
namespace detail {
struct access;
}
using ikea::byte;
using bec256::describe;
using bec256::error;
using bec256::view;
inline constexpr unsigned positions = 4096, blocks = 16,
                          max_bytes = (detail::body_max_bits + 7) / 8, scratch_bytes = 64;
static_assert(max_bytes == 784);

/// Validates exact headless framing: head, lengths, every block stream,
/// padding and length. Population is caller-owned interpretation metadata.
[[nodiscard]] std::expected<void, error> validate(std::span<const byte> body,
                                                  unsigned population) noexcept;

/// Borrowed placement, before any encoded-content proof. The supplied suffix is
/// initialized readable storage, not writable padding or part of the body.
class const_view {
    std::span<const byte> storage_;
    unsigned bytes_, population_;
    const_view(std::span<const byte> storage, unsigned bytes, unsigned population)
        : storage_(storage), bytes_(bytes), population_(population) {}

  public:
    /// Checks population and byte extents without examining encoded contents.
    [[nodiscard]] static std::expected<const_view, error>
    attach(std::span<const byte> storage, unsigned bytes, unsigned population) noexcept;
    /// Reuses only placement checks, never a content-validation proof.
    static const_view attach_unchecked(std::span<const byte> storage, unsigned bytes,
                                       unsigned population) noexcept {
        return const_view(storage, bytes, population);
    }
    std::span<const byte> storage() const noexcept { return storage_; }
    unsigned bytes() const noexcept { return bytes_; }
    unsigned population() const noexcept { return population_; }
    [[nodiscard]] std::expected<void, error> validate() const noexcept;
};

/// Decoded head: each block's population and the bit offset of its stream.
struct frame {
    std::array<std::uint16_t, 16> population, start;
};

/// Reusable admitted source. Binding validates contents once and retains the
/// decoded frame by value, so block, count and rank access never reread the
/// head. Placement, contents and their association stay unchanged meanwhile.
class reader {
    const_view source_;
    struct frame frame_;
    reader(const_view input, const struct frame &f) : source_(input), frame_(f) {}
    friend struct detail::access;

  public:
    std::span<const byte> storage() const noexcept { return source_.storage(); }
    unsigned bytes() const noexcept { return source_.bytes(); }
    unsigned population() const noexcept { return source_.population(); }
    /// Population of one 256-position block, from the retained frame.
    unsigned population(unsigned block) const noexcept { return frame_.population[block]; }
    const struct frame &frame() const noexcept { return frame_; }
};
[[nodiscard]] std::expected<reader, error> bind_reader(const_view) noexcept;
/// Caller retains a content proof for this exact population, length and bytes;
/// decodes the frame only.
reader bind_reader_unchecked(const_view) noexcept;
[[nodiscard]] std::expected<reader, error>
bind_reader(std::span<const byte> storage, unsigned bytes, unsigned population) noexcept;
inline reader bind_reader_unchecked(std::span<const byte> storage, unsigned bytes,
                                    unsigned population) noexcept {
    return bind_reader_unchecked(const_view::attach_unchecked(storage, bytes, population));
}

/// Materializes all 512 bytes. Output may not overlap the source.
void decode(const reader &, std::span<byte, 512> output) noexcept;
/// One 256-position block, without reading other blocks' streams.
void decode(const reader &, unsigned block, std::span<byte, 32> output) noexcept;
/// Populations of one block's 32 bytes, from its count tree alone.
void counts(const reader &, unsigned block, std::span<std::uint8_t, 32> output) noexcept;
/// Populations of all 512 bytes, from the count tree alone.
void counts(const reader &, std::span<std::uint8_t, 512> output) noexcept;
/// Set positions strictly before position; rank(r, 4096) is the population.
unsigned rank(const reader &, unsigned position) noexcept;
/// Set positions in [first, first + count).
unsigned population(const reader &, unsigned first, unsigned count) noexcept;

/// Transient encoded bytes for one complete value, prepared without owner
/// writes so exact capacity and effects are known before admission.
class encoding {
    alignas(64) std::array<byte, max_bytes + 64> data_;
    unsigned bytes_ = 0;
    friend struct detail::access;

  public:
    [[nodiscard]] static std::expected<encoding, error> make(std::span<const byte, 512> input,
                                                             unsigned population) noexcept;
    [[nodiscard]] static std::expected<encoding, error> make(const bitset4096 &input,
                                                             unsigned population) noexcept;
    unsigned bytes() const noexcept { return bytes_; }
    unsigned effect_capacity() const noexcept { return bytes_ ? 1u : 0u; }
    std::span<const byte> body() const noexcept { return {data_.data(), bytes_}; }
    /// Checks capacity and command/effect aliasing, then writes exactly bytes().
    [[nodiscard]] std::expected<void, error> write(view &, std::size_t offset,
                                                   journal &) const noexcept;
    [[nodiscard]] std::expected<void, error> admit_write(const view &, std::size_t offset,
                                                         const journal &) const noexcept;
    template <class Coverage> requires ikea::effects<Coverage, view>
    [[gnu::always_inline]] void write_unchecked(view &target, std::size_t offset,
                                                Coverage &effects) const noexcept {
        if (!bytes_)
            return;
        effects.before(target, 0, offset, bytes_);
        __builtin_memcpy(target.storage.data() + offset, data_.data(), bytes_);
    }
};

/// Checked construction/replacement. Returns emitted byte length; rejection
/// leaves destination and journal unchanged. Input may not overlap the
/// destination; other command metadata must be disjoint.
[[nodiscard]] std::expected<unsigned, error> encode(std::span<const byte, 512> input,
                                                    unsigned population, view &,
                                                    std::size_t offset, journal &) noexcept;

// Ordinary values use numeric position words; byte spans are the wire bridge.
inline void decode(const reader &input, bitset4096 &output) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        decode(input, std::as_writable_bytes(std::span{output.words}));
    else {
        std::array<byte, 512> wire;
        decode(input, wire);
        output = bitset4096::load(wire);
    }
}
inline void decode(const reader &input, unsigned block, bitset256 &output) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        decode(input, block, std::as_writable_bytes(std::span{output.words}));
    else {
        std::array<byte, 32> wire;
        decode(input, block, wire);
        output = bitset256::load(wire);
    }
}
inline std::expected<encoding, error> encoding::make(const bitset4096 &input,
                                                     unsigned population) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        return make(std::as_bytes(std::span{input.words}), population);
    else {
        std::array<byte, 512> wire;
        input.store(wire);
        return make(wire, population);
    }
}
[[nodiscard]] inline std::expected<unsigned, error>
encode(const bitset4096 &input, unsigned population, view &target, std::size_t offset,
       journal &effects) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        return encode(std::as_bytes(std::span{input.words}), population, target, offset, effects);
    else {
        std::array<byte, 512> wire;
        input.store(wire);
        return encode(wire, population, target, offset, effects);
    }
}
} // namespace ikea::bec4096
