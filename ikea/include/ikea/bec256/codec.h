#pragma once
#include <ikea/effects.h>
#include <ikea/bytes.h>
#include <ikea/bitset256.h>
#include <bit>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace ikea::bec256 {
namespace detail {
struct encoding_access;
}
using ikea::byte;
inline constexpr unsigned positions = 256, max_bytes = 47, scratch_bytes = 64;

enum class error {
    population,
    truncated,
    split,
    rank,
    padding,
    trailing,
    capacity,
    effects,
    overlap
};
std::string_view describe(error) noexcept;

/// Validates exact headless framing without reading beyond body. Population is
/// caller-owned interpretation metadata, not authenticated original content.
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

/// Reusable admitted decode operation. Placement/contents and their population
/// association stay unchanged through every call. Metadata is retained by value;
/// storage remains borrowed. Binding scans contents once; decode does not rescan.
class reader {
    const_view source_;
    explicit reader(const_view input) : source_(input) {}
    friend std::expected<reader, error> bind_reader(const_view) noexcept;
    friend reader bind_reader_unchecked(const_view) noexcept;

  public:
    std::span<const byte> storage() const noexcept { return source_.storage(); }
    unsigned bytes() const noexcept { return source_.bytes(); }
    unsigned population() const noexcept { return source_.population(); }
};
[[nodiscard]] std::expected<reader, error> bind_reader(const_view) noexcept;
/// Caller retains a content proof for this exact population, length and bytes.
inline reader bind_reader_unchecked(const_view input) noexcept { return reader(input); }
/// Complete binding convenience: checks placement, then validates exact contents.
[[nodiscard]] std::expected<reader, error>
bind_reader(std::span<const byte> storage, unsigned bytes, unsigned population) noexcept;
inline reader bind_reader_unchecked(std::span<const byte> storage, unsigned bytes,
                                    unsigned population) noexcept {
    return bind_reader_unchecked(const_view::attach_unchecked(storage, bytes, population));
}

/// Materializes exactly 32 bytes. Output may overlap the source: decoding
/// completes before output is written. No framing scan after source admission.
void decode(const reader &, std::span<byte, 32> output) noexcept;
/// Low/high output halves correspond to the two independent input arguments.
/// Both bodies are decoded before any output store, permitting byte overlap.
void decode_pair(const reader &, const reader &, std::span<byte, 64> output) noexcept;

/// Caller-owned destination plane. Effects use this named object's identity and
/// byte offsets relative to storage. Keep it alive until effects are resolved.
struct view {
    std::span<byte> storage;
    std::span<const byte> plane_storage(unsigned) const noexcept { return storage; }
};

/// Transient encoded bytes for one complete block, not a persisted header or a
/// storage owner. The caller retains population and records bytes() separately.
/// Preparing before admission makes exact capacity and effects known without
/// exposing a partly written destination. It also permits overlapping input.
class encoding {
    alignas(64) std::array<byte, scratch_bytes> data_;
    unsigned bytes_ = 0;
    friend struct detail::encoding_access;

  public:
    /// Checks population and prepares exact bytes privately, without owner writes.
    [[nodiscard]] static std::expected<encoding, error>
    make(std::span<const byte, 32> input, unsigned population) noexcept;
    [[nodiscard]] static std::expected<encoding, error>
    make(const bitset256 &input, unsigned population) noexcept;
    unsigned bytes() const noexcept { return bytes_; }
    unsigned effect_capacity() const noexcept { return bytes_ ? 1u : 0u; }
    std::span<const byte> body() const noexcept { return {data_.data(), bytes_}; }
    /// Checks capacity and command/effect aliasing before any write or effect.
    /// Writes exactly bytes() bytes; following bodies and slack are untouched.
    /// A zero-byte body needs no capacity or journal entry. Caller updates its
    /// population/length/address metadata under the enclosing owner protocol.
    [[nodiscard]] std::expected<void, error> write(view &, std::size_t offset,
                                                   journal &) const noexcept;
    /// Separate cold admission for an enclosing multi-child mutation. Admission
    /// does not reserve capacity: the owner budgets every child before execution.
    [[nodiscard]] std::expected<void, error>
    admit_write(const view &, std::size_t offset,
                const journal &) const noexcept;
    /// Trusted exact-store body shared by ordinary and native composition.
    /// Requires admitted capacity, disjoint command/effect storage, and a no-fail,
    /// nonsuspending coverage hook. Emits no effect for an empty body.
    template <class Coverage> requires ikea::effects<Coverage, view>
    [[gnu::always_inline]] void write_unchecked(view &target, std::size_t offset,
                                                Coverage &effects) const noexcept {
        if (!bytes_)
            return;
        effects.before(target, 0, offset, bytes_);
        __builtin_memcpy(target.storage.data() + offset, data_.data(), bytes_);
    }
};

