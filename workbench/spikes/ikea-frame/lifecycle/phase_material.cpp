#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>
#include "phase_material.h"
#include <cassert>
#include <cstring>
#include <limits>

namespace frame_lifecycle::phase {
namespace {
template<class T> T load(const std::byte* p) { T x; std::memcpy(&x, p, sizeof(x)); return x; }
unsigned state(Bytes bytes, unsigned row) {
    const auto present = load<u64>(bytes.data + (row / 64) * 8);
    const auto valid = load<u64>(bytes.data + flag_bytes + (row / 64) * 8);
    return unsigned((present >> (row % 64)) & 1) | unsigned((valid >> (row % 64)) & 1) * 2;
}
std::array<u64, 4> active(Bytes bytes, unsigned b) {
    std::array<u64, 4> out;
    for (unsigned w = 0; w < 4; ++w) out[w] = load<u64>(
        bytes.data + flag_bytes + (b * 4 + w) * 8);
    return out;
}
u64 native_value(const GroupHeader& h, Bytes bytes, unsigned row) {
    const auto* p = bytes.data + row * h.native_bytes;
    switch (h.native_bytes) {
    case 1: return h.base + load<std::uint8_t>(p);
    case 2: return h.base + load<std::uint16_t>(p);
    case 4: return h.base + load<std::uint32_t>(p);
    case 8: return h.base + load<u64>(p);
    }
    std::abort();
}
template<class T>
QueryResult native_range(const GroupHeader& h, Bytes bytes, unsigned first,
                         unsigned count, u64 threshold, const std::array<u64, 4>* valid) {
    // Split the exact unsigned sum into two u64 reductions. A u128 add per row
    // would cripple the plain control despite its native representation.
    u64 low = 0, high = 0, total = 0, matched = 0;
    const bool all_hit = threshold <= h.base;
    const bool all_miss = !all_hit && threshold - h.base > std::numeric_limits<T>::max();
    const T cutoff = T(threshold - h.base);
    for (unsigned i = 0; i < count; ++i) {
        const bool selected = !valid || ((*valid)[i / 64] >> (i % 64) & 1);
        const auto residual = load<T>(bytes.data + (first + i) * sizeof(T));
        low += selected ? std::uint32_t(residual) : 0;
        if constexpr (sizeof(T) == 8) high += selected ? u64(residual) >> 32 : 0;
        total += selected;
        matched += selected && (all_hit || (!all_miss && residual >= cutoff));
    }
    return {u128(h.base) * total + (u128(high) << 32) + low, matched, total};
}
QueryResult native_dispatch(const GroupHeader& h, Bytes bytes, unsigned first,
                            unsigned count, u64 threshold, const std::array<u64, 4>* valid) {
    switch (h.native_bytes) {
    case 1: return native_range<std::uint8_t>(h, bytes, first, count, threshold, valid);
    case 2: return native_range<std::uint16_t>(h, bytes, first, count, threshold, valid);
    case 4: return native_range<std::uint32_t>(h, bytes, first, count, threshold, valid);
    case 8: return native_range<u64>(h, bytes, first, count, threshold, valid);
    }
    std::abort();
}
void add(QueryResult& a, QueryResult b) { a.sum += b.sum; a.count += b.count; a.present += b.present; }
}

bool GroupHeader::valid() const noexcept {
    if (!identity) return false;
    if (native_bytes != 0 && native_bytes != 1 && native_bytes != 2 && native_bytes != 4 && native_bytes != 8)
        return false;
    if (!bytes || (native_bytes && bytes != rows_per_group * native_bytes)) return false;
    for (unsigned b = 0; b < blocks_per_group; ++b)
        if (present_count[b] > 256 || valid_count[b] > present_count[b] ||
            (valid_count[b] && bounds[b].minimum > bounds[b].maximum)) return false;
    return true;
}
bool EncodedBase::valid() const noexcept {
    if (!header.valid()) return false;
    if (header.native_bytes) return true; // A thawed group uses the same ownership family.
    if (unsigned(checkpoints) > unsigned(fe::checkpoints::none)) return false;
    const auto& d = dictionary;
    if (d.count && (!d.edition.scope || !d.width || d.width > 64 ||
        d.offset > header.bytes || d.bytes() > header.bytes - d.offset ||
        d.grant_bytes < d.bytes() || d.grant_bytes > header.bytes - d.offset ||
        (d.order != fe::dictionary_order::sorted && d.order != fe::dictionary_order::frequency)))
        return false;
    if (coded && !d.count) return false;
    for (unsigned i = 0; i < blocks_per_group; ++i) {
        const auto& b = blocks[i];
        if (unsigned(b.type) > unsigned(fe::kind::delta_patched) || b.offset > header.bytes ||
            grants[i] > header.bytes - b.offset)
            return false;
    }
    return true;
}
fe::borrowed_block borrow(const EncodedBase& b, Bytes bytes, unsigned block) {
    assert(block < blocks_per_group && !b.header.native_bytes && bytes.size == b.header.bytes);
    fe::borrowed_block out{};
    out.d = b.blocks[block];
    out.body = reinterpret_cast<const std::uint8_t*>(bytes.data) + out.d.offset;
    out.grant = b.grants[block];
    out.delta_checkpoints = b.checkpoints;
    out.coded = (b.coded >> block) & 1;
    if (out.coded) {
        const auto& d = b.dictionary;
        out.dictionary = {d.min, d.count, d.width, d.order,
            reinterpret_cast<const std::uint8_t*>(bytes.data) + d.offset, d.grant_bytes, d.edition};
    }
    return out;
}
LogicalRow point(const EncodedBase& b, Bytes bytes, Bytes validity, unsigned row) {
    assert(row < rows_per_group && bytes.size == b.header.bytes);
    const auto flags = state(validity, row);
    if (flags != 3) return {0, bool(flags & 1), bool(flags & 2)};
    return {b.header.native_bytes ? native_value(b.header, bytes, row)
            : fe::value_at(borrow(b, bytes, row / 256), row % 256), true, true};
}
LogicalRow point(const NativeBase& b, Bytes bytes, Bytes validity, unsigned row) {
    assert(row < rows_per_group && bytes.size == b.header.bytes);
    const auto flags = state(validity, row);
    return {flags == 3 ? native_value(b.header, bytes, row) : 0,
            bool(flags & 1), bool(flags & 2)};
}

namespace {
QueryResult native_query(const GroupHeader& h, Bytes bytes, Bytes validity, std::uint16_t selected,
                         u64 threshold) {
    QueryResult result;
    if (!selected) return result; // Before even reading interpretation.
    bool all_valid = selected == UINT16_MAX;
    for (unsigned b = 0; b < blocks_per_group && all_valid; ++b) all_valid &= h.valid_count[b] == 256;
    if (all_valid) return native_dispatch(h, bytes, 0, rows_per_group, threshold, nullptr);
    while (selected) {
        const unsigned b = std::countr_zero(selected); selected &= selected - 1;
        result.present += h.present_count[b];
        if (!h.valid_count[b]) continue;
        std::array<u64, 4> flags;
        const std::array<u64, 4>* mask = nullptr;
        if (h.valid_count[b] != 256) { flags = active(validity, b); mask = &flags; }
        auto part = native_dispatch(h, bytes, b * 256, 256, threshold, mask);
        part.present = 0; add(result, part);
    }
    return result;
}
}
QueryResult query(const NativeBase& base, Bytes bytes, Bytes validity, std::uint16_t selected,
                  u64 threshold, QueryCache&) {
    return native_query(base.header, bytes, validity, selected, threshold);
}
QueryResult query(const EncodedBase& base, Bytes bytes, Bytes validity, std::uint16_t selected,
                  u64 threshold, QueryCache& cache) {
    QueryResult result;
    if (!selected) return result;
    const auto& h = base.header;
    if (h.native_bytes) return native_query(h, bytes, validity, selected, threshold);
    std::array<u64, 256> decoded;
    while (selected) {
        const unsigned b = std::countr_zero(selected); selected &= selected - 1;
        result.present += h.present_count[b];
        if (!h.valid_count[b]) continue;
        const auto k = borrow(base, bytes, b);
        const bool all_hit = threshold <= h.bounds[b].minimum;
        const bool all_miss = threshold > h.bounds[b].maximum;
        if (h.valid_count[b] == 256) {
            result.sum += k.coded ? cache.sum(k) : fe::stored_sum(k);
            if (k.coded && !all_hit && !all_miss && cache.count.threshold != threshold) {
                cache.count.threshold = threshold;
                cache.count.ready = false;
            }
            result.count += all_hit ? 256 : all_miss ? 0 :
                k.coded ? cache.count.count(k) : fe::stored_count_ge(k, threshold);
            continue;
        }
        const auto flags = active(validity, b);
        if (!cache.reference_selection) {
            result.sum += k.coded ? cache.sum.masked(k,flags) : fe::masked_stored_sum(k,flags);
            if (k.coded && !all_hit && !all_miss && cache.count.threshold != threshold) {
                cache.count.threshold=threshold;cache.count.ready=false;
            }
            result.count += all_hit ? h.valid_count[b] : all_miss ? 0 :
                k.coded ? cache.count.masked(k,flags) : fe::masked_stored_count_ge(k,flags,threshold);
            continue;
        }
        fe::decode_block(k, decoded.data());
        for (unsigned i = 0; i < 256; ++i) if (flags[i / 64] >> (i % 64) & 1) {
            result.sum += decoded[i];
            result.count += all_hit || (!all_miss && decoded[i] >= threshold);
        }
    }
    return result;
}
QueryResult query_overlay(const EncodedBase& frozen, Bytes body, Bytes validity, const Overlay& changes,
                          Bytes values, std::uint16_t selected, u64 threshold, QueryCache& cache) {
    QueryResult result;
    if (!selected) return result;
    assert(changes.basis == frozen.header.identity);
    result = query(frozen, body, validity, selected, threshold, cache);
    const auto n = changes.count;
    assert(values.size == Overlay::bytes(n));
    u128 removed = 0, added = 0;
    u64 old_count = 0, new_count = 0, old_present = 0, new_present = 0;
    for (unsigned i = 0; i < n; ++i) {
        const auto row = load<std::uint16_t>(values.data + 8 + i * 2);
        if (!(selected >> (row / 256) & 1)) continue; // Do not load excluded values/flags.
        const auto old = load<std::uint8_t>(values.data + 8 + n * 2 + i);
        const auto next = load<std::uint8_t>(values.data + 8 + n * 3 + i);
        old_present += old & 1; new_present += next & 1;
        if (old == 3) {
            const auto value = load<u64>(values.data + 8 + n * 4 + i * 8);
            removed += value; old_count += value >= threshold;
        }
        if (next == 3) {
            const auto value = load<u64>(values.data + 8 + n * 12 + i * 8);
            added += value; new_count += value >= threshold;
        }
    }
    assert(result.sum >= removed && result.count >= old_count && result.present >= old_present);
    result.sum = result.sum - removed + added;
    result.count = result.count - old_count + new_count;
    result.present = result.present - old_present + new_present;
    return result;
}
namespace {
bool admit_flags(const GroupHeader& h, Bytes bytes) {
    if (!h.valid() || bytes.size != flag_bytes * 2 || !bytes.data) return false;
    for (unsigned b = 0; b < blocks_per_group; ++b) {
        unsigned present = 0, valid = 0;
        for (unsigned w = 0; w < 4; ++w) {
            const auto p = load<u64>(bytes.data + (b * 4 + w) * 8);
            const auto v = load<u64>(bytes.data + flag_bytes + (b * 4 + w) * 8);
            if (v & ~p) return false;
            present += std::popcount(p); valid += std::popcount(v);
        }
        if (present != h.present_count[b] || valid != h.valid_count[b]) return false;
    }
    return true;
}
}
bool admit(const EncodedBase& base, Bytes bytes, Bytes flags) {
    if (!base.valid() || bytes.size != base.header.bytes || !admit_flags(base.header, flags)) return false;
    if (base.header.native_bytes) return true;
    auto end = base.dictionary.count ? base.dictionary.offset + base.dictionary.bytes() : 0;
    for (unsigned b = 0; b < blocks_per_group; ++b) {
        const auto& d = base.blocks[b];
        if (d.offset < end) return false;
        const auto k = borrow(base, bytes, b);
        const auto extent = fe::extent_of(d, base.checkpoints, k.body, k.grant);
        if (!extent || !fe::admits(k)) return false;
        end = d.offset + k.grant;
    }
    return end <= base.header.bytes;
}
bool admit(const NativeBase& base, Bytes bytes, Bytes flags) {
    return base.valid() && bytes.size == base.header.bytes && admit_flags(base.header, flags);
}
bool admit(const Overlay& changes, Bytes bytes) {
    const auto n = changes.count;
    if (n > rows_per_group || bytes.size != Overlay::bytes(n) || !bytes.data || !changes.basis ||
        load<u64>(bytes.data) != changes.basis) return false;
    unsigned previous = 0;
    for (unsigned i = 0; i < n; ++i) {
        const auto row = load<std::uint16_t>(bytes.data + 8 + i * 2);
        const auto old = load<std::uint8_t>(bytes.data + 8 + n * 2 + i);
        const auto next = load<std::uint8_t>(bytes.data + 8 + n * 3 + i);
        if (row >= rows_per_group || (i && row <= previous) || old > 3 || next > 3 || old == 2 || next == 2)
            return false;
        previous = row;
    }
    return true;
}
} // namespace frame_lifecycle::phase

#define FRAME_PHASE_INSTANTIATE(LayoutType) \
    template struct orbital::object_detail::History<frame_lifecycle::phase::Physical<LayoutType>>; \
    template class orbital::ObjectStore<frame_lifecycle::phase::Physical<LayoutType>>; \
    template struct orbital::RepresentationSelection<LayoutType>; \
    template void orbital::release(orbital::Part<LayoutType>*) noexcept; \
    template orbital::View orbital::view(const orbital::Part<LayoutType>*) noexcept; \
    template orbital::ObjectBinding orbital::binding(const orbital::Part<LayoutType>*) noexcept; \
    template orbital::Result<orbital::PartSource> orbital::source(orbital::RepresentationSelection<LayoutType>, std::size_t) noexcept;
FRAME_PHASE_INSTANTIATE(frame_lifecycle::phase::EncodedLayout)
FRAME_PHASE_INSTANTIATE(frame_lifecycle::phase::OverlayLayout)
FRAME_PHASE_INSTANTIATE(frame_lifecycle::phase::NativeLayout)
#undef FRAME_PHASE_INSTANTIATE
