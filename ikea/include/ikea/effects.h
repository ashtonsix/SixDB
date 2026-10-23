#pragma once
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <span>

namespace ikea {
/// Issued physical stores, including preserved neighbors. The named source
/// and preallocated record storage outlive execution and owner resolution.
/// Every Ikea effect source exposes plane_storage(plane): resolve a record as
/// that span's data() + offset before the source is retargeted or destroyed.
/// These records neither capture beforeimages nor publish a transaction.
struct journal {
    struct entry {
        const void* source;
        unsigned plane;
        std::size_t offset, size;
    };
    std::span<entry> storage;
    std::size_t used = 0;
    /// Trusted no-fail hook: capacity, disjointness and byte bounds were admitted
    /// before execution. Coalesces only the last source/plane's touching spans.
    template <class Source>
    void before(const Source& source, unsigned plane, std::size_t offset,
                std::size_t size) noexcept {
        if (used && storage[used - 1].source == &source && storage[used - 1].plane == plane) {
            auto& previous = storage[used - 1];
            const auto end = previous.offset + previous.size, next_end = offset + size;
            if (previous.offset <= next_end && offset <= end) {
                const auto begin = std::min(previous.offset, offset);
                previous.offset = begin;
                previous.size = std::max(end, next_end) - begin;
                return;
            }
        }
        storage[used++] = {&source, plane, offset, size};
    }
    std::size_t remaining() const noexcept { return storage.size() - used; }
    std::span<const entry> entries() const noexcept { return storage.first(used); }
};
struct no_effects {
    template <class Source>
    void before(const Source&, unsigned, std::size_t, std::size_t) const noexcept {}
};
/// No-fail, nonsuspending local mutation hook. Admission and owner isolation
/// precede the first store group; execution uses only pre-admitted resources.
template <class E, class Source>
concept effects = requires(E& e, const Source& source, unsigned plane,
                           std::size_t offset, std::size_t size) {
    { e.before(source, plane, offset, size) } noexcept -> std::same_as<void>;
};
namespace detail {
// Plane-local geometry for kernel visitors; effects always name a source too.
struct write_span { unsigned plane; std::size_t offset, size; };
}
} // namespace ikea
