#include <ikea/seriespack/reader.h>

namespace ikea::seriespack {
namespace {
template <unsigned K, class U> std::expected<erased_reader<U>, error> select(const dense_input& source) {
    if (source.storage == geometry::local)
        return detail::erase_dense_reader<U>(dense_source<layout<K>>{source.bytes.data(), source.count});
    if constexpr (striped_width(K))
        return detail::erase_dense_reader<U>(
            dense_source<layout<K, geometry::striped>>{source.bytes.data(), source.count});
    return std::unexpected(error::description);
}
} // namespace
template <class U> std::expected<erased_reader<U>, error> bind_reader(const dense_input& source) {
    if (source.width == 0 || source.width > sizeof(U) * 8 ||
        (source.storage != geometry::local && source.storage != geometry::striped))
        return std::unexpected(error::description);
    if (source.storage == geometry::striped && !striped_width(source.width))
        return std::unexpected(error::description);
    const auto rows =
        source.storage == geometry::local ? 8 : 32 * (8 / std::gcd(source.width % 8, 8u));
    const auto bytes = rows * source.width / 8;
    const auto tiles = source.count / rows + (source.count % rows != 0);
    if (tiles > std::numeric_limits<std::size_t>::max() / bytes)
        return std::unexpected(error::overflow);
    if (source.bytes.size() < tiles * bytes)
        return std::unexpected(error::capacity);
    std::expected<erased_reader<U>, error> result = std::unexpected(error::description);
    detail::each<sizeof(U) * 8>([&](auto k) {
        if (source.width == k + 1)
            result = select<k + 1, U>(source);
    });
    return result;
}
template std::expected<erased_reader<std::uint8_t>, error> bind_reader(const dense_input&);
template std::expected<erased_reader<std::uint16_t>, error> bind_reader(const dense_input&);
template std::expected<erased_reader<std::uint32_t>, error> bind_reader(const dense_input&);
template std::expected<erased_reader<std::uint64_t>, error> bind_reader(const dense_input&);
} // namespace ikea::seriespack
