#include "prefetch.h"

#include <limits>
#include <stdexcept>

namespace loom_spike::prefetch {
namespace {
constexpr std::array<std::string_view, classes> names{
    "none", "l1-1", "l2-1", "l3-1", "l1-2", "l2-2", "l1-4", "l2-4",
    "l1-8", "l2-8", "l1-l2-2", "l1-l2-4", "page1", "page2", "page4", "page8"};

std::size_t last_line(Class kind) {
    switch (kind) {
    case Class::l1_2: case Class::l2_2: return 64;
    case Class::l1_4: case Class::l2_4: return 192;
    case Class::l1_8: case Class::l2_8: return 448;
    case Class::l1_l2_2: return 128;
    case Class::l1_l2_4: return 256;
    default: return 0;
    }
}
void geometry(std::size_t page_bytes) {
    if (page_bytes < 64 || !std::has_single_bit(page_bytes) ||
        page_bytes > std::numeric_limits<std::size_t>::max() / 16)
        throw std::invalid_argument("prefetch geometry must be a bounded power of two >=64");
}
bool fits(Class kind, std::size_t bytes, std::size_t first_page, std::size_t stride) {
    if (kind == Class::none)
        return true;
    if (!bytes)
        return false;
    if (const auto count = pages(kind))
        return first_page + (count - 1) * stride + 64 < bytes;
    return last_line(kind) < bytes;
}
Class smaller(Class kind) {
    switch (kind) {
    case Class::page8: return Class::page4;
    case Class::page4: return Class::page2;
    case Class::page2: return Class::page1;
    case Class::page1: return Class::l1_1;
    case Class::l1_8: return Class::l1_4;
    case Class::l1_4: return Class::l1_2;
    case Class::l1_2: return Class::l1_1;
    case Class::l2_8: return Class::l2_4;
    case Class::l2_4: return Class::l2_2;
    case Class::l2_2: return Class::l2_1;
    case Class::l1_l2_4: return Class::l1_l2_2;
    case Class::l1_l2_2: return Class::l1_1;
    default: return Class::none;
    }
}
Binding choose(Class requested, std::size_t bytes, std::size_t first_page, std::size_t stride) {
    while (!fits(requested, bytes, first_page, stride))
        requested = smaller(requested);
    return {requested, stride};
}
} // namespace

std::string_view name(Class kind) { return names.at(unsigned(kind)); }
Class parse(std::string_view value) {
    for (unsigned i = 0; i < classes; ++i)
        if (value == names[i])
            return Class(i);
    throw std::invalid_argument("unknown prefetch class");
}
bool permitted(Class kind, std::span<const std::byte> span, std::size_t page_bytes) {
    geometry(page_bytes);
    if (unsigned(kind) >= classes)
        throw std::invalid_argument("prefetch class index out of bounds");
    const auto address = reinterpret_cast<std::uintptr_t>(span.data());
    const auto first_page = page_bytes - (address & (page_bytes - 1));
    return fits(kind, span.size(), first_page, page_bytes);
}
Binding bind(Class requested, std::span<const std::byte> span, std::size_t page_bytes) {
    geometry(page_bytes);
    if (unsigned(requested) >= classes)
        throw std::invalid_argument("prefetch class index out of bounds");
    const auto address = reinterpret_cast<std::uintptr_t>(span.data());
    return choose(requested, span.size(), page_bytes - (address & (page_bytes - 1)), page_bytes);
}
Binding bind_shape(Class requested, std::size_t minimum_bytes, std::size_t page_bytes) {
    geometry(page_bytes);
    if (unsigned(requested) >= classes)
        throw std::invalid_argument("prefetch class index out of bounds");
    return choose(requested, minimum_bytes, page_bytes, page_bytes);
}
Region prepare(Binding binding, const void* address) {
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    if (pages(binding.kind))
        return {static_cast<const char*>(address),
                reinterpret_cast<const char*>((value & ~(binding.page_bytes - 1)) + binding.page_bytes),
                binding.page_bytes};
    return {static_cast<const char*>(address), nullptr, binding.page_bytes};
}

#define LOOM_HINT_ENTRY(N, KIND) \
extern "C" __attribute__((noinline)) void loom_hint_##N(const char* base, const char* page, std::size_t stride) { \
    issue<Class::KIND>(base, page, stride); \
}
LOOM_HINT_ENTRY(0, none)
LOOM_HINT_ENTRY(1, l1_1)
LOOM_HINT_ENTRY(2, l2_1)
LOOM_HINT_ENTRY(3, l3_1)
LOOM_HINT_ENTRY(4, l1_2)
LOOM_HINT_ENTRY(5, l2_2)
LOOM_HINT_ENTRY(6, l1_4)
LOOM_HINT_ENTRY(7, l2_4)
LOOM_HINT_ENTRY(8, l1_8)
LOOM_HINT_ENTRY(9, l2_8)
LOOM_HINT_ENTRY(10, l1_l2_2)
LOOM_HINT_ENTRY(11, l1_l2_4)
LOOM_HINT_ENTRY(12, page1)
LOOM_HINT_ENTRY(13, page2)
LOOM_HINT_ENTRY(14, page4)
LOOM_HINT_ENTRY(15, page8)
#undef LOOM_HINT_ENTRY

const std::array<Function, classes>& entries() {
    static constexpr std::array<Function, classes> functions{
        loom_hint_0, loom_hint_1, loom_hint_2, loom_hint_3,
        loom_hint_4, loom_hint_5, loom_hint_6, loom_hint_7,
        loom_hint_8, loom_hint_9, loom_hint_10, loom_hint_11,
        loom_hint_12, loom_hint_13, loom_hint_14, loom_hint_15};
    return functions;
}
void issue_runtime(Class kind, const char* base, const char* page, std::size_t page_bytes) {
    entries()[unsigned(kind)](base, page, page_bytes);
}
} // namespace loom_spike::prefetch
