#pragma once
#include <algorithm>
#include <cstddef>
#include <span>
namespace ikea2::seriespack {
struct byte_write {
    unsigned plane;
    std::size_t offset, size;
};
struct no_coverage {
    void before(byte_write) const noexcept {}
};
/// Borrowed, preallocated output. Offsets belong to the actual attached plane;
/// an integration adapter maps them to partition/owner identities. These are
/// issued writes, including preserved neighbors, not a byte-difference list.
struct write_journal {
    std::span<byte_write> storage;
    std::size_t used = 0;
    void before(byte_write write) noexcept {
        if (used && storage[used - 1].plane == write.plane) {
            auto& previous = storage[used - 1];
            const auto end = previous.offset + previous.size, next_end = write.offset + write.size;
            if (previous.offset <= next_end && write.offset <= end) {
                const auto begin = std::min(previous.offset, write.offset);
                previous = {write.plane, begin, std::max(end, next_end) - begin};
                return;
            }
        }
        storage[used++] = write;
    }
    std::span<const byte_write> entries() const {
        return storage.first(used);
    }
};

} // namespace ikea2::seriespack
namespace ikea2::seriespack::composition {
struct owner_write {
    const void* source;
    byte_write bytes;
};
/// Identities are the actual named leaf sources, not the original parent whose
/// children may have been replaced. The owner resolves them to storage mappings.
struct write_journal {
    std::span<owner_write> storage;
    std::size_t used = 0;
    template <class S> void before(const S& source, byte_write bytes) noexcept {
        if (used && storage[used - 1].source == &source &&
            storage[used - 1].bytes.plane == bytes.plane) {
            auto& previous = storage[used - 1].bytes;
            const auto end = previous.offset + previous.size, next_end = bytes.offset + bytes.size;
            if (previous.offset <= next_end && bytes.offset <= end) {
                const auto begin = std::min(previous.offset, bytes.offset);
                previous = {bytes.plane, begin, std::max(end, next_end) - begin};
                return;
            }
        }
        storage[used++] = {&source, bytes};
    }
    std::span<const owner_write> entries() const {
        return storage.first(used);
    }
};

} // namespace ikea2::seriespack::composition
