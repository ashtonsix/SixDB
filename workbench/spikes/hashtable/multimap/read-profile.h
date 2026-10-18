#pragma once
#include <ikea/hashtable/encoding-selection.h>
#include "measured-profiles.h"

namespace write_arch {
template<unsigned K,unsigned V,bool Multi=false>
consteval ikea::hashtable::encoding_choice read_profile() {
#if SIXDB_TUNE_ZEN5
    constexpr auto cpu="zen5";
#elif SIXDB_TUNE_GRANITE_RAPIDS
    constexpr auto cpu="granite-rapids";
#elif SIXDB_TUNE_NEOVERSE_V2
    constexpr auto cpu="neoverse-v2";
#else
    static_assert(K>64,"select a measured CPU profile for the write integration");
    constexpr auto cpu="unknown";
#endif
#if defined(__AVX512VBMI__) && defined(__GFNI__)
    constexpr auto isa="avx512";
#elif defined(__AVX2__) && defined(__GFNI__)
    constexpr auto isa="avx2-gfni";
#elif defined(__AVX2__)
    constexpr auto isa="avx2";
#elif defined(__aarch64__)
    constexpr auto isa="neon";
#else
    constexpr auto isa="unknown";
#endif
    constexpr auto choice=[] {
        if constexpr(Multi)return multimap_profiles::measured(K,V,cpu,isa);
        else return ikea::hashtable::measured_encoding(K,V,cpu,isa);
    }();
    static_assert(choice.has_value(),"no measured reader profile for this target/surface");
    return *choice;
}
}
