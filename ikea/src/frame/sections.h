// Little-endian fields of a stage's image section, written by emit and read
// back by attach without reading past the section.
#pragma once
#include <ikea/frame/types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ikea::frame::detail {

template <class T> void put(std::vector<std::byte> &out, T v) {
    for (unsigned i = 0; i < sizeof v; ++i)
        out.push_back(static_cast<std::byte>(static_cast<std::uint64_t>(v) >> (8 * i)));
}
inline void put_bytes(std::vector<std::byte> &out, std::span<const std::byte> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

/// A bounds-checked reader over one section. Any short read makes it fail,
/// and every later read returns zero.
struct cursor {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    bool ok = true;
    template <class T> T take() noexcept {
        if (!ok || sizeof(T) > bytes.size() - at) {
            ok = false;
            return 0;
        }
        std::uint64_t x = 0;
        for (unsigned i = 0; i < sizeof(T); ++i)
            x |= std::uint64_t{std::to_integer<std::uint8_t>(bytes[at++])} << (8 * i);
        return static_cast<T>(x);
    }
    /// The next n bytes, or an empty span (and failure) if they are absent.
    std::span<const std::byte> span(std::size_t n) noexcept {
        if (!ok || n > bytes.size() - at) {
            ok = false;
            return {};
        }
        const auto s = bytes.subspan(at, n);
        at += n;
        return s;
    }
};

} // namespace ikea::frame::detail
