#include "tile_support.h"
void check_tile_widths_33(std::uint8_t* end) {
    tile_boundary<33>(end);
    tile_boundary<34>(end);
    tile_boundary<35>(end);
    tile_boundary<36>(end);
    tile_boundary<37>(end);
    tile_boundary<38>(end);
    tile_boundary<39>(end);
    tile_boundary<40>(end);
}
