#define FILTER_X86_NO_MAIN
#include "x86-index.cpp"
extern "C" {
[[gnu::noinline]] unsigned index30_public(const std::uint8_t* p,std::uint64_t q,std::uint64_t a) {
    return x86_index::lookup<30,16,0>(p,q,a);
}
[[gnu::noinline]] unsigned index30_fused_first(const std::uint8_t* p,std::uint64_t q,std::uint64_t a) {
    return x86_index::lookup<30,16,7>(p,q,a);
}
[[gnu::noinline]] unsigned index30_fused_full(const std::uint8_t* p,std::uint64_t q,std::uint64_t a) {
    return x86_index::lookup<30,16,8>(p,q,a);
}
[[gnu::noinline]] unsigned index30_prepared_first(const std::uint8_t* p,std::uint64_t q,std::uint64_t a) {
    return x86_index::lookup<30,16,4>(p,q,a);
}
[[gnu::noinline]] unsigned index30_prepared_full(const std::uint8_t* p,std::uint64_t q,std::uint64_t a) {
    return x86_index::lookup<30,16,6>(p,q,a);
}
[[gnu::noinline]] unsigned equal6_bounded(const std::uint8_t* p,std::uint64_t q) {
    return ikea::seriespack::author::local_equal8<6>(q)(x86_index::exact_load<6>(p));
}
[[gnu::noinline]] unsigned equal6_owner(const std::uint8_t* p,std::uint64_t q) {
    std::uint64_t raw; __builtin_memcpy(&raw,p,8);
    return ikea::seriespack::author::local_equal8<6>(q)(raw);
}
}
