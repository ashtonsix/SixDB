#include "probe.h"
#include <bit>

namespace hb {
word raw_roundtrip(reader operation, std::size_t row) {
    return consume_reader(pass_reader(operation), row);
}
std::expected<word, tp::error> checked_roundtrip(const read_plan& plan,
                                               const tp::const_view& source, std::size_t row) {
    auto operation = make_reader(plan, source);
    if (!operation)
        return std::unexpected(operation.error());
    if (row >= operation->size())
        return std::unexpected(tp::error::range);
    return consume_reader(*operation, row);
}
word stored_roundtrip(std::span<reader> slots, std::size_t index, reader operation,
                      std::size_t row) {
    store_reader(slots, index, operation);
    return consume_reader(slots[index], row);
}
std::expected<void, tp::error> write_roundtrip(writer operation, std::size_t row, word input,
                                             ikea::journal& effects) {
    return consume_writer(pass_writer(operation), row, input, effects);
}
std::expected<word, tp::error> checked_filter_command(const filter_plan& plan,
                                                    const tp::const_view& source,
                                                    std::size_t first, word rhs,
                                                    std::uint64_t active) {
    if (!tp::detail::valid_window<16>(source.size(), first, active))
        return std::unexpected(tp::error::range);
    auto operation = make_filter(plan, source);
    if (!operation)
        return std::unexpected(operation.error());
    return consume_filter(*operation, first, rhs, active);
}
// Seven actual independent bindings and six useful running values survive the
// opaque calls. There is no barrier in the value path. These are complete
// read/filter callers, not empty callees used as timing substitutes.
word crowded_readers(const std::array<reader, 7>& operations, std::size_t row,
                     word a, word b, word c, word d, word e, word f) {
    a += consume_reader(operations[0], row);
    b ^= consume_reader(operations[1], row + 1);
    c += consume_reader(operations[2], row + 2);
    d ^= consume_reader(operations[3], row + 3);
    e += consume_reader(operations[4], row + 4);
    f ^= consume_reader(operations[5], row + 5);
    a = std::rotl(a, 7) + consume_reader(operations[6], row + 6);
    return a ^ std::rotl(b, 11) ^ std::rotl(c, 19) ^ std::rotl(d, 23) ^ std::rotl(e, 31) ^ f;
}
word crowded_filters(const std::array<filter, 7>& operations, std::size_t first,
                     word rhs, std::uint64_t active,
                     word a, word b, word c, word d, word e, word f) {
    a += consume_filter(operations[0], first, rhs, active);
    b ^= consume_filter(operations[1], first, rhs, active);
    c += consume_filter(operations[2], first, rhs, active);
    d ^= consume_filter(operations[3], first, rhs, active);
    e += consume_filter(operations[4], first, rhs, active);
    f ^= consume_filter(operations[5], first, rhs, active);
    a = std::rotl(a, 7) + consume_filter(operations[6], first, rhs, active);
    return a ^ std::rotl(b, 11) ^ std::rotl(c, 19) ^ std::rotl(d, 23) ^ std::rotl(e, 31) ^ f;
}
} // namespace hb
