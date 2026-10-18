#include "tile_support.h"
void check_tile_widths_1(std::uint8_t* end) {
    tile_boundary<1>(end);
    tile_boundary<2>(end);
    tile_boundary<3>(end);
    tile_boundary<4>(end);
    tile_boundary<5>(end);
    tile_boundary<6>(end);
    tile_boundary<7>(end);
    tile_boundary<8>(end);
}
