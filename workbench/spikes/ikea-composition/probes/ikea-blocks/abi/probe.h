#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>

// These are compiler vector carriers, not physical block definitions. AArch64
// has no fixed-width native 256/512-bit vector in its base procedure-call ABI.
using V128 = std::uint64_t __attribute__((vector_size(16)));
using V256 = std::uint64_t __attribute__((vector_size(32)));
using V512 = std::uint64_t __attribute__((vector_size(64)));
enum class Error : std::uint8_t { invalid = 3 };
template<class V> struct Tagged { std::uint64_t tag; V bits; };
template<class V> struct UnionChecked { union { V bits; Error error; }; bool valid; };
template<class V> using Checked = std::expected<V, Error>;
using ScalarChecked = Checked<std::uint64_t>;
struct HVA256 { V128 lo, hi; };
struct Output { alignas(64) std::uint8_t bits[64]; std::uint64_t tag; };

template<class V> inline V load(const void* p) {
    V v;
    __builtin_memcpy(&v, p, sizeof(v));
    return v;
}
template<class V> inline void store(void* p, V v) {
    __builtin_memcpy(p, &v, sizeof(v));
}
template<class V> inline Checked<V> checked_load(const void* p, bool valid) {
    if (!valid) return std::unexpected(Error::invalid);
    return load<V>(p);
}

#define NOINLINE __attribute__((noinline))
#define REGCALL __attribute__((regcall))
#define EACH_WIDTH(M) M(128) M(256) M(512)

#define DECLARE(N) \
    using Next##N = void(*)(void*, std::uint64_t, V##N); \
    extern "C" NOINLINE V##N raw##N(const void*); \
    extern "C" NOINLINE Tagged<V##N> tagged##N(const void*, std::uint64_t); \
    extern "C" NOINLINE Checked<V##N> expected##N(const void*, bool); \
    extern "C" NOINLINE void cps##N(const void*, std::uint64_t, void*, Next##N); \
    extern "C" NOINLINE void sink##N(void*, std::uint64_t, V##N); \
    extern "C" NOINLINE void call_raw##N(const void*, Output*); \
    extern "C" NOINLINE std::uint64_t call_tagged##N(const void*, std::uint64_t, Output*); \
    extern "C" NOINLINE unsigned call_expected##N(const void*, bool, Output*); \
    extern "C" NOINLINE void call_cps##N(const void*, std::uint64_t, Output*); \
    extern "C" NOINLINE void inline_raw##N(const void*, Output*); \
    extern "C" NOINLINE void inline_expected_success##N(const void*, Output*); \
    extern "C" NOINLINE unsigned inline_expected_dynamic##N(const void*, bool, Output*);
EACH_WIDTH(DECLARE)
#undef DECLARE

extern "C" NOINLINE ScalarChecked expected_scalar(std::uint64_t, bool);
extern "C" NOINLINE std::uint64_t call_expected_scalar(std::uint64_t, bool);
extern "C" NOINLINE HVA256 raw_hva(const void*);
extern "C" NOINLINE void call_hva(const void*, Output*);
using NextChunks512 = void(*)(void*, std::uint64_t, V128, V128, V128, V128);
extern "C" NOINLINE void cps_chunks512(const void*, std::uint64_t, void*, NextChunks512);
extern "C" NOINLINE void sink_chunks512(void*, std::uint64_t, V128, V128, V128, V128);
extern "C" NOINLINE void call_cps_chunks512(const void*, std::uint64_t, Output*);

#if defined(__x86_64__)
#define DECLARE_REG(N) \
    extern "C" NOINLINE REGCALL Tagged<V##N> reg_tagged##N(const void*, std::uint64_t); \
    extern "C" NOINLINE REGCALL Checked<V##N> reg_expected##N(const void*, bool); \
    extern "C" NOINLINE std::uint64_t call_reg_tagged##N(const void*, std::uint64_t, Output*); \
    extern "C" NOINLINE unsigned call_reg_expected##N(const void*, bool, Output*);
EACH_WIDTH(DECLARE_REG)
#undef DECLARE_REG
extern "C" NOINLINE REGCALL UnionChecked<V128> reg_union128(const void*, bool);
extern "C" NOINLINE unsigned call_reg_union128(const void*, bool, Output*);
#endif
