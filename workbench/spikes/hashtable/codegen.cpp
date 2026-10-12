#include "bucket.h"

extern "C" unsigned hashtable_fp3_decode(const byte* p, byte wanted) {
    return match_ikea<3>(p, wanted);
}
extern "C" unsigned hashtable_fp3_planes(const byte* p, byte wanted) {
    return match_planes<3>(p, wanted);
}
extern "C" unsigned hashtable_fp7_decode(const byte* p, byte wanted) {
    return match_ikea<7>(p, wanted);
}
extern "C" unsigned hashtable_fp7_planes(const byte* p, byte wanted) {
    return match_planes<7>(p, wanted);
}
extern "C" unsigned hashtable_fp8_decode(const byte* p, byte wanted) {
    return match_ikea<8>(p, wanted);
}
extern "C" word hashtable_tuple24_composed(const plans* p, const byte* row) {
    return tp::native::read_body(p->suffix.controls(),row);
}
extern "C" word hashtable_tuple24_fixed(const byte* row) {
    return load<3>(row);
}
extern "C" word hashtable_tuple24_admitted(const plans* p, const byte* row) {
    return admitted_transfer<3>(p->suffix,row);
}
