#include "verify.h"
#include "../support.h"

void verify_mutation_coverage(const void* source, const std::array<ikea::seriespack::plane<ikea::byte>, 3>& planes,
                              const std::array<std::vector<std::uint8_t>, 3>& before,
                              std::span<const ikea::journal::entry> writes,
                              std::array<std::size_t, 3> occupied) {
    // Compile the byte-level oracle once, independently of the native kernels
    // exercised by the 206 layout-specific mutation checks.
    for (const auto& write : writes) {
        IKEA_CHECK(write.source == source);
        IKEA_CHECK(write.plane < 3 && occupied[write.plane]);
        const auto& plane = planes[write.plane];
        IKEA_CHECK(write.offset <= plane.bytes.size() &&
                    write.size <= plane.bytes.size() - write.offset);
    }
    for (unsigned p = 0; p < 3; ++p) {
        if (!occupied[p])
            continue;
        const auto bytes = ikea::to_u8(planes[p].bytes);
        const auto stride = planes[p].stride;
        for (std::size_t b = 0; b < bytes.size(); ++b) {
            bool covered = false;
            for (const auto& write : writes)
                covered |= write.plane == p && b >= write.offset && b - write.offset < write.size;
            if (bytes[b] != before[p][b])
                IKEA_CHECK(covered);
            if (covered)
                IKEA_CHECK(b % stride < occupied[p]);
            if (b % stride >= occupied[p])
                IKEA_CHECK(bytes[b] == 0x96);
        }
    }
}
