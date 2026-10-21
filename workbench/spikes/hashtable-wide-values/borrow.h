#pragma once
#include <ikea/hashtable/author/table.h>
#include <bit>
#include <cstdint>

namespace wide {
namespace ht = ikea::hashtable;
using ht::byte;
using ht::word;
using Access = ht::author::table_access;

// Research read adapter over an exclusively built, immutable-during-read owner.
// Returned bytes belong to that owner, never a materialized callback temporary.
// The maintained group-comparison helpers preserve first-empty logical groups.
template <class Table>
[[gnu::noinline, gnu::pure]] const byte* overflow_pointer(const Table& table, word key,
                                                        word swiss) {
    const auto s = Access::get(table);
    const typename Table::mix_type::result h{0, 0, swiss};
    if (!s.groups)
        return nullptr;
    constexpr unsigned width = ht::detail::swiss_read_groups;
    const auto mask = s.groups - 1;
    std::size_t group = swiss & mask, step = 0;
    for (;;) {
        ht::detail::swiss_read_masks packet{};
        if constexpr (width > 1) {
            if (s.groups >= width)
                packet = ht::detail::compare_swiss_read<width>(
                    s.controls.data, group, step, mask, h.fingerprint());
            else {
                packet.candidates = ht::equal_controls(s.controls.data + group * 16,
                                                               h.fingerprint());
                packet.empties = ht::equal_controls(s.controls.data + group * 16, 0);
            }
        } else {
            packet.candidates = ht::equal_controls(s.controls.data + group * 16,
                                                           h.fingerprint());
            packet.empties = ht::equal_controls(s.controls.data + group * 16, 0);
        }
        auto candidates = packet.candidates;
        while (candidates) {
            const auto lane = unsigned(std::countr_zero(candidates));
            candidates &= candidates - 1;
            auto slot = group * 16 + lane;
            if constexpr (width > 1)
                if (s.groups >= width)
                    slot = ht::detail::swiss_read_slot<width>(group, step, lane, mask);
            const auto* tuple = s.slots.data + slot * Table::tuple_bytes;
            if (ht::load<Table::key_bytes>(tuple) == key)
                return tuple + Table::key_bytes;
        }
        if (packet.empties)
            return nullptr;
        if constexpr (width > 1) {
            if (s.groups >= width) {
                group = ht::detail::swiss_read_group(group, step, width, mask);
                step += width;
            } else
                group = (group + ++step) & mask;
        } else
            group = (group + ++step) & mask;
    }
}

template <unsigned V> struct borrowed_value {
    const byte* address;
    bool found;
};
template <unsigned V> alignas(64) inline constexpr ht::bytes<V> zero{};

template <ht::lookup_hint Hint, class Table>
[[gnu::always_inline]] auto borrow(const Table& table, word key) {
    static_assert(sizeof(typename Table::value_type) <= 8 || sizeof(typename Table::value_type) % 8 == 0);
    constexpr auto V = sizeof(typename Table::value_type);
    const auto p = table.prepare(key);
    if constexpr (Table::bitmap) {
        const unsigned row = Access::primary_find<Table>(p.primary, p.hash.quotient);
        return borrowed_value<V>{row != Table::N ? p.primary + Table::value_offset + row * V
                                                : zero<V>.data(), row != Table::N};
    } else {
        if constexpr (Hint == ht::lookup_hint::unpredictable) {
            unsigned overflow = p.primary[Table::overflow_offset];
            asm("" : "+r"(overflow));
            if (!overflow) {
                const auto matches = Access::primary_matches<Table>(p.primary, p.hash.quotient);
                const auto row = unsigned(std::countr_zero(matches));
                // Integer address arithmetic permits a non-dereferenced absent
                // candidate. Select real readable zero storage before consumption.
                std::uintptr_t address = std::uintptr_t(p.primary) + Table::value_offset + row * V;
#if defined(__x86_64__)
                asm("testq %1, %1; cmovzq %2, %0" : "+r"(address)
                    : "r"(matches), "r"(zero<V>.data()) : "cc");
#else
                address = matches ? address : std::uintptr_t(zero<V>.data());
#endif
                return borrowed_value<V>{reinterpret_cast<const byte*>(address), matches != 0};
            }
        }
        const auto row = Access::primary_find<Table>(p.primary, p.hash.quotient);
        if (row != Table::N)
            return borrowed_value<V>{p.primary + Table::value_offset + row * V, true};
        if (!p.primary[Table::overflow_offset])
            return borrowed_value<V>{zero<V>.data(), false};
        const auto* value = overflow_pointer(table, key, p.hash.swiss);
        return borrowed_value<V>{value ? value : zero<V>.data(), value != nullptr};
    }
}
} // namespace wide
