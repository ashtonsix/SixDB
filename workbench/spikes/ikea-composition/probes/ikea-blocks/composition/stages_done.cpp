#include "stage.h"

namespace ikea::composition {
IKEA_COMP_STAGE(ikea_comp_done) {
    return accumulator+fields;
}
}
