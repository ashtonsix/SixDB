#pragma once
#include "../table.h"

namespace ikea::hashtable::author {
// Internal authoring seam for private, exclusive mutation kernels. These
// references do not admit a public mutation, acquire a lock, publish damage or
// retain storage. The kernel must preserve the table's representation and
// counters, and must not let references/probes escape an invalidating operation.
// No reader or owner member layout changes when an author uses this seam.
struct table_access {
    // For a complete, exclusive fresh-image builder only. It must initialize
    // every assigned byte before exposing a reader or invoking public methods.
    // Partially initialized images may only be destroyed if construction fails.
    template <class Table>
    static Table uninitialized_image(word seed,
                                     typename Table::storage_policy::context* memory = nullptr) {
        return Table(typename Table::uninitialized_image_tag{}, seed, memory);
    }
    template <class Table> struct state {
        decltype((std::declval<Table&>().primary_)) primary;
        decltype((std::declval<Table&>().control_)) controls;
        decltype((std::declval<Table&>().slots_)) slots;
        decltype((std::declval<Table&>().mix_)) mix;
        decltype((std::declval<Table&>().groups_)) groups;
        decltype((std::declval<Table&>().spill_size_)) spill;
        decltype((std::declval<Table&>().deleted_)) deleted;
        decltype((std::declval<Table&>().size_)) size;
    };
    template <class Table> [[gnu::always_inline]] static state<Table> get(Table& table) {
        return {table.primary_, table.control_,    table.slots_,   table.mix_,
                table.groups_,  table.spill_size_, table.deleted_, table.size_};
    }
    template <class Table>
    static auto prepare_write(Table& table, word key, typename Table::value_type& previous) {
        return table.prepare_write(key, previous);
    }
    template <class Table, class Position>
    static bool insert_prepared(Table& table, word key, const Position& position,
                                const typename Table::value_type& value) {
        return table.insert_prepared_unchecked(key, position, value);
    }
    template <class Table>
    [[gnu::always_inline]] static unsigned primary_find(const std::uint8_t* p, word q) {
        return Table::primary_find(p, q);
    }
    template <class Table>
    [[gnu::always_inline]] static word primary_matches(const std::uint8_t* p, word q) {
        return Table::primary_matches(p, q);
    }
    template <class Table>
    static typename Table::value_type primary_value(const std::uint8_t* p, unsigned row) {
        typename Table::value_type value{};
        Table::consume_primary(p, row, [&](const auto& v) { value = v; });
        return value;
    }
    template <class Table>
    static void write_value(std::uint8_t* p, unsigned row, const typename Table::value_type& value) {
        Table::write_value(p, row, value);
    }
};
} // namespace ikea::hashtable::author
