#include "model.h"
#include <algorithm>
#include <bit>

namespace tuple_runtime {
namespace {
std::expected<void, error> validate(schema s, const mapping& m, bool writing) {
    if (!s.bytes || s.bytes > 64 || s.codes.empty() || s.codes.size() > 128)
        return std::unexpected(error::schema);
    std::array<byte, 64> used{};
    for (auto c : s.codes) {
        if (c.offset >= s.bytes || !c.width || c.width > 8 || c.shift + c.width > 8)
            return std::unexpected(error::schema);
        const byte bits = ((1u << c.width) - 1) << c.shift;
        if (used[c.offset] & bits)
            return std::unexpected(error::schema);
        used[c.offset] |= bits;
    }
    std::array<bool, 128> mapped{};
    for (auto rank : m) {
        if (rank == 255) continue;
        if (rank >= s.codes.size()) return std::unexpected(error::map);
        if (writing && mapped[rank]) return std::unexpected(error::duplicate_writer);
        mapped[rank] = true;
    }
    return {};
}
void finish(shuffle& p, bool left) {
    for (auto& indices : p.avx2_index) indices.fill(255);
    for (unsigned i = 0; i < 64; ++i) {
        if (p.mask[i]) {
            p.routes |= 1u << (p.index[i] / 16);
            p.avx2_index[p.index[i] / 16][i] = p.index[i] % 16;
            p.shifting |= p.shift[i] != 0;
        }
        p.masking |= p.mask[i] != 255;
        // VPMULTISHIFTQB chooses a circular bit window within each 64-bit lane.
        // The final mask removes bits arriving from an adjacent byte.
        p.bit_index[i] = (8 * (i % 8) - p.shift[i]) & 63;
        const unsigned shift = left ? p.shift[i] : -p.shift[i];
        const unsigned scale = left ? 1u << shift : 1u << (8 - shift);
        (i & 1 ? p.odd_factor : p.even_factor)[i / 2] = scale;
    }
}
} // namespace

std::expected<read_plan, error> prepare_read(schema s, const mapping& m) {
    if (auto valid = validate(s, m, false); !valid) return std::unexpected(valid.error());
    read_plan p;
    unsigned chunks = 0;
    for (auto rank : m)
        if (rank != 255) chunks |= 1u << (s.codes[rank].offset / 16);
    std::array<byte, 4> compact{};
    for (unsigned i = 0; i < 4; ++i) {
        if (!(chunks & (1u << i))) continue;
        compact[i] = p.count;
        const unsigned length = std::min(16u, s.bytes - i * 16);
        p.chunks[p.count++] = {byte(i * 16), byte(length)};
        for (unsigned j = 0; j < length; ++j)
            p.issued_reads |= std::uint64_t(1) << (i * 16 + j);
    }
    for (unsigned i = 0; i < 64; ++i) {
        if (m[i] == 255) continue;
        const auto c = s.codes[m[i]];
        p.scalar[i] = c;
        p.operation.index[i] = compact[c.offset / 16] * 16 + c.offset % 16;
        p.operation.mask[i] = (1u << c.width) - 1;
        p.operation.shift[i] = -c.shift;
    }
    finish(p.operation, false);
    return p;
}

std::expected<write_plan, error> prepare_write(schema s, const mapping& m) {
    if (auto valid = validate(s, m, true); !valid) return std::unexpected(valid.error());
    write_plan p;
    p.bytes = s.bytes;
    p.preserve.fill(255);
    std::array<write_byte, 64> by_byte{};
    for (unsigned i = 0; i < 64; ++i) {
        if (m[i] == 255) continue;
        const auto c = s.codes[m[i]];
        const byte mask = ((1u << c.width) - 1) << c.shift;
        p.invalid_bits[i] = byte(~((1u << c.width) - 1));
        p.preserve[c.offset] &= byte(~mask);
        p.issued_writes |= std::uint64_t(1) << c.offset;
        auto& b = by_byte[c.offset];
        const unsigned round = b.count++;
        b.offset = c.offset;
        b.mask |= mask;
        b.input[round] = i;
        b.shift[round] = c.shift;
        auto& r = p.rounds[round];
        r.index[c.offset] = i;
        r.mask[c.offset] = mask;
        r.shift[c.offset] = c.shift;
        p.round_count = std::max(p.round_count, round + 1);
    }
    for (auto b : by_byte)
        if (b.count) p.stores[p.count++] = b;
    for (unsigned i = 0; i < p.round_count; ++i) finish(p.rounds[i], true);
    // Sparse point maps use the byte-coalesced scalar control. Dense native
    // maps cover every byte, with exact bounded prefix loads and stores.
    p.dense_native = p.count == s.bytes;
    for (unsigned i = 0; i < s.bytes; ++i) p.needs_old |= p.preserve[i] != 0;
    return p;
}

std::array<byte, 64> reference_read(schema s, const mapping& m, const byte* row) {
    std::array<byte, 64> result{};
    for (unsigned i = 0; i < 64; ++i) {
        if (m[i] == 255) continue;
        const auto c = s.codes[m[i]];
        for (unsigned bit = 0; bit < c.width; ++bit)
            result[i] |= ((row[c.offset] >> (c.shift + bit)) & 1u) << bit;
    }
    return result;
}
void reference_write(schema s, const mapping& m, byte* row, const std::array<byte, 64>& input) {
    for (unsigned i = 0; i < 64; ++i) {
        if (m[i] == 255) continue;
        const auto c = s.codes[m[i]];
        for (unsigned bit = 0; bit < c.width; ++bit) {
            const byte mask = 1u << (c.shift + bit);
            row[c.offset] = (row[c.offset] & ~mask) | (((input[i] >> bit) & 1u) << (c.shift + bit));
        }
    }
}
std::uint64_t read8(const read_plan& p, const byte* row) {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) {
        auto c = p.scalar[i];
        if (c.width)
            result |= std::uint64_t((row[c.offset] >> c.shift) & ((1u << c.width) - 1)) << (i * 8);
    }
    return result;
}
std::array<byte, 64> read_scalar(const read_plan& p, const byte* row) {
    std::array<byte, 64> result{};
    for (unsigned i = 0; i < 64; ++i) {
        auto c = p.scalar[i];
        if (c.width) result[i] = (row[c.offset] >> c.shift) & ((1u << c.width) - 1);
    }
    return result;
}
std::expected<void, error> write_scalar(const write_plan& p, byte* row,
                                       std::array<byte, 64> input, std::uint64_t& effects) {
    for (unsigned i = 0; i < 64; ++i)
        if (input[i] & p.invalid_bits[i]) return std::unexpected(error::value);
    for (unsigned i = 0; i < p.count; ++i) {
        const auto& b = p.stores[i];
        byte next = b.mask == 255 ? 0 : row[b.offset] & byte(~b.mask);
        for (unsigned j = 0; j < b.count; ++j) next |= input[b.input[j]] << b.shift[j];
        row[b.offset] = next;
    }
    effects |= p.issued_writes;
    return {};
}
} // namespace tuple_runtime
