// Runtime width dispatch for the pack stage: forwards to the TU holding the
// width's instantiation (widths_*.cpp); layout constants need no kernels.
#include "stages.h"

namespace frame3 {
template <sp::preset P, unsigned First, unsigned Last> std::unique_ptr<width_array> make_width_range(unsigned, const std::vector<u64> &);

template <sp::preset P> std::unique_ptr<width_array> make_width_array(unsigned K, const std::vector<u64> &values) {
    if (K <= 16)
        return make_width_range<P, 1, 16>(K, values);
    if (K <= 32)
        return make_width_range<P, 17, 32>(K, values);
    if (K <= 48)
        return make_width_range<P, 33, 48>(K, values);
    return make_width_range<P, 49, 64>(K, values);
}
template <sp::preset P, unsigned First, unsigned Last>
std::unique_ptr<width_array> place_width_range(unsigned, std::span<const ikea::byte>, std::size_t, std::span<ikea::byte>);
template <sp::preset P>
std::unique_ptr<width_array> place_width_array(unsigned K, std::span<const ikea::byte> bytes, std::size_t rows, std::span<ikea::byte> writable) {
    if (K <= 16)
        return place_width_range<P, 1, 16>(K, bytes, rows, writable);
    if (K <= 32)
        return place_width_range<P, 17, 32>(K, bytes, rows, writable);
    if (K <= 48)
        return place_width_range<P, 33, 48>(K, bytes, rows, writable);
    return place_width_range<P, 49, 64>(K, bytes, rows, writable);
}
template <sp::preset P> std::size_t width_body_bytes(unsigned K) {
    std::size_t out = 0;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((K == I + 1 ? (out = block_rows / sp::preset_layout<I + 1, P>::tile_rows * sp::preset_layout<I + 1, P>::tile_bytes, 0) : 0), ...);
    }(std::make_integer_sequence<unsigned, 64>{});
    return out;
}
template std::unique_ptr<width_array> make_width_array<sp::preset::compact>(unsigned, const std::vector<u64> &);
template std::size_t width_body_bytes<sp::preset::compact>(unsigned);
template std::unique_ptr<width_array> place_width_array<sp::preset::compact>(unsigned, std::span<const ikea::byte>, std::size_t, std::span<ikea::byte>);
template std::unique_ptr<width_array> make_width_array<sp::preset::bulk_x86>(unsigned, const std::vector<u64> &);
template std::size_t width_body_bytes<sp::preset::bulk_x86>(unsigned);
template std::unique_ptr<width_array> place_width_array<sp::preset::bulk_x86>(unsigned, std::span<const ikea::byte>, std::size_t, std::span<ikea::byte>);
template std::unique_ptr<width_array> make_width_array<sp::preset::bulk_arm>(unsigned, const std::vector<u64> &);
template std::size_t width_body_bytes<sp::preset::bulk_arm>(unsigned);
template std::unique_ptr<width_array> place_width_array<sp::preset::bulk_arm>(unsigned, std::span<const ikea::byte>, std::size_t, std::span<ikea::byte>);
}  // namespace frame3
