#pragma once
#include <ikea2/seriespack/detail/admission.h>
#include <vector>
namespace ikea2::seriespack::composition::detail {
using ikea2::seriespack::detail::occupied_run;
struct writable_fields {
    struct field {
        occupied_run bytes;
        const void* source;
        unsigned plane;
    };
    std::size_t count;
    std::vector<field> occupied;
    bool valid = true, aliased = false, allocation_failed = false;
    mutation_diagnostic diagnostic;
    explicit writable_fields(std::size_t rows) : count(rows) {}
    void add(occupied_run bytes, const void* source, unsigned plane);
};
} // namespace ikea2::seriespack::composition::detail
