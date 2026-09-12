#pragma once
#include <ikea/tuplepack/description.h>
#include <limits>
#include <type_traits>

namespace ikea::tuplepack {
/// Borrowed placement. Offsets/effects are relative to storage(), including the
/// unit offset. Keep this named view and its bytes alive through bound calls.
/// Stride covers a whole unit; adjacent units may share the same larger stride.
template <class Byte> class basic_view {
    static_assert(std::is_same_v<std::remove_const_t<Byte>, byte>);
    std::span<Byte> storage_;
    std::size_t count_, stride_, offset_;
    unsigned bytes_;
    basic_view(std::span<Byte> storage, std::size_t count, std::size_t stride, std::size_t offset,
               unsigned bytes)
        : storage_(storage), count_(count), stride_(stride), offset_(offset), bytes_(bytes) {}

  public:
    [[nodiscard]] static std::expected<basic_view, error>
    bind(const layout& format, std::span<Byte> storage, std::size_t count, std::size_t stride,
         std::size_t offset = 0) {
        if (stride < format.bytes())
            return std::unexpected(error::stride);
        if (reinterpret_cast<std::uintptr_t>(storage.data()) >
            std::numeric_limits<std::uintptr_t>::max() - storage.size())
            return std::unexpected(error::overflow);
        if (offset > storage.size())
            return std::unexpected(error::capacity);
        if (count) {
            const auto available = storage.size() - offset;
            if (available < format.bytes() || (count - 1) > (available - format.bytes()) / stride)
                return std::unexpected(error::capacity);
        }
        return basic_view(storage, count, stride, offset, format.bytes());
    }
    std::span<Byte> storage() const noexcept {
        return storage_;
    }
    std::size_t size() const noexcept {
        return count_;
    }
    std::size_t stride() const noexcept {
        return stride_;
    }
    std::size_t offset() const noexcept {
        return offset_;
    }
    unsigned unit_bytes() const noexcept {
        return bytes_;
    }
    /// Trusted original row coordinate; requires row<size().
    Byte* row_unchecked(std::size_t row) const noexcept {
        return storage_.data() + offset_ + row * stride_;
    }
};
using view = basic_view<byte>;
using const_view = basic_view<const byte>;
} // namespace ikea::tuplepack
