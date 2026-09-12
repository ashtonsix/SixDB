#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace tuple_runtime {
using byte = std::uint8_t;
using mapping = std::array<byte, 64>;
struct code { byte offset, shift, width; };
struct schema {
    unsigned bytes;
    std::span<const code> codes;
};
enum class error { schema, map, duplicate_writer, value };

// Common information is resolved before invocation. Native bodies use explicit
// ISA code; these are shuffle controls, not an abstract vector instruction set.
struct alignas(64) shuffle {
    std::array<byte, 64> index{}, mask{};
    std::array<std::int8_t, 64> shift{};
    std::array<byte, 64> bit_index{};
    std::array<std::array<byte, 64>, 4> avx2_index{};
    std::array<std::uint16_t, 32> even_factor{}, odd_factor{};
    unsigned routes = 0;
    bool shifting = false;
    bool masking = false;
};
struct chunk { byte offset, bytes; };
struct read_plan {
    shuffle operation;
    std::array<chunk, 4> chunks{};
    unsigned count = 0;
    std::uint64_t issued_reads = 0;
    // Exact scalar gathers share code semantics, not the SIMD load envelope.
    std::array<code, 64> scalar{};
};
struct write_byte {
    byte offset = 0, mask = 0, count = 0;
    std::array<byte, 8> input{}, shift{};
};
struct write_plan {
    // A conservative first representation; preparation size is measured. Each
    // round contributes at most one code to each destination byte.
    std::array<shuffle, 8> rounds{};
    std::array<write_byte, 64> stores{};
    std::array<byte, 64> invalid_bits{}, preserve{};
    unsigned count = 0, round_count = 0, bytes = 0;
    bool dense_native = false;
    bool needs_old = false;
    std::uint64_t issued_writes = 0;
};

std::expected<read_plan, error> prepare_read(schema, const mapping&);
std::expected<write_plan, error> prepare_write(schema, const mapping&);

// Read holes are zero in this experiment. The bitwise reference is independent
// of prepared permutation/shift controls and honors the ordered outer map.
std::array<byte, 64> reference_read(schema, const mapping&, const byte* row);
void reference_write(schema, const mapping&, byte* row, const std::array<byte, 64>&);
std::uint64_t read8(const read_plan&, const byte* row);
std::array<byte, 64> read_scalar(const read_plan&, const byte* row);

// Failure changes neither data nor this call's accumulated byte coverage.
// Input is a value, so it cannot alias a destination overwritten by this call.
// Issued byte positions are relative to this tuple; the owner translates them.
std::expected<void, error> write_scalar(const write_plan&, byte* row,
                                       std::array<byte, 64> input, std::uint64_t& effects);
} // namespace tuple_runtime
