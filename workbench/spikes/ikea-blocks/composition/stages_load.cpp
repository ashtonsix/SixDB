#include "stage.h"

namespace ikea::composition {
IKEA_COMP_STAGE(ikea_comp_load) {
    const auto loaded=load_native(tile);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,accumulator,fields,
                                             IKEA_COMP_BITS_UNPACK(loaded));
}
}
