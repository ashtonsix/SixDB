#include "grain.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>

namespace {
struct page {
    std::size_t size = std::size_t(sysconf(_SC_PAGESIZE));
    std::uint8_t* data = static_cast<std::uint8_t*>(mmap(nullptr, 3 * size, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    page() { if (data == MAP_FAILED || mprotect(data + size, size, PROT_READ | PROT_WRITE)) std::abort(); }
    ~page() { munmap(data, 3 * size); }
};
std::size_t checks = 0;
template<unsigned Values, bool Join>
void verify() {
    std::array<std::uint64_t, Values> input{};
    std::array<std::uint8_t, Values> encoded{};
    const auto check = [&](const std::uint64_t* in, std::uint8_t* out) {
        byte_grain::array<Values, Join>(in, out, Values);
        for (unsigned i = 0; i < Values; ++i)
            if (out[i] != std::uint8_t(in[i])) std::abort();
        ++checks;
    };
    check(input.data(), encoded.data());
    for (unsigned i = 0; i < Values; ++i) {
        for (unsigned bit : {0U,1U,2U,3U,4U,5U,6U,7U,8U,31U,63U}) {
            input[i] = std::uint64_t(1) << bit;
            check(input.data(), encoded.data()); input[i] = 0;
        }
    }
    std::uint64_t random = 0x59a238dd1a9b107eULL;
    for (unsigned trial = 0; trial < 16; ++trial) {
        for (auto& value : input) {
            random ^= random >> 12; random ^= random << 25; random ^= random >> 27;
            value = random * 0x2545f4914f6cdd1dULL;
        }
        check(input.data(), encoded.data());
    }
    auto exact_input = std::make_unique<std::uint64_t[]>(Values);
    auto exact_output = std::make_unique<std::uint8_t[]>(Values);
    std::copy(input.begin(), input.end(), exact_input.get());
    check(exact_input.get(), exact_output.get());
    for (unsigned offset = 0; offset < 64; ++offset) {
        std::array<std::uint8_t, Values + 128> storage;
        storage.fill(0xa5); check(input.data(), storage.data() + 32 + offset);
        for (unsigned i = 0; i < storage.size(); ++i)
            if ((i < 32 + offset || i >= 32 + offset + Values) && storage[i] != 0xa5) std::abort();
    }
    page source, output;
    for (auto* in : {source.data + source.size, source.data + 2 * source.size - Values * 8}) {
        std::memcpy(in, input.data(), Values * 8);
        for (auto* out : {output.data + output.size, output.data + 2 * output.size - Values})
            check(reinterpret_cast<const std::uint64_t*>(in), out);
    }
    byte_grain::array<Values, Join>(nullptr, nullptr, 0);
}
}
int main() {
    verify<32, false>(); verify<32, true>(); verify<64, false>(); verify<64, true>();
    verify<256, false>(); verify<256, true>();
    std::printf("NEON byte gather: %zu projection, exact and guarded cases passed\n", checks);
}
