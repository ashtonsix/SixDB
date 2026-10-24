#include "writers.h"

namespace ikea::seriespack {
template std::expected<void, error> initialize(const dense_output&, std::span<const std::uint32_t>, journal&);
template std::expected<void, error> set(const dense_output&, std::size_t, std::uint32_t, journal&);
} // namespace ikea::seriespack
