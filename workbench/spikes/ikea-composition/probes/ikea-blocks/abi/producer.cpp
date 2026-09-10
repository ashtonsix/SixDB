#include "probe.h"

#define PRODUCE(N) \
    extern "C" V##N raw##N(const void* p) { return load<V##N>(p); } \
    extern "C" Tagged<V##N> tagged##N(const void* p, std::uint64_t t) { \
        return {t + 3, load<V##N>(p)}; \
    } \
    extern "C" Checked<V##N> expected##N(const void* p, bool valid) { \
        return checked_load<V##N>(p, valid); \
    } \
    extern "C" void cps##N(const void* p, std::uint64_t t, void* state, Next##N next) { \
        next(state, t + 3, load<V##N>(p)); \
    }
EACH_WIDTH(PRODUCE)
#undef PRODUCE

extern "C" ScalarChecked expected_scalar(std::uint64_t x, bool valid) {
    if (!valid) return std::unexpected(Error::invalid);
    return x + 3;
}
extern "C" HVA256 raw_hva(const void* p) {
    return {load<V128>(p), load<V128>(static_cast<const std::uint8_t*>(p) + 16)};
}
extern "C" void cps_chunks512(const void* p, std::uint64_t t, void* state, NextChunks512 next) {
    auto* b = static_cast<const std::uint8_t*>(p);
    next(state, t + 3, load<V128>(b), load<V128>(b+16), load<V128>(b+32), load<V128>(b+48));
}

#if defined(__x86_64__)
#define PRODUCE_REG(N) \
    extern "C" REGCALL Tagged<V##N> reg_tagged##N(const void* p, std::uint64_t t) { \
        return {t + 3, load<V##N>(p)}; \
    } \
    extern "C" REGCALL Checked<V##N> reg_expected##N(const void* p, bool valid) { \
        return checked_load<V##N>(p, valid); \
    }
EACH_WIDTH(PRODUCE_REG)
#undef PRODUCE_REG
extern "C" REGCALL UnionChecked<V128> reg_union128(const void* p, bool valid) {
    if (valid) return {.bits=load<V128>(p), .valid=true};
    return {.error=Error::invalid, .valid=false};
}
#endif
