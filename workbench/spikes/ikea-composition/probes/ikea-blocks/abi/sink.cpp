#include "probe.h"

#define SINK(N) \
    extern "C" void sink##N(void* state, std::uint64_t tag, V##N v) { \
        auto* out = static_cast<Output*>(state); store(out->bits, v); out->tag = tag; \
    }
EACH_WIDTH(SINK)
#undef SINK
extern "C" void sink_chunks512(void* state, std::uint64_t tag, V128 a, V128 b, V128 c, V128 d) {
    auto* out = static_cast<Output*>(state);
    store(out->bits, a); store(out->bits+16, b); store(out->bits+32, c); store(out->bits+48, d);
    out->tag = tag;
}
