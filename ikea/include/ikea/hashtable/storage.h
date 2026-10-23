#pragma once
#include <ikea/hashtable/types.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ikea::hashtable {
struct storage_usage {
    std::size_t assigned = 0, accessible = 0, reserved = 0;
    storage_usage& operator+=(storage_usage other) {
        assigned += other.assigned;
        accessible += other.accessible;
        reserved += other.reserved;
        return *this;
    }
};
// Exact private copy of the assigned representation. Owners initialize every
// assigned byte; allocator slack outside that extent is neither read nor copied.
// A new buffer identity is required even when the destination context is shared.
template <class Storage>
typename Storage::buffer duplicate_buffer(const typename Storage::buffer& source,
                                          typename Storage::context* memory) {
    if (!source.size)
        return {};
    typename Storage::buffer result(source.size, memory, false, source.size);
    std::memcpy(result.data, source.data, source.size);
    return result;
}
// Ordinary aligned heap storage. Captured descriptors retain the complete owner,
// which owns these buffers; no separate allocator retention token is required.
struct heap_storage_policy {
    struct context {};
    struct retention {};
    using trim_result = std::expected<void, error>;
    class buffer {
        std::size_t capacity_ = 0;
        static std::size_t round64(std::size_t n) {
            if (n > std::numeric_limits<std::size_t>::max() - 63)
                throw std::length_error("allocation extent");
            return (n + 63) & ~std::size_t{63};
        }

      public:
        std::byte* data = nullptr;
        std::size_t size = 0;
        buffer() = default;
        explicit buffer(std::size_t n, context* = nullptr, bool zero = true,
                        std::size_t reserve = 0) {
            n = round64(n);
            reserve = round64(std::max(n, reserve));
            if (reserve) {
                data = static_cast<std::byte*>(std::aligned_alloc(64, reserve));
                if (!data)
                    throw std::bad_alloc();
            }
            size = n;
            capacity_ = reserve;
            if (zero && n)
                std::memset(data, 0, n);
        }
        buffer(const buffer&) = delete;
        buffer(buffer&& other) noexcept {
            swap(other);
        }
        buffer& operator=(buffer&& other) noexcept {
            swap(other);
            return *this;
        }
        ~buffer() {
            std::free(data);
        }
        void swap(buffer& other) noexcept {
            std::swap(data, other.data);
            std::swap(size, other.size);
            std::swap(capacity_, other.capacity_);
        }
        std::size_t capacity() const {
            return capacity_;
        }
        std::size_t reserved_bytes() const {
            return capacity_;
        }
        storage_usage usage() const {
            return {size, capacity_, capacity_};
        }
        void grow(std::size_t n, std::size_t reserve = 0, bool zero_new = true) {
            n = round64(n);
            reserve = round64(std::max(n, reserve));
            if (n < size)
                throw std::invalid_argument("growth would contract storage");
            const auto previous = size;
            if (reserve > capacity_) {
                buffer next(n, nullptr, false, reserve);
                if (size)
                    std::memcpy(next.data, data, size);
                swap(next);
            } else
                size = n;
            if (zero_new && n > previous)
                std::memset(data + previous, 0, n - previous);
        }
        void retain(retention&) const {}
        trim_result relinquish_tail(std::size_t n) {
            n = round64(n);
            if (n > size)
                return std::unexpected(error::range);
            size = n;
            return {};
        }
        trim_result relinquish_surplus() {
            return {};
        }
    };
};
} // namespace ikea::hashtable
