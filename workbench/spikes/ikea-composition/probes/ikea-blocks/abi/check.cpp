#include "probe.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

template<class T> void layout(const char* name) {
    std::printf("%s,%zu,%zu,%d,%d,%d,%d\n", name, sizeof(T), alignof(T),
                std::is_trivially_copyable_v<T>, std::is_trivially_destructible_v<T>,
                std::is_trivially_copy_constructible_v<T>, std::is_trivially_move_constructible_v<T>);
}

int main() {
    std::puts("type,size,alignment,trivially_copyable,trivially_destructible,trivial_copy_ctor,trivial_move_ctor");
#define LAYOUT(N) layout<V##N>("V" #N); layout<Tagged<V##N>>("Tagged" #N); layout<Checked<V##N>>("Expected" #N);
    EACH_WIDTH(LAYOUT)
#undef LAYOUT
    layout<ScalarChecked>("ExpectedScalar");
    layout<HVA256>("HVA256");
    layout<UnionChecked<V128>>("Union128");
    std::printf("libstdcxx_release,%d\nlibstdcxx_date,%d\n", _GLIBCXX_RELEASE, __GLIBCXX__);

    std::array<std::uint8_t, 64> input;
    for (unsigned i = 0; i < input.size(); ++i) input[i] = (i * 71) ^ 0xab;
    Output out{};
#define CLEAR() std::memset(&out, 0, sizeof(out))
#define VERIFY(N) \
    CLEAR(); call_raw##N(input.data(), &out); assert(std::memcmp(input.data(), out.bits, N/8) == 0); \
    CLEAR(); assert(call_tagged##N(input.data(), 5, &out) == 8); assert(std::memcmp(input.data(), out.bits, N/8) == 0); \
    CLEAR(); assert(call_expected##N(input.data(), true, &out) == 0); assert(std::memcmp(input.data(), out.bits, N/8) == 0); \
    assert(call_expected##N(nullptr, false, &out) == 3); \
    CLEAR(); call_cps##N(input.data(), 11, &out); assert(out.tag == 14); assert(std::memcmp(input.data(), out.bits, N/8) == 0); \
    CLEAR(); inline_raw##N(input.data(), &out); assert(std::memcmp(input.data(), out.bits, N/8) == 0); \
    CLEAR(); inline_expected_success##N(input.data(), &out); assert(std::memcmp(input.data(), out.bits, N/8) == 0); \
    CLEAR(); assert(inline_expected_dynamic##N(input.data(), true, &out) == 0); assert(std::memcmp(input.data(), out.bits, N/8) == 0); \
    assert(inline_expected_dynamic##N(nullptr, false, &out) == 3);
    EACH_WIDTH(VERIFY)
#undef VERIFY
    assert(call_expected_scalar(10, true) == 13);
    assert(call_expected_scalar(10, false) == 3);
    CLEAR(); call_hva(input.data(), &out); assert(std::memcmp(input.data(), out.bits, 32) == 0);
    CLEAR(); call_cps_chunks512(input.data(), 19, &out); assert(out.tag == 22);
    assert(std::memcmp(input.data(), out.bits, 64) == 0);
#if defined(__x86_64__)
#define VERIFY_REG(N) \
    CLEAR(); assert(call_reg_tagged##N(input.data(), 8, &out) == 11); assert(std::memcmp(input.data(), out.bits, N/8) == 0); \
    CLEAR(); assert(call_reg_expected##N(input.data(), true, &out) == 0); assert(std::memcmp(input.data(), out.bits, N/8) == 0); \
    assert(call_reg_expected##N(nullptr, false, &out) == 3);
    EACH_WIDTH(VERIFY_REG)
#undef VERIFY_REG
    CLEAR(); assert(call_reg_union128(input.data(), true, &out) == 0);
    assert(std::memcmp(input.data(), out.bits, 16) == 0);
    assert(call_reg_union128(nullptr, false, &out) == 3);
#endif
    std::puts("status,pass");
}
