#pragma once

#include "graph.h"
#include <memory>
#include <string>

namespace ikea::composition {
struct Program;

enum class Layout { contiguous, strided };
struct Description {
    SourceChild child;
    Layout layout;
    unsigned positions_per_tile=256;
    std::size_t offset=0, stride=32, tile_count=0;
};
// Admission retains an immutable segment owner, so its byte addresses, source
// metadata and model binding share one lifetime. Existing data is never migrated
// by preparing an executable. The integration contract forbids mutable aliases.
struct Segment {
    Description description;
    std::vector<std::uint8_t> bytes;
};

struct Binding {
    const std::uint8_t* first;
    std::size_t count, stride;
    const Program* program;
};
enum class Execution { inline_named, inline_function, cps };
using Entry = std::uint64_t (*)(const void* erased_binding);
struct Prepared {
    std::shared_ptr<const Segment> owner;
    std::shared_ptr<const Program> code;
    Binding binding{};
    Entry entry=nullptr;
    std::uint64_t operator()() const { return entry(&binding); }
};

// This cold operation checks the actual source/graph/target once. Failure text
// is a wrapper concern. No admission or dispatch condition is repeated per tile.
bool prepare(std::shared_ptr<const Segment>, const Graph&, Execution, Prepared&, std::string& error);

extern "C" std::uint64_t ikea_comp_run_cps(const void*);
extern "C" std::uint64_t ikea_comp_run_named(const void*);
extern "C" std::uint64_t ikea_comp_run_function(const void*);

} // namespace ikea::composition
