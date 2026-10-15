#include "domain_advisory.h"
#include <cstdlib>
#include <cstdio>
namespace {
void check(bool ok) {if(!ok)std::abort();}
}
int main() {
    using namespace ht;
    // A full 8-bit set needs exactly32 bitmap bytes; strict equality provides
    // no allowance for any additional representation overhead.
    auto set=advise_dense_domain<8>(256,0,256);
    check(set.minimum_index_bytes==32 && set.remaining_overhead_budget==224);
    check(!advise_dense_domain<8>(256,0,32).worth_investigating());
    auto map=advise_dense_domain<8>(256,8,256*9);
    check(map.minimum_index_bytes==2080 && map.remaining_overhead_budget==224);
    // Deferred rank support can consume the entire apparent map saving.
    check(!advise_dense_domain<8>(256,8,2080).worth_investigating());
    check(!advise_dense_domain<16>(4,8,128).worth_investigating());
    auto wide=advise_dense_domain<64>(UINT64_MAX,64,UINT64_MAX);
    check(wide.bitmap_bytes==2305843009213693952ULL && !wide.minimum_index_bytes && !wide.worth_investigating());
    check(advise_dense_domain<64>(0,8,0).minimum_index_bytes==2305843009213693952ULL);
    std::puts("dense-domain bounds: strict saving, rank overhead budget, sparse rejection and overflow passed");
}
