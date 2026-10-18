#pragma once
#include "encoding-profiles.h"
#include "geometry.h"
#include <optional>
#include <stdexcept>
#include <utility>
namespace ikea::hashtable {
struct encoding_choice {
    unsigned width;
    search_kernel kernel;
};
// Construction/rebuild selection only. Unknown surfaces refuse rather than
// inheriting another CPU's measurements. Production profiles are set/map only.
constexpr std::optional<encoding_choice>
measured_encoding(unsigned k, unsigned value_bytes, std::string_view cpu, std::string_view isa) {
    if (k > 64)
        return {};
    if (k <= 9)
        return encoding_choice{k, search_kernel::generic};
    for (const auto& profile : detail::profiles::encoding_profiles)
        if (profile.cpu == cpu && profile.isa == isa && profile.value_bytes == value_bytes)
            return encoding_choice{profile.encoding[k], profile.kernel[k]};
    return {};
}
template <unsigned K, unsigned E = K, class Bind>
decltype(auto) bind_encoding(encoding_choice choice, Bind&& bind) {
    static_assert(K <= 64);
    constexpr auto maximum = geometry::at(64, 64 - K).maximum_encoding;
    if (choice.width == E)
        return bind(std::integral_constant<unsigned, E>{}, choice);
    if constexpr (E < maximum)
        return bind_encoding<K, E + 1>(choice, std::forward<Bind>(bind));
    else
        throw std::invalid_argument("encoding outside fixed bucket geometry");
}
// Bind a complete operation once; native loops do not query profiles.
template <unsigned K, std::size_t I = 0, class Bind>
decltype(auto) bind_measured_encoding(unsigned value_bytes, std::string_view cpu,
                                      std::string_view isa, Bind&& bind) {
    static_assert(K <= 64);
    if constexpr (K <= 9)
        return bind(std::integral_constant<unsigned, K>{},
                    std::integral_constant<search_kernel, search_kernel::generic>{},
                    encoding_choice{K, search_kernel::generic});
    else {
        constexpr auto profile = detail::profiles::encoding_profiles[I];
        constexpr encoding_choice choice{profile.encoding[K], profile.kernel[K]};
        if (profile.cpu == cpu && profile.isa == isa && profile.value_bytes == value_bytes)
            return bind(std::integral_constant<unsigned, choice.width>{},
                        std::integral_constant<search_kernel, choice.kernel>{}, choice);
        if constexpr (I + 1 < detail::profiles::encoding_profiles.size())
            return bind_measured_encoding<K, I + 1>(value_bytes, cpu, isa,
                                                    std::forward<Bind>(bind));
        else
            throw std::invalid_argument("no measured encoding profile for this surface and target");
    }
}
} // namespace ikea::hashtable
