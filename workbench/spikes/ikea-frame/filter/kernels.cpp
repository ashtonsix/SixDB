#include "kernels.h"
#include "catalogue.h"
#include <array>
#include <cassert>
namespace frame_filter {
template <unsigned H> const std::array<const std::array<operations, 8> *, 8> tables{
    &catalogue<H, 1>::entries, &catalogue<H, 9>::entries,
    &catalogue<H, 17>::entries, &catalogue<H, 25>::entries,
    &catalogue<H, 33>::entries, &catalogue<H, 41>::entries,
    &catalogue<H, 49>::entries, &catalogue<H, 57>::entries};
const operations &kernels(unsigned width, unsigned heads) {
    assert(width && width <= 64 && width >= heads);
    assert(heads == 0 || heads == 8 || heads == 16);
    const auto &table = heads == 0 ? tables<0> : heads == 8 ? tables<8> : tables<16>;
    return (*table[(width - 1) / 8])[(width - 1) % 8];
}
}
