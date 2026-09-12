#pragma once
#include <algorithm>
#include <cstddef>
#include <span>
namespace ikea {
/// Issued store coordinates within a source plane, in bytes. A bound operation
/// proves offset + size fits the admitted storage; this is not a changed-bit set.
struct byte_write {
    unsigned plane;
    std::size_t offset, size;
};
struct no_coverage {
    void before(byte_write) const noexcept {}
    template <class Source> void before(const Source&, byte_write) const noexcept {}
};
/// Borrowed, preallocated output. Offsets belong to the actual attached plane;
/// an integration adapter maps them to partition/owner identities. These are
/// issued writes, including preserved neighbors, not a byte-difference list.
/// Keep used <= storage.size(); before() is a trusted no-fail hook requiring
/// pre-admitted capacity. The storage must not alias the mutated bytes.
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
    std::size_t remaining() const noexcept {
        return storage.size() - used;
    }
    std::span<const byte_write> entries() const {
        return storage.first(used);
    }
};

struct owner_write {
    /// Borrowed named source identity; resolve it before that source expires.
    const void* source;
    byte_write bytes;
};
/// Identities are the actual named leaf sources, not the original parent whose
/// children may have been replaced. The owner resolves them to storage mappings.
/// Source objects and this preallocated output outlive production/resolution.
/// Keep used <= storage.size(); before() needs pre-admitted capacity and cannot
/// allocate, fail or suspend. Coalescing only the last entry is opportunistic.
struct source_write_journal {
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
    std::size_t remaining() const noexcept {
        return storage.size() - used;
    }
    std::span<const owner_write> entries() const {
        return storage.first(used);
    }
};

} // namespace ikea
