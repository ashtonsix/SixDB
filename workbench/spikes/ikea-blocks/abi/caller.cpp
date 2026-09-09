#include "probe.h"

#define CALL(N) \
    extern "C" void call_raw##N(const void* p, Output* out) { \
        store(out->bits, raw##N(p)); \
    } \
    extern "C" std::uint64_t call_tagged##N(const void* p, std::uint64_t t, Output* out) { \
        auto v = tagged##N(p, t); store(out->bits, v.bits); return v.tag; \
    } \
    extern "C" unsigned call_expected##N(const void* p, bool valid, Output* out) { \
        auto v = expected##N(p, valid); \
        if (!v) return static_cast<unsigned>(v.error()); \
        store(out->bits, *v); return 0; \
    } \
    extern "C" void call_cps##N(const void* p, std::uint64_t t, Output* out) { \
        cps##N(p, t, out, sink##N); \
    } \
    extern "C" void inline_raw##N(const void* p, Output* out) { \
        store(out->bits, load<V##N>(p)); \
    } \
    extern "C" void inline_expected_success##N(const void* p, Output* out) { \
        auto v = checked_load<V##N>(p, true); store(out->bits, *v); \
    } \
    extern "C" unsigned inline_expected_dynamic##N(const void* p, bool valid, Output* out) { \
        auto v = checked_load<V##N>(p, valid); \
        if (!v) return static_cast<unsigned>(v.error()); \
        store(out->bits, *v); return 0; \
    }
EACH_WIDTH(CALL)
#undef CALL

extern "C" std::uint64_t call_expected_scalar(std::uint64_t x, bool valid) {
    auto v = expected_scalar(x, valid);
    return v ? *v : static_cast<std::uint64_t>(v.error());
}
extern "C" void call_hva(const void* p, Output* out) {
    auto v = raw_hva(p); store(out->bits, v.lo); store(out->bits + 16, v.hi);
}
extern "C" void call_cps_chunks512(const void* p, std::uint64_t t, Output* out) {
    cps_chunks512(p, t, out, sink_chunks512);
}

#if defined(__x86_64__)
#define CALL_REG(N) \
    extern "C" std::uint64_t call_reg_tagged##N(const void* p, std::uint64_t t, Output* out) { \
        auto v = reg_tagged##N(p, t); store(out->bits, v.bits); return v.tag; \
    } \
    extern "C" unsigned call_reg_expected##N(const void* p, bool valid, Output* out) { \
        auto v = reg_expected##N(p, valid); \
        if (!v) return static_cast<unsigned>(v.error()); \
        store(out->bits, *v); return 0; \
    }
EACH_WIDTH(CALL_REG)
#undef CALL_REG
extern "C" unsigned call_reg_union128(const void* p, bool valid, Output* out) {
    auto v = reg_union128(p, valid);
    if (!v.valid) return static_cast<unsigned>(v.error);
    store(out->bits, v.bits); return 0;
}
#endif
