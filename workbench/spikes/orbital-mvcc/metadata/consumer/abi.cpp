#include "types.h"
#include <utility>
using namespace consumer_metadata;

// No vectorcall, preserve-most, trivial_abi, LTO, or forced inlining. This is a
// separate translation unit with the target's ordinary C++ calling convention.
// The named C symbols make inspection mechanical; the parameter is still T.
#define ABI_TYPE(name, type) \
    extern "C" type consumer_abi_##name(type value) { return value; }
CONSUMER_METADATA_TYPES(ABI_TYPE)
#undef ABI_TYPE
