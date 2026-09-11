#pragma once
#include <string_view>
namespace ikea::seriespack {
/// Admission failure. These describe the command or borrowed storage; no
/// mutation, summary contribution or effect output has been committed on error.
enum class error {
    capacity,
    stride,
    alignment,
    overlap,
    range,
    value,
    overflow,
    description,
    unsupported,
    allocation
};
/// Optional cold binding diagnostic. Pointers name borrowed source views, never
/// data addresses to dereference. An overlap identifies both conflicting leaves;
/// range failure identifies the short source. Empty fields stay null.
struct mutation_diagnostic {
    const void* source = nullptr;
    const void* conflicting_source = nullptr;
    unsigned plane = 0, conflicting_plane = 0;
};
/// Cold diagnostic text; native kernel bodies do not format errors.
std::string_view describe(error reason) noexcept;
} // namespace ikea::seriespack
