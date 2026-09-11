#include "verify.h"
#include "../support.h"

void verify_mutation_coverage(const std::array<ikea2::seriespack::plane<std::uint8_t>, 3>& planes,
                              const std::array<std::vector<std::uint8_t>, 3>& before,
                              std::span<const ikea2::seriespack::byte_write> writes,
                              std::array<std::size_t, 3> occupied) {
    // Compile the byte-level oracle once, independently of the native kernels
    // exercised by the 206 format-specific mutation checks.
    for (const auto& write : writes) {
        IKEA2_CHECK(write.plane < 3 && occupied[write.plane]);
        const auto& plane = planes[write.plane];
        IKEA2_CHECK(write.offset <= plane.bytes.size() &&
                    write.size <= plane.bytes.size() - write.offset);
    }
    for (unsigned p = 0; p < 3; ++p) {
        if (!occupied[p])
            continue;
        const auto bytes = planes[p].bytes;
        const auto stride = planes[p].stride;
        for (std::size_t b = 0; b < bytes.size(); ++b) {
            bool covered = false;
            for (const auto& write : writes)
                covered |= write.plane == p && b >= write.offset && b - write.offset < write.size;
            if (bytes[b] != before[p][b])
                IKEA2_CHECK(covered);
            if (covered)
                IKEA2_CHECK(b % stride < occupied[p]);
            if (b % stride >= occupied[p])
                IKEA2_CHECK(bytes[b] == 0x96);
        }
    }
}
