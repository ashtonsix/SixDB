#include "kernels.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {
std::size_t checks = 0;
std::uint64_t random_state = 0x74a95f0108d6be32ULL;
std::uint64_t random_value() {
    random_state ^= random_state >> 12; random_state ^= random_state << 25;
    random_state ^= random_state >> 27;
    return random_state * 0x2545f4914f6cdd1dULL;
}
struct guarded {
    std::uint8_t* allocation;
    std::size_t page = std::size_t(sysconf(_SC_PAGESIZE)), accessible, total;
    explicit guarded(std::size_t n) : accessible(std::max(page, (n + page - 1) / page * page)), total(accessible + 2 * page) {
        allocation = static_cast<std::uint8_t*>(mmap(nullptr, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (allocation == MAP_FAILED || mprotect(allocation + page, accessible, PROT_READ | PROT_WRITE)) std::abort();
    }
    ~guarded() { munmap(allocation, total); }
    std::uint8_t* begin() { return allocation + page; }
    std::uint8_t* end() { return allocation + page + accessible; }
};

template<unsigned W, unsigned Region, class UInt>
void check_family() {
    constexpr auto T = scan_grain::tile_values<W>;
    constexpr auto mask = (1u << W) - 1;
    for (std::size_t tiles = 0; tiles <= 9; ++tiles) {
        const auto n = tiles * T, bytes = n * W / 8;
        auto input = std::make_unique<UInt[]>(n), output = std::make_unique<UInt[]>(n);
        auto wire = std::make_unique<std::uint8_t[]>(bytes);
        std::vector<std::uint8_t> expected(bytes);
        std::vector<UInt> wanted(n);
        const auto prepare = [&] {
            scan_grain::oracle<W>(input.get(), expected.data(), n);
            for (std::size_t i = 0; i < n; ++i) wanted[i] = input[i] & mask;
        };
        const auto decode = [&](const std::uint8_t* source, std::uint8_t* destination) {
            scan_grain::native_region<W, Region, false, UInt>(source, destination, n);
            for (std::size_t i = 0; i < n; ++i) {
                UInt value;
                std::memcpy(&value, destination + i * sizeof(UInt), sizeof(UInt));
                if (value != wanted[i]) {
                    std::fprintf(stderr, "decode W%u region%u carrier%zu tiles%zu index%zu\n", W, Region, sizeof(UInt), tiles, i);
                    std::abort();
                }
            }
            ++checks;
        };
        const auto exercise = [&](const std::uint8_t* source, std::uint8_t* encoded, std::uint8_t* decoded) {
            scan_grain::native_region<W, Region, true, UInt>(source, encoded, n);
            if (!std::equal(expected.begin(), expected.end(), encoded)) {
                std::fprintf(stderr, "encode W%u region%u carrier%zu tiles%zu\n", W, Region, sizeof(UInt), tiles);
                std::abort();
            }
            ++checks;
            decode(expected.data(), decoded);
            decode(encoded, decoded);
        };
        const auto plain = [&] {
            exercise(reinterpret_cast<const std::uint8_t*>(input.get()), wire.get(), reinterpret_cast<std::uint8_t*>(output.get()));
        };
        for (unsigned trial = 0; trial < 8; ++trial) {
            for (std::size_t i = 0; i < n; ++i)
                input[i] = trial == 0 ? UInt(0) : trial == 1 ? ~UInt(0) : UInt(random_value());
            prepare(); plain();
        }
        if constexpr (sizeof(UInt) == 1) {
            if (tiles == 1) {
                std::fill_n(input.get(), n, 0);
                for (unsigned bit = 0; bit < n * 8; ++bit) {
                    input[bit / 8] = std::uint8_t(1u << (bit % 8));
                    prepare(); plain(); input[bit / 8] = 0;
                }
                for (std::size_t i = 0; i < n; ++i) input[i] = UInt(random_value());
                prepare();
            }
        }
        for (unsigned offset = 0; offset < 64; ++offset) {
            std::vector<std::uint8_t> packed(bytes + 128, 0xa5);
            std::vector<UInt> source(n + 64), decoded(n + 64, UInt(0xa5));
            const unsigned value_offset = offset / sizeof(UInt);
            std::copy_n(input.get(), n, source.data() + value_offset);
            exercise(reinterpret_cast<const std::uint8_t*>(source.data() + value_offset), packed.data() + 32 + offset,
                reinterpret_cast<std::uint8_t*>(decoded.data() + value_offset));
            for (std::size_t i = 0; i < packed.size(); ++i)
                if ((i < 32 + offset || i >= 32 + offset + bytes) && packed[i] != 0xa5) std::abort();
            for (std::size_t i = 0; i < decoded.size(); ++i)
                if ((i < value_offset || i >= value_offset + n) && decoded[i] != UInt(0xa5)) std::abort();
        }
        guarded source_page(n * sizeof(UInt)), wire_page(bytes), output_page(n * sizeof(UInt));
        for (auto* source : {source_page.begin(), source_page.end() - n * sizeof(UInt)})
            for (auto* encoded : {wire_page.begin(), wire_page.end() - bytes})
                for (auto* decoded : {output_page.begin(), output_page.end() - n * sizeof(UInt)}) {
                    std::memcpy(source, input.get(), n * sizeof(UInt));
                    exercise(source, encoded, decoded);
                }
    }
    scan_grain::native_region<W, Region, true, UInt>(nullptr, nullptr, 0);
    scan_grain::native_region<W, Region, false, UInt>(nullptr, nullptr, 0);
}

template<unsigned W, unsigned Region> void check_carriers() {
    check_family<W, Region, std::uint8_t>();
    check_family<W, Region, std::uint16_t>();
    check_family<W, Region, std::uint32_t>();
    check_family<W, Region, std::uint64_t>();
}
template<unsigned W> void check_width() {
    check_carriers<W, 0>(); check_carriers<W, 128>();
    check_carriers<W, 256>(); check_carriers<W, 512>();
}
} // namespace

int main() {
    check_width<4>(); check_width<6>();
    std::printf("Scan grain: %zu independent wire, projection, exact, offset and guard checks passed\n", checks);
}
