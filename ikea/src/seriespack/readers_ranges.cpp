// The runtime binding with compiled range operations, in its own unit: only
// programs that call bind_range_reader link the operation tables.
#include <ikea/seriespack/detail/dense_operations.h>

namespace ikea::seriespack {
template <class U> std::expected<erased_reader<U>, error> bind_range_reader(const dense_input& source) {
    auto result = bind_reader<U>(source);
    if (result)
        result = result->with_operations(detail::compiled_operations(source.width, source.storage));
    return result;
}
template std::expected<erased_reader<std::uint8_t>, error> bind_range_reader(const dense_input&);
template std::expected<erased_reader<std::uint16_t>, error> bind_range_reader(const dense_input&);
template std::expected<erased_reader<std::uint32_t>, error> bind_range_reader(const dense_input&);
template std::expected<erased_reader<std::uint64_t>, error> bind_range_reader(const dense_input&);
} // namespace ikea::seriespack
