#include "support.h"
#include <cstdio>
#include <cstdlib>
namespace ikea_test {
thread_local const scope* current = nullptr;
scope::scope(const char* name, std::size_t begin, std::size_t size, std::size_t bits)
    : label(name), first(begin), count(size), mask(bits), parent(current) {
    current = this;
}
scope::~scope() {
    current = parent;
}
[[noreturn]] void fail(const char* expression, std::source_location where) {
    std::fprintf(stderr, "%s:%u: %s\n  failed: %s\n", where.file_name(), where.line(),
                 where.function_name(), expression);
    for (auto* context = current; context; context = context->parent) {
        std::fprintf(stderr, "  %s: first=%zu count=%zu mask=%#zx", context->label, context->first,
                     context->count, context->mask);
        if (context->width)
            std::fprintf(stderr, " width=%u heads=%u geometry=%u dense=%u", context->width,
                         context->heads, context->geometry, context->dense);
        std::fputc('\n', stderr);
    }
    std::abort();
}
} // namespace ikea_test
