#include "transport.h"

namespace ikea::integers::composition {
I12_DECLARE(ikea_i12_filter) {
    mask=filter_native(I12_VALUES,cutoff);
    [[clang::musttail]] return cursor->execute(I12_STAGE_FORWARD);
}
} // namespace ikea::integers::composition
