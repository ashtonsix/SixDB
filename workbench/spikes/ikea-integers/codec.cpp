#include "codec.h"
#include "local.h"
#include "scan.h"

namespace ikea::integers {
#if defined(IP_SCAN_CONSTANT_OFFSETS)
constexpr auto endpoint_scan_reader=ScanReader::constant_offsets;
#else
constexpr auto endpoint_scan_reader=ScanReader::fragment_classes;
#endif
// This materializing endpoint covers 256 values, like its comparands. Convey
// that trusted contract without validation in the generic arbitrary-index read.
template<Layout L,unsigned K> __attribute__((aligned(64))) uint8_t get1(const uint8_t* p,unsigned i) {
    __builtin_assume(i<256);
    if constexpr(L==Layout::local) return local_point<K>(p,i);
    else return scan_point<K,endpoint_scan_reader>(p,i);
}
template<Layout L,unsigned K> void get16(const uint8_t* p,unsigned i,uint8_t* out) {
    __builtin_assume(i<256 && i%16==0);
    if constexpr(L==Layout::local) local_read16<K>(p,i).store(out);
    else scan_read16<K,endpoint_scan_reader>(p,i).store(out);
}
template<Layout L,unsigned K> constexpr Codec make_codec() {
    if constexpr(L==Layout::local) return {get1<L,K>,get16<L,K>,local_decode<K>,local_encode<K>};
    else return {get1<L,K>,get16<L,K>,scan_decode<K>,scan_encode<K>};
}
template<Layout L> const std::array<Codec,7> bank={make_codec<L,1>(),make_codec<L,2>(),make_codec<L,3>(),make_codec<L,4>(),make_codec<L,5>(),make_codec<L,6>(),make_codec<L,7>()};
const std::array<Codec,7>& codecs(Layout layout) { return layout==Layout::local?bank<Layout::local>:bank<Layout::scan>; }
} // namespace ikea::integers
