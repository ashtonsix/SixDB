#pragma once
#include <ikea/hashtable/author/table.h>

namespace wide {
namespace ht = ikea::hashtable;
using byte = std::byte;
using ht::word;
using Access = ht::author::table_access;

template <unsigned V> struct borrowed_value {
    const byte* address;
    bool found;
};
template <unsigned V> alignas(64) inline constexpr ht::bytes<V> zero{};

// Research slot stores and the comparison driver use the maintained read API.
// The original adapter remains recoverable in the captured spike sources.
template <ht::lookup_hint Hint, class Table>
[[gnu::always_inline]] auto borrow(const Table& table, word key) {
    return table.template get_borrowed<Hint>(key);
}
} // namespace wide
