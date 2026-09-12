// Compile-only probe: keep producer and consumer opaque to one another so that
// the ABI, rather than successful inlining, determines the handoff.
using u8 = unsigned char;
using u64 = unsigned long long;
using v16 = u8 __attribute__((vector_size(16)));
using v32 = u8 __attribute__((vector_size(32)));
using v64 = u8 __attribute__((vector_size(64)));

#define NOINLINE __attribute__((noinline))

extern "C" NOINLINE u64 scalar8(const u8* p) {
    u64 value;
    __builtin_memcpy(&value, p, 8);
    return value;
}

#if defined(__x86_64__)
struct pair32 { v32 a, b; };
extern "C" NOINLINE pair32 sysv_pair(const u8* p) {
    pair32 value;
    __builtin_memcpy(&value, p, 64);
    return value;
}
extern "C" NOINLINE v32 sysv_single32(const u8* p) {
    v32 value;
    __builtin_memcpy(&value, p, 32);
    return value;
}
extern "C" NOINLINE __attribute__((regcall)) pair32 regcall_pair(const u8* p) {
    pair32 value;
    __builtin_memcpy(&value, p, 64);
    return value;
}
extern "C" void take_pair(v32, v32);
extern "C" NOINLINE void flattened_handoff(const u8* p) {
    v32 a, b;
    __builtin_memcpy(&a, p, 32);
    __builtin_memcpy(&b, p + 32, 32);
    take_pair(a, b);
}
using sysv_reader = pair32 (*)(const u8*);
using regcall_reader = pair32 (__attribute__((regcall)) *)(const u8*);
extern "C" NOINLINE void consume_sysv(sysv_reader f, const u8* p) {
    auto v = f(p);
    take_pair(v.a, v.b);
}
extern "C" NOINLINE void consume_regcall(regcall_reader f, const u8* p) {
    auto v = f(p);
    take_pair(v.a, v.b);
}
#if defined(__AVX512F__)
extern "C" NOINLINE v64 sysv_single64(const u8* p) {
    v64 value;
    __builtin_memcpy(&value, p, 64);
    return value;
}
#endif
#else
struct quad16 { v16 a, b, c, d; };
extern "C" NOINLINE quad16 aapcs_quad(const u8* p) {
    quad16 value;
    __builtin_memcpy(&value, p, 64);
    return value;
}
extern "C" void take_quad(quad16);
using aapcs_reader = quad16 (*)(const u8*);
extern "C" NOINLINE void consume_aapcs(aapcs_reader f, const u8* p) {
    take_quad(f(p));
}
#endif
