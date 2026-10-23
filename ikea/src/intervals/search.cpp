#include <ikea/intervals/detail/prediction.h>
#include <ikea/intervals/types.h>
#include <bit>

namespace ikea::intervals {
std::string_view describe(error reason) noexcept {
    switch (reason) {
    case error::description:
        return "unknown or inconsistent source description";
    case error::capacity:
        return "borrowed capacity or extent bounds are insufficient";
    case error::alignment:
        return "storage or operation coordinates violate alignment requirements";
    case error::overlap:
        return "borrowed storage overlaps command or effect storage";
    case error::contents:
        return "interval fields, checkpoints or readable tail are inconsistent";
    case error::value:
        return "interval size exceeds its field or total extent limit";
    case error::effects:
        return "effect journal has insufficient available entries";
    case error::range:
        return "operation range or selection coverage is invalid";
    }
    return "unknown Intervals error";
}
} // namespace ikea::intervals

namespace ikea::intervals::detail {
numeric_preparation prepare_numeric(const void* address, unsigned count, unsigned bytes) {
    numeric_preparation p;
    if (!count)
        return p;
    auto value = [&](unsigned rank) -> std::uint64_t {
        const auto* data = static_cast<const std::byte*>(address) + std::size_t(rank) * bytes;
        switch (bytes) {
        case 2:
            return load<std::uint16_t>(data);
        case 4:
            return load<std::uint32_t>(data);
        default:
            return load<std::uint64_t>(data);
        }
    };
    p.first = value(0);
    p.last = value(count - 1);
    if (p.first == p.last) {
        p.kind = prediction_kind::constant;
        return p;
    }
    p.scale = double(count - 1) / double(p.last - p.first);
    unsigned maximum_error = 0;
    for (unsigned part = 1; part < 8; ++part) {
        const auto rank = unsigned(std::uint64_t(count - 1) * part / 8);
        const auto estimate =
            unsigned(std::min(double(count - 1), double(value(rank) - p.first) * p.scale));
        maximum_error =
            std::max(maximum_error, estimate > rank ? estimate - rank : rank - estimate);
    }
    // Sparse quantiles can alias equal runs. Zero sampled residual does not
    // admit a tiny correction region. Widen conservatively and always check it.
    const auto window = std::max<std::uint64_t>(32, std::bit_ceil(2ull * maximum_error + 4));
    // These are profitability controls, not ordering/domain constraints. Larger
    // or irregular sources retain the ordinary native comparison kernel.
    if (window <= 128 && window * 32 <= count) {
        p.window_log = std::countr_zero(window);
        p.kind = prediction_kind::interpolation;
    }
    return p;
}
} // namespace ikea::intervals::detail
