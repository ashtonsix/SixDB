#include <ikea/seriespack/detail/dense_read.h>

namespace ikea::seriespack {
namespace {
template <unsigned K, class U> std::expected<decoder<U>, error> select(const dense_input& source) {
    if (source.storage == geometry::local)
        return bind_decoder<U>(dense_source<format<K>>{source.bytes.data(), source.count});
    if constexpr (striped_width(K))
        return bind_decoder<U>(
            dense_source<format<K, geometry::striped>>{source.bytes.data(), source.count});
    return std::unexpected(error::description);
}
} // namespace
template <class U> std::expected<decoder<U>, error> bind_decoder(const dense_input& source) {
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
    if (source.count && reinterpret_cast<std::uintptr_t>(source.bytes.data()) % 64)
        return std::unexpected(error::alignment);
    std::expected<decoder<U>, error> result = std::unexpected(error::description);
    detail::each<sizeof(U) * 8>([&](auto k) {
        if (source.width == k + 1)
            result = select<k + 1, U>(source);
    });
    return result;
}
template std::expected<decoder<std::uint8_t>, error> bind_decoder(const dense_input&);
template std::expected<decoder<std::uint16_t>, error> bind_decoder(const dense_input&);
template std::expected<decoder<std::uint32_t>, error> bind_decoder(const dense_input&);
template std::expected<decoder<std::uint64_t>, error> bind_decoder(const dense_input&);
} // namespace ikea::seriespack
