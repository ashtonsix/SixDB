#include "stage.h"
#include <string_view>

namespace ikea::composition {
static_assert(std::string_view(FixedModel::id)==bec_predictor::model_id);
IKEA_COMP_STAGE(ikea_comp_model) {
    const auto predicted=bec_predictor::predict_size(fields);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,accumulator,predicted,
                                             IKEA_COMP_BITS_FORWARD);
}
IKEA_COMP_STAGE(ikea_comp_transition_model_stage) {
    const auto predicted=bec_predictor::predict_size_transitions(fields);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,accumulator,predicted,
                                             IKEA_COMP_BITS_FORWARD);
}
}
