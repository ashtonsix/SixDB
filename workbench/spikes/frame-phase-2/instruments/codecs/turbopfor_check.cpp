#include "check_common.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <vector>
#include <ic.h>
// The amalgamated ic.h repeats DAC inline definitions through bitpack.h.
// Use upstream's original vp4.h for DAC, as its own implementation does.
#define TURBOPFOR_DAC
#include <vp4.h>

int main() try {
    std::uint64_t cases = 0, points = 0;
    for (unsigned bits = 0; bits <= 32; ++bits) {
        for (unsigned n : {1u, 17u, 128u, 256u, 1024u}) {
            std::vector<std::uint32_t> input(n), output(n + 16, 0xdeadbeef);
            for (unsigned i = 0; i < n; ++i) input[i] = codec_mix(i + 1729) & codec_mask(bits);
            auto capacity = p4nbound32(n);
            std::vector<unsigned char> packed(capacity + 64, 0);
            auto bytes = p4nenc32(input.data(), n, packed.data());
            require_codec(bytes <= capacity, "TurboPFor bound32");
            auto read = p4ndec32(packed.data(), n, output.data());
            require_codec(read == bytes, "TurboPFor consumed32");
            require_codec(std::equal(input.begin(), input.end(), output.begin()), "TurboPFor u32 values");
            require_codec(std::all_of(output.begin() + n, output.end(), [](auto x) { return x == 0xdeadbeef; }),
                          "TurboPFor u32 output overrun");
            ++cases;
        }
        // The actual direct-access image has its own exception-map preparation.
        // Check every row in mixed low-bit and sparse full-width cases, including
        // the final slot. Declared allocation includes the upstream DAC read slack.
        for (unsigned shape = 0; shape < 3; ++shape) {
            constexpr unsigned n = 128;
            std::array<std::uint32_t, n> input{};
            for (unsigned i = 0; i < n; ++i)
                input[i] = shape == 0 ? 7 : std::uint32_t(codec_mix(i + 1729) & codec_mask(bits));
            if (shape == 2) for (unsigned i : {0u, 63u, 64u, 127u}) input[i] = ~std::uint32_t(0);
            std::vector<unsigned char> packed(p4nbound32(n) + 64, 0);
            auto* end = p4encx32(input.data(), n, packed.data());
            require_codec(end >= packed.data() && end < packed.data() + packed.size(), "TurboPFor DAC size");
            auto* values = packed.data();
            struct p4 prepared{};
            unsigned width{};
            p4ini(&prepared, &values, n, &width);
            for (unsigned i = 0; i < n; ++i) {
                require_codec(p4getx32(&prepared, values, i, width) == input[i], "TurboPFor DAC row");
                ++points;
            }
            ++cases;
        }
    }
    for (unsigned bits = 0; bits <= 64; ++bits) {
        constexpr unsigned n = 1024;
        std::vector<std::uint64_t> input(n), output(n + 16, 0xdeadbeefdeadbeefULL);
        for (unsigned i = 0; i < n; ++i) input[i] = codec_mix(i + 1729) & codec_mask(bits);
        auto capacity = p4nbound64(n);
        std::vector<unsigned char> packed(capacity + 64, 0);
        auto bytes = p4nenc64(input.data(), n, packed.data());
        require_codec(bytes <= capacity, "TurboPFor bound64");
        auto read = p4ndec64(packed.data(), n, output.data());
        require_codec(read == bytes, "TurboPFor consumed64");
        require_codec(std::equal(input.begin(), input.end(), output.begin()), "TurboPFor u64 values");
        require_codec(std::all_of(output.begin() + n, output.end(), [](auto x) { return x == 0xdeadbeefdeadbeefULL; }),
                      "TurboPFor u64 output overrun");
        ++cases;
    }
    std::cout << "{\"peer\":\"TurboPFor\",\"revision\":\"" << WORKBENCH_PEER_REVISION
              << "\",\"cases\":" << cases << ",\"direct_points\":" << points
              << ",\"timing_claim\":false}\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
