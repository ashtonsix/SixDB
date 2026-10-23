#include "check_common.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <fls/ffor.hpp>
#include <fls/unffor.hpp>

template<class T> std::uint64_t check_type() {
    constexpr std::size_t n = 1024;
    std::array<T, n> input{};
    std::array<T, n + 16> packed{}, output{};
    std::uint64_t cases = 0;
    for (unsigned bits = 0; bits <= sizeof(T) * 8; ++bits) {
        const T mask = T(codec_mask(bits));
        for (unsigned nonzero_base = 0; nonzero_base < 2; ++nonzero_base) {
            const T base = nonzero_base && bits < sizeof(T) * 8 ? T(17) : T(0);
            for (std::size_t i = 0; i < n; ++i)
                input[i] = base + T(codec_mix(i + 1729) & (mask >> (nonzero_base ? 1 : 0)));
            packed.fill(T(0x5a)); output.fill(T(0xa5));
            ffor::ffor(input.data(), packed.data(), std::uint8_t(bits), &base);
            const auto words = (bits * n) / (sizeof(T) * 8);
            require_codec(std::all_of(packed.begin() + words, packed.end(), [](T x) { return x == T(0x5a); }),
                          "FastLanes packed extent");
            unffor::unffor(packed.data(), output.data(), std::uint8_t(bits), &base);
            require_codec(std::equal(input.begin(), input.end(), output.begin()), "FastLanes values");
            require_codec(std::all_of(output.begin() + n, output.end(), [](T x) { return x == T(0xa5); }),
                          "FastLanes output extent");
            ++cases;
        }
    }
    return cases;
}
int main() try {
    auto cases = check_type<std::uint8_t>() + check_type<std::uint16_t>() +
                 check_type<std::uint32_t>() + check_type<std::uint64_t>();
    std::cout << "{\"peer\":\"FastLanes FFOR\",\"revision\":\"" << WORKBENCH_PEER_REVISION
              << "\",\"cases\":" << cases << ",\"vector_rows\":1024,\"timing_claim\":false}\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
