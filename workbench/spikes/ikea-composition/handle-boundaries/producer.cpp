#include "probe.h"

namespace hb {
#define HB_PASS(name, type) HB_NOINLINE type pass_##name(type value) { return value; }
HB_TUPLE_RAW_TYPES(HB_PASS)
#undef HB_PASS

std::expected<reader, tp::error> make_reader(const read_plan& plan, const tp::const_view& source) {
    return tp::bind_reader(plan, source);
}
std::expected<writer, tp::error> make_writer(const write_plan& plan, const tp::view& destination) {
    return tp::bind_writer(plan, destination);
}
std::expected<filter, tp::error> make_filter(const filter_plan& plan, const tp::const_view& source) {
    auto result = tp::bind_reader(plan, source);
    if (!result)
        return std::unexpected(result.error());
#if HB_PREPARED_PREDICATE
    return predicate::bind(*result);
#else
    if (plan.ordering().map_size() != 1 || !plan.read_bytes())
        return std::unexpected(tp::error::map);
    return *result;
#endif
}
std::expected<void, tp::error> rebind_reader(reader& destination, const read_plan& plan,
                                           const tp::const_view& source) {
    auto result = tp::bind_reader(plan, source);
    if (!result)
        return std::unexpected(result.error());
    destination = *result;
    return {};
}
word consume_reader(reader operation, std::size_t row) {
    return operation.get_unchecked(row);
}
std::expected<void, tp::error> consume_writer(writer operation, std::size_t row, word input,
                                            ikea::journal& effects) {
    return operation.set(row, input, effects);
}
word consume_filter(filter operation, std::size_t first, word rhs, std::uint64_t active) {
    return operation.template filter_unchecked<ikea::comparison::greater_equal>(first, rhs, active);
}
void store_reader(std::span<reader> slots, std::size_t index, reader value) {
    slots[index] = value;
}

// One six-word record per actual type, in HB_CENSUS_TYPES order.
extern "C" const std::uint64_t hb_facts[] = {
#define HB_FACT(name, ...) sizeof(__VA_ARGS__), alignof(__VA_ARGS__), \
    std::is_trivially_copy_constructible_v<__VA_ARGS__>, \
    std::is_trivially_move_constructible_v<__VA_ARGS__>, \
    std::is_trivially_destructible_v<__VA_ARGS__>, std::is_standard_layout_v<__VA_ARGS__>,
    HB_CENSUS_TYPES(HB_FACT)
#undef HB_FACT
};
} // namespace hb
