#include "recipe_view.h"

// Isolated loading/masking witnesses. These do not replace the native audits
// of complete timed lookup loops or establish a performance result.
#if defined(__AVX2__)
#define SELECTOR_PROBE(Bytes) \
extern "C" [[gnu::noinline]] ht::word selector_composed_##Bytes(const ht::byte* p,ht::byte fp,unsigned count) { \
    return ht::recipe_matches32<Bytes,false>(p,fp,count); \
} \
extern "C" [[gnu::noinline]] ht::word selector_direct_##Bytes(const ht::byte* p,ht::byte fp,unsigned count) { \
    return ht::recipe_matches32<Bytes,true>(p,fp,count); \
}
SELECTOR_PROBE(17)
SELECTOR_PROBE(23)
SELECTOR_PROBE(31)
SELECTOR_PROBE(32)
#undef SELECTOR_PROBE
#endif
