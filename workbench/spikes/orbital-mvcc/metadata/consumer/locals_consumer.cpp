#if CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned/consumer.cpp"
#else
#include "consumer/consumer.cpp"
#endif
#include <type_traits>
namespace v = orbital_bench::versioned;
extern "C" const unsigned long consumer_local_LocalWriters[] = {
    sizeof(v::Writers), alignof(v::Writers), std::is_trivially_copyable_v<v::Writers>,
    std::is_trivially_destructible_v<v::Writers>,
    std::is_trivially_move_constructible_v<v::Writers>, std::is_standard_layout_v<v::Writers>};
extern "C" v::Writers consumer_abi_LocalWriters(v::Writers value) { return value; }
