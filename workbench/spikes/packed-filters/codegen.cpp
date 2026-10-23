#include "baseline.h"
#include <ikea/seriespack/filter.h>
#include <ikea/tuplepack/filter.h>
namespace sp=ikea::seriespack;
using C=ikea::comparison;
extern "C" std::uint64_t series8_equal(const std::uint8_t* p,std::uint64_t rhs) {
    return sp::filter_unchecked<C::equal>(sp::dense_source<sp::layout<8>>{reinterpret_cast<const std::byte*>(p),16},0,16,rhs);
}
extern "C" std::uint64_t fused8_equal(const std::uint8_t* p,std::uint64_t rhs) {
    return control::run<8,0>(p,rhs);
}
extern "C" std::uint64_t series3_equal(const std::uint8_t* p,std::uint64_t rhs) {
    return sp::filter_unchecked<C::equal>(sp::dense_source<sp::layout<3>>{reinterpret_cast<const std::byte*>(p),16},0,16,rhs);
}
extern "C" std::uint64_t packed3_equal(const std::uint8_t* p,std::uint64_t rhs) {
    return sp::detail::filter_local8<C::equal,3>(p,rhs)|
        (sp::detail::filter_local8<C::equal,3>(p+3,rhs)<<8);
}
extern "C" std::uint64_t series64_less(const std::uint8_t* p,std::uint64_t rhs) {
    return sp::filter_unchecked<C::less>(sp::dense_source<sp::layout<64>>{reinterpret_cast<const std::byte*>(p),16},0,16,rhs);
}
