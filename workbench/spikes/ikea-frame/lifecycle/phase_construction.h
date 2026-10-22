#pragma once
#include "phase_material.h"
#include "../execution/material-witness/encoded_region.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace frame_lifecycle::phase {
struct GroupData {
    std::array<u64, rows_per_group> value{};
    std::array<u64, flag_words> present{}, valid{};
    GroupData() { present.fill(UINT64_MAX); valid.fill(UINT64_MAX); }
    LogicalRow row(unsigned i) const {
        return {value[i], bool(present[i / 64] >> (i % 64) & 1), bool(valid[i / 64] >> (i % 64) & 1)};
    }
    void set(unsigned i, LogicalRow r) {
        const auto bit = u64{1} << (i % 64);
        present[i / 64] = (present[i / 64] & ~bit) | (r.present ? bit : 0);
        valid[i / 64] = (valid[i / 64] & ~bit) | (r.contributes() ? bit : 0);
        if (r.contributes()) value[i] = r.value;
    }
};
inline u64 fresh_identity() {
    static std::atomic<u64> identity{1};
    const auto next = identity.fetch_add(1, std::memory_order_relaxed);
    if (!next) throw std::overflow_error("phase base identity exhausted");
    return next;
}
inline void summarize_block(GroupHeader& h, unsigned b, std::span<const u64, 256> values,
                            std::span<const u64, 4> present, std::span<const u64, 4> valid) {
    h.present_count[b] = h.valid_count[b] = 0; h.bounds[b] = {};
    for (unsigned w = 0; w < 4; ++w) {
        const auto p = present[w], v = valid[w];
        if (v & ~p) throw std::invalid_argument("validity is not a subset of presence");
        h.present_count[b] += std::popcount(p); h.valid_count[b] += std::popcount(v);
        auto bits = v;
        while (bits) {
            const auto i = w * 64 + std::countr_zero(bits); bits &= bits - 1;
            h.bounds[b].minimum = std::min(h.bounds[b].minimum, values[i]);
            h.bounds[b].maximum = std::max(h.bounds[b].maximum, values[i]);
        }
    }
}
inline GroupHeader summarize(const GroupData& data) {
    GroupHeader h;
    h.identity = fresh_identity();
    for (unsigned b = 0; b < blocks_per_group; ++b)
        summarize_block(h, b, std::span(data.value).subspan(b * 256, 256).first<256>(),
            std::span(data.present).subspan(b * 4, 4).first<4>(),
            std::span(data.valid).subspan(b * 4, 4).first<4>());
    return h;
}
inline void copy_flags(const GroupData& data, std::span<std::byte> target) {
    if (target.size() != flag_bytes * 2) throw std::invalid_argument("bad flag extent");
    std::memcpy(target.data(), data.present.data(), flag_bytes);
    std::memcpy(target.data() + flag_bytes, data.valid.data(), flag_bytes);
}
struct EncodedConstruction {
    fm::EncodedRegion image;
    EncodedBase base;
    EncodedConstruction(const GroupData& data, fe::encode_options options = {})
        : image(data.value, options, 0, 0) {
        base.header = summarize(data); base.header.bytes = 8 + image.metadata.bytes();
        base.checkpoints = image.metadata.checkpoints;
        base.dictionary = image.metadata.dictionary;
        if (base.dictionary.count) base.dictionary.offset += 8;
        for (unsigned b = 0; b < blocks_per_group; ++b) {
            base.blocks[b] = image.metadata.blocks[b].descriptor;
            base.blocks[b].offset = 8 + image.metadata.blocks[b].offset;
            base.grants[b] = image.metadata.blocks[b].grant_bytes;
            base.coded |= unsigned(image.metadata.blocks[b].coded) << b;
        }
    }
    void copy_values(std::span<std::byte> target) const {
        if (target.size() != base.header.bytes) throw std::invalid_argument("bad encoded value extent");
        const u64 occupied = object_tag; std::memcpy(target.data(), &occupied, 8);
        image.copy_into(target.subspan(8));
    }
};
struct NativeConstruction {
    const GroupData& source;
    NativeBase base;
    explicit NativeConstruction(const GroupData& data, bool raw64 = false) : source(data) {
        auto& h = base.header; h = summarize(data);
        u64 low = UINT64_MAX, high = 0;
        for (unsigned b = 0; b < blocks_per_group; ++b) if (h.valid_count[b]) {
            low = std::min(low, h.bounds[b].minimum); high = std::max(high, h.bounds[b].maximum);
        }
        // Even an all-invalid source has a bounded, addressable native lane.
        // A constant/native-null specialization remains a separate control.
        if (low == UINT64_MAX && high == 0) low = 0;
        h.base = raw64 ? 0 : low;
        const auto width = fe::width_of(high - low);
        h.native_bytes = raw64 || width > 32 ? 8 : width > 16 ? 4 : width > 8 ? 2 : 1;
        h.bytes = rows_per_group * h.native_bytes;
    }
    template<class T> void copy(std::span<std::byte> target) const {
        for (unsigned i = 0; i < rows_per_group; ++i) {
            const auto active = source.valid[i / 64] >> (i % 64) & 1;
            const T residual = active ? T(source.value[i] - base.header.base) : 0;
            std::memcpy(target.data() + i * sizeof(T), &residual, sizeof(T));
        }
    }
    void copy_values(std::span<std::byte> target) const {
        if (target.size() != base.header.bytes) throw std::invalid_argument("bad native extent");
        switch (base.header.native_bytes) {
        case 1: return copy<std::uint8_t>(target); case 2: return copy<std::uint16_t>(target);
        case 4: return copy<std::uint32_t>(target); case 8: return copy<u64>(target);
        }
        std::abort();
    }
};
struct OverrideRow { std::uint16_t position; LogicalRow original, current; };
inline unsigned flags(LogicalRow r) { return unsigned(r.present) | unsigned(r.contributes()) * 2; }
inline void copy_overlay(std::span<const OverrideRow> entries, std::span<std::byte> target, u64 basis) {
    const auto n = entries.size();
    if (n > rows_per_group || target.size() != Overlay::bytes(unsigned(n)))
        throw std::invalid_argument("bad overlay extent");
    if (!basis) throw std::invalid_argument("overlay needs a base identity");
    std::memcpy(target.data(), &basis, 8);
    for (unsigned i = 0; i < n; ++i) {
        const auto& e = entries[i];
        if (e.position >= rows_per_group || (i && e.position <= entries[i - 1].position))
            throw std::invalid_argument("overlay positions must be ordered and distinct");
        const std::uint8_t old = flags(e.original), next = flags(e.current);
        std::memcpy(target.data() + 8 + i * 2, &e.position, 2);
        std::memcpy(target.data() + 8 + n * 2 + i, &old, 1);
        std::memcpy(target.data() + 8 + n * 3 + i, &next, 1);
        std::memcpy(target.data() + 8 + n * 4 + i * 8, &e.original.value, 8);
        std::memcpy(target.data() + 8 + n * 12 + i * 8, &e.current.value, 8);
    }
}
} // namespace frame_lifecycle::phase