/// Checked construction/replacement convenience. Returns emitted byte length;
/// no population or length is written into the body. Rejection leaves destination
/// and journal unchanged. All input bits are consumed before the first write,
/// permitting input/destination byte overlap. Other command metadata must be
/// disjoint. The transient prepare/write form is useful across owner waits;
/// this immediate operation does not require that intermediate byte buffer.
[[nodiscard]] std::expected<unsigned, error> encode(std::span<const byte, 32> input,
                                                    unsigned population, view &,
                                                    std::size_t offset,
                                                    journal &) noexcept;

/// Heuristic encode-or-decline. After checking population, a nonterminal block
/// is declined when sum(byte-rank widths) >= enum_bit_cutoff (units: bits,
/// range 0..224). Values >=225 disable this shortcut; zero declines every
/// nonterminal block. Empty/full blocks still encode as zero bytes.
/// Success holds an emitted byte count, or nullopt for a heuristic decline.
/// Decline does not prove incompressibility and performs no destination admission,
/// writes or effects. An error likewise leaves destination and journal unchanged.
/// On acceptance, encode()'s exact-write/overlap/lifetime contract applies.
/// The caller selects the cutoff and any alternative representation.
[[nodiscard]] std::expected<std::optional<unsigned>, error>
encode_if_promising(std::span<const byte, 32> input, unsigned population, unsigned enum_bit_cutoff,
                    view &, std::size_t offset, journal &) noexcept;
// Ordinary bit values use numeric position words. Explicit byte-span overloads
// remain the canonical little-endian wire bridge and support byte-overlap tests.
inline void decode(const reader &input, bitset256 &output) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        decode(input, std::as_writable_bytes(std::span{output.words}));
    else {
        std::array<byte, 32> wire;
        decode(input, wire);
        output = bitset256::load(wire);
    }
}
inline void decode_pair(const reader &a, const reader &b, std::span<bitset256, 2> output) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        decode_pair(a, b, std::as_writable_bytes(output));
    else {
        std::array<byte, 64> wire;
        decode_pair(a, b, wire);
        output[0] = bitset256::load(std::span<const byte, 32>(wire.data(), 32));
        output[1] = bitset256::load(std::span<const byte, 32>(wire.data() + 32, 32));
    }
}
inline std::expected<encoding, error>
encoding::make(const bitset256 &input, unsigned population) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        return make(std::as_bytes(std::span{input.words}), population);
    else {
        std::array<byte, 32> wire;
        input.store(wire);
        return make(wire, population);
    }
}
[[nodiscard]] inline std::expected<unsigned, error>
encode(const bitset256 &input, unsigned population, view &target, std::size_t offset,
       journal &effects) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        return encode(std::as_bytes(std::span{input.words}), population, target, offset, effects);
    else {
        std::array<byte, 32> wire;
        input.store(wire);
        return encode(wire, population, target, offset, effects);
    }
}
[[nodiscard]] inline std::expected<std::optional<unsigned>, error>
encode_if_promising(const bitset256 &input, unsigned population, unsigned enum_bit_cutoff,
                    view &target, std::size_t offset, journal &effects) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        return encode_if_promising(std::as_bytes(std::span{input.words}), population,
                                   enum_bit_cutoff, target, offset, effects);
    else {
        std::array<byte, 32> wire;
        input.store(wire);
        return encode_if_promising(wire, population, enum_bit_cutoff, target, offset, effects);
    }
}
} // namespace ikea::bec256
