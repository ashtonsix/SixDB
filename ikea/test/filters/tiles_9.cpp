#include "tile_support.h"
void check_tile_widths_9(std::uint8_t* end) {
    tile_boundary<9>(end);
    tile_boundary<10>(end);
    tile_boundary<11>(end);
    tile_boundary<12>(end);
    tile_boundary<13>(end);
    tile_boundary<14>(end);
    tile_boundary<15>(end);
    tile_boundary<16>(end);
}
