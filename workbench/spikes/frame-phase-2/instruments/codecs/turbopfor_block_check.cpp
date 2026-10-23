#include "check_common.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>
#include <ic.h>
#define TURBOPFOR_DAC
#include <vp4.h>

// Separate from the original boot receipt: actual 256-row point images and
// horizontal FOR. Read slack is initialized; the peer library is unmodified.
int main(int argc, char** argv) try {
    const bool aligned_pfor = argc == 2 && std::string_view(argv[1]) == "--aligned-pfor";
    require_codec(argc == 1 || aligned_pfor, "usage: frame_turbopfor_block_check [--aligned-pfor]");
    std::uint64_t for_cases = 0, for_points = 0, pfor_cases = 0, pfor_points = 0;
    std::uint64_t patched_cases = 0, wide_prefix_cases = 0;
    std::mt19937 random(1729);
    for (unsigned bits = 0; bits <= 32; ++bits) {
        for (unsigned n : {1u, 17u, 63u, 64u, 65u, 127u, 128u, 129u, 255u, 256u}) {
            std::vector<std::uint32_t> input(n + 16, 0);
            for (unsigned i = 0; i < n; ++i)
                input[i] = codec_mix(i + 1729) & codec_mask(bits);
            if (n) input[n - 1] = static_cast<std::uint32_t>(codec_mask(bits));
            const auto bytes = (std::size_t(n) * bits + 7) / 8;
            std::vector<unsigned char> packed(bytes + 64 + 16, 0);
            std::fill(packed.end() - 16, packed.end(), 0xcd);
            const auto* end = bitpack32(input.data(), n, packed.data(), bits);
            require_codec(end == packed.data() + bytes, "FOR byte count");
            for (unsigned i = 0; i < n; ++i) {
                require_codec(bitgetx32(packed.data(), i, bits) == input[i], "FOR point");
                ++for_points;
            }
            require_codec(std::all_of(packed.end() - 16, packed.end(),
                                      [](auto x) { return x == 0xcd; }), "FOR slack canary");
            ++for_cases;
        }
        for (unsigned n : {128u, 256u}) {
            for (unsigned shape = 0; shape < 6; ++shape) {
                std::vector<std::uint32_t> input(n + 16, 0);
                for (unsigned i = 0; i < n; ++i) {
                    input[i] = shape == 0 ? 7 :
                        std::uint32_t(codec_mix(i + 1729) & codec_mask(bits));
                    bool exception = false;
                    if (shape == 2)
                        exception = i == 0 || i == 63 || i == 64 || i == 127 ||
                                    i == 128 || i == 191 || i == 192 || i == 255;
                    if (shape == 3) exception = i < 65;
                    if (shape == 4) exception = i < 129;
                    if (shape == 5) exception = i % 2 == 0;
                    if (exception) input[i] = ~std::uint32_t(0);
                }
                const auto capacity = p4nbound32(n);
                std::vector<unsigned char> storage(capacity + 64 + 16 + 7, 0);
                const auto prefix = aligned_pfor ?
                    ((6 - reinterpret_cast<std::uintptr_t>(storage.data())) & 7) : 0;
                auto* packed = storage.data() + prefix;
                auto* canary = packed + capacity + 64;
                std::fill(canary, canary + 16, 0xcd);
                // Patched p4getx32 accesses its image+2 bitmap as u64*. The
                // optional placement aligns that bitmap without a peer patch.
                const auto* end = p4encx32(input.data(), n, packed);
                require_codec(end > packed && end <= packed + capacity,
                              "PFor byte count");
                auto* values = packed;
                struct p4 prepared{};
                unsigned width = 0;
                p4ini(&prepared, &values, n, &width);
                require_codec(values >= packed && values <= end, "PFor prepared pointer");
                // Independently check prefix counts, using memcpy for unaligned
                // bitmap words rather than repeating the point accessor's law.
                if (prepared.isx) {
                    ++patched_cases;
                    unsigned prefix = 0;
                    for (unsigned word = 0; word < n / 64; ++word) {
                        require_codec(prepared.cum[word] == prefix, "PFor exception prefix");
                        std::uint64_t map = 0;
                        std::memcpy(&map, reinterpret_cast<const unsigned char*>(prepared.xmap) + word * 8, 8);
                        for (unsigned bit = 0; bit < 64; ++bit) prefix += (map >> bit) & 1;
                    }
                    if (prefix > 128) ++wide_prefix_cases;
                }
                std::vector<unsigned> order(n);
                std::iota(order.begin(), order.end(), 0);
                for (unsigned i = 0; i < n; ++i) {
                    require_codec(p4getx32(&prepared, values, i, width) == input[i], "PFor ordered point");
                    ++pfor_points;
                }
                std::shuffle(order.begin(), order.end(), random);
                for (unsigned i : order) {
                    require_codec(p4getx32(&prepared, values, i, width) == input[i], "PFor shuffled point");
                    ++pfor_points;
                }
                require_codec(std::all_of(canary, canary + 16,
                                          [](auto x) { return x == 0xcd; }), "PFor slack canary");
                ++pfor_cases;
            }
        }
    }
    require_codec(patched_cases > 0 && wide_prefix_cases > 0, "PFor patch coverage");
    std::cout << "{\"peer\":\"TurboPFor\",\"revision\":\"" << WORKBENCH_PEER_REVISION
              << "\",\"for_cases\":" << for_cases << ",\"for_points\":" << for_points
              << ",\"pfor_cases\":" << pfor_cases << ",\"pfor_points\":" << pfor_points
              << ",\"patched_cases\":" << patched_cases
              << ",\"over128_exception_cases\":" << wide_prefix_cases
              << ",\"aligned_pfor\":" << (aligned_pfor ? "true" : "false")
              << ",\"timing_claim\":false}\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
