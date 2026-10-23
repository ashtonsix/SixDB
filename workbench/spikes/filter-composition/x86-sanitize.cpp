#include "ikea/test/filters/tile_support.h"
void check_local_equal();
int main() {
    series_case<sp::layout<4>,C::equal>();
    series_case<sp::layout<5>,C::equal>();
    series_case<sp::layout<6>,C::equal>();
    series_case<sp::layout<7>,C::equal>();
    series_case<sp::layout<14>,C::equal>();
    series_case<sp::layout<30>,C::equal>();
    series_case<sp::layout<31>,C::equal>();
    series_case<sp::layout<62>,C::equal>();
    series_case<sp::layout<64>,C::equal>();
    series_case<sp::layout<30,sp::geometry::local,8>,C::equal>();
    series_case<sp::layout<14,sp::geometry::striped>,C::equal>();
    check_local_equal();
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* p=static_cast<std::uint8_t*>(mmap(nullptr,page*2,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(p!=MAP_FAILED); assert(mprotect(p,page,PROT_READ|PROT_WRITE)==0);
    tile_boundary<4>(p+page); tile_boundary<5>(p+page); tile_boundary<6>(p+page); tile_boundary<7>(p+page);
    tile_boundary<30>(p+page); tile_boundary<31>(p+page); tile_boundary<62>(p+page); tile_boundary<64>(p+page);
    assert(munmap(p,page*2)==0);
    puts("x86 graduation ASan/UBSan: indexof windows/activity/domain, Local/Striped/heads, exact page ends and all prepared widths passed");
}
