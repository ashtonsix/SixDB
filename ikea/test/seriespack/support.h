#pragma once
#include <cstddef>
#include <source_location>

namespace ikea_test {
// Diagnostic state is outside timed kernels. Nested scopes retain the format
// when a shared verification helper reports the failed byte or event.
struct scope {
    const char* label;
    std::size_t first, count, mask;
    unsigned width = 0, heads = 0, geometry = 0;
    bool dense = false;
    const scope* parent;
    explicit scope(const char* label, std::size_t first = 0, std::size_t count = 0,
                   std::size_t mask = 0);
    ~scope();
    scope(const scope&) = delete;
    scope& operator=(const scope&) = delete;
};
template <class F> struct format_scope : scope {
    explicit format_scope(const char* label, bool dense = false) : scope(label) {
        width = F::width;
        heads = F::heads;
        geometry = unsigned(F::storage);
        this->dense = dense;
    }
};
[[noreturn]] void fail(const char* expression,
                       std::source_location location = std::source_location::current());
} // namespace ikea_test

#define IKEA_CHECK(...)                                                                           \
    do {                                                                                           \
        if (!(__VA_ARGS__))                                                                        \
            ikea_test::fail(#__VA_ARGS__);                                                        \
    } while (false)
