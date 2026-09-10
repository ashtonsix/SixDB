#include "stage.h"

namespace ikea::composition {
IKEA_COMP_STAGE(ikea_comp_features) {
    const auto extracted=native_features(IKEA_COMP_BITS_VALUE);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,accumulator,extracted,
                                             IKEA_COMP_BITS_FORWARD);
}
IKEA_COMP_STAGE(ikea_comp_load_features) {
    const auto loaded=load_native(tile);
    const auto extracted=native_features(loaded);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,accumulator,extracted,
                                             IKEA_COMP_BITS_UNPACK(loaded));
}
IKEA_COMP_STAGE(ikea_comp_transition_features_stage) {
    const auto extracted=native_features_impl<true,false>(IKEA_COMP_BITS_VALUE);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,accumulator,extracted,
                                             IKEA_COMP_BITS_FORWARD);
}
IKEA_COMP_STAGE(ikea_comp_load_transition_features_stage) {
    const auto loaded=load_native(tile);
    const auto extracted=native_features_impl<true,false>(loaded);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,accumulator,extracted,
                                             IKEA_COMP_BITS_UNPACK(loaded));
}
}
