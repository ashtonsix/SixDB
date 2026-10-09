#if CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned/data.cpp"
#else
#include "consumer/data.cpp"
#endif
#include <type_traits>
namespace v = orbital_bench::versioned;

// Include the implementation in this probe TU solely to observe its actual
// private types. No mirrored surrogate layout is substituted for those types.
#define LOCAL(name, type) \
    extern "C" const unsigned long consumer_local_##name[] = {sizeof(type), alignof(type), \
        std::is_trivially_copyable_v<type>, std::is_trivially_destructible_v<type>, \
        std::is_trivially_move_constructible_v<type>, std::is_standard_layout_v<type>}; \
    extern "C" type consumer_abi_##name(type value) { return value; }
LOCAL(LocalParent, v::Parent)
LOCAL(LocalWriteColumns, v::WriteColumns)
LOCAL(LocalWriteViews, v::WriteViews)
#undef LOCAL
