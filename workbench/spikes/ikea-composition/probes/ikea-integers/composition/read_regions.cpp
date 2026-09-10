#include "transport.h"

namespace ikea::integers::composition {
I12_DECLARE(ikea_i12_read_local) {
    NativeOps<Local4,ParentKind::packets8> ops{tile};
    const auto decoded=read12(ops,Payload12<Local4>{},group);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,group,cutoff,acc,mask,I12_VALUES_EXPAND(decoded));
}
I12_DECLARE(ikea_i12_read_scan) {
    NativeOps<Scan4,ParentKind::body64_tail32> ops{tile};
    const auto decoded=read12(ops,Payload12<Scan4>{},group);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,group,cutoff,acc,mask,I12_VALUES_EXPAND(decoded));
}
I12_DECLARE(ikea_i12_read_scan_middle) {
    NativeOps<Scan4,ParentKind::body32_tail32_body32> ops{tile};
    const auto decoded=read12(ops,Payload12<Scan4>{},group);
    [[clang::musttail]] return cursor->execute(cursor+1,tile,group,cutoff,acc,mask,I12_VALUES_EXPAND(decoded));
}
} // namespace ikea::integers::composition
