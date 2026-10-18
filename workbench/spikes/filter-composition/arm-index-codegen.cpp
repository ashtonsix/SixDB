#define FILTER_ARM_REDUCTION 1
#define FILTER_INDEX_NO_MAIN 1
#include "indexof.cpp"

#define WITNESS(Name,Arm) \
extern "C" [[gnu::noinline]] unsigned Name(const std::uint8_t* p,std::uint64_t query,std::uint64_t active) { \
    return index_probe::lookup<30,16,Arm>(p,query,active); \
}
WITNESS(index_current,0)
WITNESS(index_fused,3)
WITNESS(index_bounded_full,9)
WITNESS(index_bounded_first,7)
WITNESS(index_graduated,11)
extern "C" [[gnu::noinline]] unsigned equal6_bounded(const std::uint8_t* p,std::uint64_t query) {
    return ikea::seriespack::detail::filter_local8<ikea::comparison::equal,6>(p,query);
}
