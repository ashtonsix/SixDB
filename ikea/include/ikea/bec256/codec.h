#pragma once
#include <ikea/effects.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace ikea::bec256 {
namespace detail {
struct encoding_access;
}
using byte = std::byte;
using plain_block = std::array<byte, 32>;
inline constexpr unsigned positions = 256, max_bytes = 47, scratch_bytes = 64;

enum class error {
    range, // Invalid pipeline stage count.
    value, // Invalid pipeline stage function.
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
const char *describe(error) noexcept;

/// Validates exact headless framing without reading beyond body. Population is
/// caller-owned interpretation metadata, not authenticated original content.
[[nodiscard]] std::expected<void, error> validate(std::span<const byte> body,
                                                  unsigned population) noexcept;

/// Borrowed admitted body. Keep its bytes and population/length association
/// valid through every use; writes or owner remapping can invalidate this proof.
/// storage includes any initialized readable suffix; bytes is the exact body
/// length supplied by the caller. A suffix is optional, never stored per body.
class source {
    std::span<const byte> storage_;
    unsigned bytes_, population_;
    source(std::span<const byte> storage, unsigned bytes, unsigned population)
        : storage_(storage), bytes_(bytes), population_(population) {}

  public:
    /// Trusted parent adapter. The caller has already proved admit()'s framing,
    /// population, extent and initialized-storage requirements for this body.
    /// No validation scan or allocation; the returned view borrows the bytes.
    static source assume_valid(std::span<const byte> storage, unsigned bytes,
                               unsigned population) noexcept {
        return source(storage, bytes, population);
    }
    [[nodiscard]] static std::expected<source, error>
    admit(std::span<const byte> storage, unsigned bytes, unsigned population) noexcept;
    std::span<const byte> storage() const noexcept { return storage_; }
    unsigned bytes() const noexcept { return bytes_; }
    unsigned population() const noexcept { return population_; }
};

/// Materializes exactly 32 bytes. Output may overlap the source: decoding
/// completes before output is written. No framing scan after source admission.
void decode(const source &, std::span<byte, 32> output) noexcept;
/// Low/high output halves correspond to the two independent input arguments.
/// Both bodies are decoded before any output store, permitting byte overlap.
void decode_pair(const source &, const source &, std::span<byte, 64> output) noexcept;

/// Caller-owned destination plane. Effects use this named object's identity and
/// byte offsets relative to storage. Keep it alive until effects are resolved.
struct destination {
    std::span<byte> storage;
};

/// Transient encoded bytes for one complete block, not a persisted header or a
/// storage owner. The caller retains population and records bytes() separately.
/// Preparing before admission makes exact capacity and effects known without
/// exposing a partly written destination. It also permits overlapping input.
class encoding {
    alignas(64) std::array<byte, scratch_bytes> data_;
    unsigned bytes_ = 0;
    friend struct detail::encoding_access;
    friend std::expected<encoding, error> prepare(std::span<const byte, 32>, unsigned) noexcept;

  public:
    unsigned bytes() const noexcept { return bytes_; }
    std::span<const byte> body() const noexcept { return {data_.data(), bytes_}; }
    /// Checks capacity and command/effect aliasing before any write or effect.
    /// Writes exactly bytes() bytes; following bodies and slack are untouched.
    /// A zero-byte body needs no capacity or journal entry. Caller updates its
    /// population/length/address metadata under the enclosing owner protocol.
    [[nodiscard]] std::expected<void, error> write(destination &, std::size_t offset,
                                                   source_write_journal &) const noexcept;
    /// Separate cold admission for an enclosing multi-child mutation. Admission
    /// does not reserve capacity: the owner budgets every child before execution.
    [[nodiscard]] std::expected<void, error>
    admit_write(const destination &, std::size_t offset,
                const source_write_journal &) const noexcept;
    /// Trusted exact-store body shared by ordinary and native composition.
    /// Requires admitted capacity, disjoint command/effect storage, and a no-fail,
    /// nonsuspending coverage hook. Emits no effect for an empty body.
    template <class Coverage>
    [[gnu::always_inline]] void write_unchecked(destination &target, std::size_t offset,
                                                Coverage &effects) const noexcept {
        if (!bytes_)
            return;
        effects.before(target, {0, offset, bytes_});
        __builtin_memcpy(target.storage.data() + offset, data_.data(), bytes_);
    }
};

/// Checks that supplied population equals the 256 input bits, then encodes into
/// private bounded storage. No allocation or writes to caller storage/effects.
[[nodiscard]] std::expected<encoding, error> prepare(std::span<const byte, 32> input,
                                                     unsigned population) noexcept;

/// Checked construction/replacement convenience. Returns emitted byte length;
/// no population or length is written into the body. Rejection leaves destination
/// and journal unchanged. All input bits are consumed before the first write,
/// permitting input/destination byte overlap. Other command metadata must be
/// disjoint. The transient prepare/write form is useful across owner waits;
/// this immediate operation does not require that intermediate byte buffer.
[[nodiscard]] std::expected<unsigned, error> encode(std::span<const byte, 32> input,
                                                    unsigned population, destination &,
                                                    std::size_t offset,
                                                    source_write_journal &) noexcept;

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
                    destination &, std::size_t offset, source_write_journal &) noexcept;
} // namespace ikea::bec256
