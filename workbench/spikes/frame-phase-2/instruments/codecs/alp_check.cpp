#include "check_common.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <alp.hpp>

template<class T> bool same_bits(T a, T b) {
    using U = typename alp::inner_t<T>::ut;
    return std::bit_cast<U>(a) == std::bit_cast<U>(b);
}

template<class T> unsigned check_type() {
    using U = typename alp::inner_t<T>::ut;
    using I = typename alp::inner_t<T>::st;
    constexpr auto n = alp::config::VECTOR_SIZE;
    std::array<T, n> input{}, sample{}, output{}, exceptions{};
    std::array<I, n> integers{}, packed{}, unpacked{};
    std::array<std::uint16_t, n> positions{}, rd_exceptions{}, left{}, packed_left{}, unpacked_left{};
    std::array<U, n> right{}, packed_right{}, unpacked_right{};
    unsigned cases = 0;
    bool observed_alp = false;
    for (unsigned shape = 0; shape < 4; ++shape) {
        for (std::size_t i = 0; i < n; ++i) {
            if (shape == 0) input[i] = T(int(i % 100) - 50) / T(100);
            else if (shape == 1) input[i] = std::ldexp(T(int(codec_mix(i) % 1000) - 500), -7);
            else input[i] = T(int(i % 200) - 100) / T(10);
        }
        if (shape >= 2) {
            input[0] = -T(0); input[1] = std::numeric_limits<T>::infinity();
            input[2] = -std::numeric_limits<T>::infinity(); input[3] = std::numeric_limits<T>::quiet_NaN();
            input[n - 1] = std::numeric_limits<T>::denorm_min();
        }
        if (shape == 3) for (std::size_t i = 4; i + 1 < n; ++i)
            input[i] = std::sin(T(i)) * T(1e10);

        if (shape < 3) {
            alp::state<T> state;
            alp::encoder<T>::init(input.data(), 0, n, sample.data(), state);
            // Sampling may lawfully choose RD; never force an inapplicable
            // decimal path merely because the test input has a short formula.
            if (state.scheme == alp::Scheme::ALP) {
            observed_alp = true;
            std::uint16_t count{};
            alp::encoder<T>::encode(input.data(), exceptions.data(), positions.data(), &count, integers.data(), state);
            alp::encoder<T>::analyze_ffor(integers.data(), state.bit_width, &state.for_base);
            ffor::ffor(integers.data(), packed.data(), state.bit_width, &state.for_base);
            unffor::unffor(packed.data(), unpacked.data(), state.bit_width, &state.for_base);
            alp::decoder<T>::decode(unpacked.data(), state.fac, state.exp, output.data());
            alp::decoder<T>::patch_exceptions(output.data(), exceptions.data(), positions.data(), &count);
            for (std::size_t i = 0; i < n; ++i) require_codec(same_bits(input[i], output[i]), "ALP unfused bit roundtrip");
            generated::falp::fallback::scalar::falp(packed.data(), output.data(), state.bit_width,
                                                   &state.for_base, state.fac, state.exp);
            alp::decoder<T>::patch_exceptions(output.data(), exceptions.data(), positions.data(), &count);
            for (std::size_t i = 0; i < n; ++i) require_codec(same_bits(input[i], output[i]), "ALP fused bit roundtrip");
            cases += 2;
            }
        }

        alp::state<T> state;
        alp::rd_encoder<T>::init(input.data(), 0, n, sample.data(), state);
        std::uint16_t count{};
        alp::rd_encoder<T>::encode(input.data(), rd_exceptions.data(), positions.data(), &count,
                                   right.data(), left.data(), state);
        ffor::ffor(right.data(), packed_right.data(), state.right_bit_width, &state.right_for_base);
        ffor::ffor(left.data(), packed_left.data(), state.left_bit_width, &state.left_for_base);
        unffor::unffor(packed_right.data(), unpacked_right.data(), state.right_bit_width, &state.right_for_base);
        unffor::unffor(packed_left.data(), unpacked_left.data(), state.left_bit_width, &state.left_for_base);
        alp::rd_encoder<T>::decode(output.data(), unpacked_right.data(), unpacked_left.data(),
                                   rd_exceptions.data(), positions.data(), &count, state);
        for (std::size_t i = 0; i < n; ++i) require_codec(same_bits(input[i], output[i]), "ALP RD bit roundtrip");
        ++cases;
    }
    require_codec(observed_alp, "No ALP decimal path was exercised");
    return cases;
}
int main() try {
    auto cases = check_type<float>() + check_type<double>();
    std::cout << "{\"peer\":\"ALP primitives\",\"revision\":\"" << WORKBENCH_PEER_REVISION
              << "\",\"cases\":" << cases << ",\"bit_exact\":true,\"timing_claim\":false}\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
