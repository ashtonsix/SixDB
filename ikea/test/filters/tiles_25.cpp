#include "tile_support.h"
void check_tile_widths_25(std::uint8_t* end) {
    tile_boundary<25>(end);
    tile_boundary<26>(end);
    tile_boundary<27>(end);
    tile_boundary<28>(end);
    tile_boundary<29>(end);
    tile_boundary<30>(end);
    tile_boundary<31>(end);
    tile_boundary<32>(end);
}
