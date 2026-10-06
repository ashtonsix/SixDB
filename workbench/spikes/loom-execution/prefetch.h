#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace loom_spike::prefetch {

// Experimental desired hint patterns, not readiness/access-right descriptions.
// A parent binds one permitted pattern before entering the native callback.
enum class Class : unsigned {
    none, l1_1, l2_1, l3_1, l1_2, l2_2, l1_4, l2_4,
    l1_8, l2_8, l1_l2_2, l1_l2_4, page1, page2, page4, page8
};
inline constexpr std::size_t classes = 16;

constexpr unsigned pages(Class kind) {
    switch (kind) {
    case Class::page1: return 1;
    case Class::page2: return 2;
    case Class::page4: return 4;
    case Class::page8: return 8;
    default: return 0;
    }
}
constexpr unsigned hints(Class kind) {
    constexpr std::array counts{0u, 1u, 1u, 1u, 2u, 2u, 4u, 4u,
                                8u, 8u, 3u, 5u, 3u, 5u, 9u, 17u};
    return counts[unsigned(kind)];
}

// Page geometry is separately retained. Line classes use only base; page
// classes use first_following_page and a qualified page stride. These are
// flattened at the runtime-dispatch ABI as well.
struct Region {
    const char* base = nullptr;
    const char* first_following_page = nullptr;
    std::size_t page_bytes = 4096;
};
struct Binding {
    Class kind = Class::none;
    std::size_t page_bytes = 4096;
};

std::string_view name(Class);
Class parse(std::string_view);
// Cold qualification for a concrete span, including unaligned starts.
bool permitted(Class, std::span<const std::byte>, std::size_t page_bytes);
Binding bind(Class requested, std::span<const std::byte>, std::size_t page_bytes = 4096);
// Cold conservative qualification for a sequence whose future addresses are
// not yet discovered. Every future window must meet this minimum span.
Binding bind_shape(Class requested, std::size_t minimum_bytes, std::size_t page_bytes = 4096);
Region prepare(Binding, const void* address);

template <unsigned Locality, std::size_t... I>
[[gnu::always_inline]] inline void lines(const char* base, std::index_sequence<I...>) {
    (__builtin_prefetch(base + I * 64, 0, Locality), ...);
}
template <std::size_t... I>
[[gnu::always_inline]] inline void page_lines(const char* first, std::size_t stride,
                                              std::index_sequence<I...>) {
    ((__builtin_prefetch(first + I * stride, 0, 2),
      __builtin_prefetch(first + I * stride + 64, 0, 2)), ...);
}

// No size, null, policy, rights, readiness or counter branch belongs here.
// Callers establish the class bounds in bind/bind_shape before native entry.
template <Class Kind>
[[gnu::always_inline]] inline void issue(const char* base, const char* page,
                                        std::size_t page_bytes) {
    if constexpr (Kind == Class::l1_1) lines<3>(base, std::make_index_sequence<1>{});
    else if constexpr (Kind == Class::l2_1) lines<2>(base, std::make_index_sequence<1>{});
    else if constexpr (Kind == Class::l3_1) lines<1>(base, std::make_index_sequence<1>{});
    else if constexpr (Kind == Class::l1_2) lines<3>(base, std::make_index_sequence<2>{});
    else if constexpr (Kind == Class::l2_2) lines<2>(base, std::make_index_sequence<2>{});
    else if constexpr (Kind == Class::l1_4) lines<3>(base, std::make_index_sequence<4>{});
    else if constexpr (Kind == Class::l2_4) lines<2>(base, std::make_index_sequence<4>{});
    else if constexpr (Kind == Class::l1_8) lines<3>(base, std::make_index_sequence<8>{});
    else if constexpr (Kind == Class::l2_8) lines<2>(base, std::make_index_sequence<8>{});
    else if constexpr (Kind == Class::l1_l2_2) {
        __builtin_prefetch(base, 0, 3);
        __builtin_prefetch(base + 64, 0, 2);
        __builtin_prefetch(base + 128, 0, 2);
    } else if constexpr (Kind == Class::l1_l2_4) {
        __builtin_prefetch(base, 0, 3);
        lines<2>(base + 64, std::make_index_sequence<4>{});
    } else if constexpr (pages(Kind)) {
        __builtin_prefetch(base, 0, 3);
        page_lines(page, page_bytes, std::make_index_sequence<pages(Kind)>{});
    }
}
template <Class Kind>
[[gnu::always_inline]] inline Region at(const void* address, std::size_t page_bytes) {
    const auto* base = static_cast<const char*>(address);
    if constexpr (pages(Kind)) {
        // Power-of-two geometry is proved by the outer binder. Integer address
        // arithmetic locates the next page; every issued target is qualified.
        const auto value = reinterpret_cast<std::uintptr_t>(address);
        const auto next = (value & ~(page_bytes - 1)) + page_bytes;
        return {base, reinterpret_cast<const char*>(next), page_bytes};
    } else
        return {base, nullptr, page_bytes};
}
template <Class Kind>
[[gnu::always_inline]] inline void issue(const void* address, std::size_t page_bytes) {
    const auto region = at<Kind>(address, page_bytes);
    issue<Kind>(region.base, region.first_following_page, region.page_bytes);
}

// Separate explicitly measured deferred-class path. Its inputs are already
// bound, and its target routines contain the same fixed hint instructions.
void issue_runtime(Class, const char* base, const char* page, std::size_t page_bytes);
[[gnu::always_inline]] inline void issue_runtime(Class kind, Region region) {
    issue_runtime(kind, region.base, region.first_following_page, region.page_bytes);
}

// Named fixed-class entries make the native lowering independently inspectable.
using Function = void (*)(const char*, const char*, std::size_t);
const std::array<Function, classes>& entries();

} // namespace loom_spike::prefetch
