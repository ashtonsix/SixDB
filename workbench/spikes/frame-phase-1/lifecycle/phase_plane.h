#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace frame_lifecycle::phase {
struct PlaneEdit { unsigned row; std::uint64_t value; };
// Private initialized Local plane only. Caller proves every coordinate/value
// fits, proves Local extent/stride/overflow/non-overlap, owns and isolates the
// entire destination (including preserved bytes),
// and publishes only after completion. This raw leaf emits no effects or
// summaries and grants no authority to modify a retained/shared source.
// Empty edits touch no body or placement metadata.
void replace_private_plane(unsigned width, unsigned rows, std::uint8_t* body,
                           std::span<const PlaneEdit> edits);
}
