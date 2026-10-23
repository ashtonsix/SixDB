#include "writers.h"

namespace ikea::seriespack {
template std::expected<void, error> initialize(const dense_output&, std::span<const std::uint8_t>, journal&);
} // namespace ikea::seriespack
