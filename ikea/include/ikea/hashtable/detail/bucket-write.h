#pragma once
#include "../table.h"
namespace ikea::hashtable::detail {
template <class Table> bool live_row(const std::uint8_t* p, unsigned row) {
    if constexpr (Table::bitmap)
        return p[row / 8] & (1u << (row % 8));
    else
        return !(p[Table::flags_offset + row / 8] & (1u << (row % 8)));
}
template <class Table> void occupy(std::uint8_t* p, unsigned row, word q) {
    if constexpr (Table::bitmap)
        p[row / 8] |= std::uint8_t(1u << (row % 8));
    else {
        ikea::hashtable::set_point<Table::encoding>(p, row, Table::N, q);
        p[Table::flags_offset + row / 8] &= std::uint8_t(~(1u << (row % 8)));
    }
}

} // namespace ikea::hashtable::detail
