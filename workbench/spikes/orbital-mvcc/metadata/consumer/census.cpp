#include "types.h"
#include <cstdio>
using namespace consumer_metadata;

int main() {
    std::puts("[");
    bool first = true;
#define PRINT_TYPE(name, type) \
    std::printf("%s{\"type\":\"%s\",\"bytes\":%zu,\"alignment\":%zu,\"trivially_copyable\":%s,\"trivial_destructor\":%s,\"trivial_move\":%s,\"standard_layout\":%s}", \
        first ? "" : ",\n", #name, sizeof(type), alignof(type), \
        std::is_trivially_copyable_v<type> ? "true" : "false", \
        std::is_trivially_destructible_v<type> ? "true" : "false", \
        std::is_trivially_move_constructible_v<type> ? "true" : "false", \
        std::is_standard_layout_v<type> ? "true" : "false"); \
    first = false;
    CONSUMER_METADATA_TYPES(PRINT_TYPE)
#undef PRINT_TYPE
    std::puts("\n]");
}
