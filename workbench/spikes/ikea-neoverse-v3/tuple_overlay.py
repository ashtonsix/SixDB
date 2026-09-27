"""Generate a build-private TuplePack TBL decomposition; maintained sources stay untouched."""
from pathlib import Path
import sys
root, output = map(Path, sys.argv[1:])
helper = output / 'v3_tuple_lookup.h'
helper.parent.mkdir(parents=True, exist_ok=True)
helper.write_text('''#pragma once
#include <arm_neon.h>
namespace v3_tuple_spike {
[[gnu::always_inline]] inline uint8x16_t tbl3(uint8x16x3_t table, uint8x16_t index) {
    return vorrq_u8(vqtbl2q_u8({{table.val[0], table.val[1]}}, index),
                   vqtbl1q_u8(table.val[2], vsubq_u8(index, vdupq_n_u8(32))));
}
[[gnu::always_inline]] inline uint8x16_t tbl4(uint8x16x4_t table, uint8x16_t index) {
    return vorrq_u8(vqtbl2q_u8({{table.val[0], table.val[1]}}, index),
                   vqtbl2q_u8({{table.val[2], table.val[3]}}, vsubq_u8(index, vdupq_n_u8(32))));
}
}
''')
for name in ('shuffle.h', 'routes.h'):
    relative = Path('ikea/tuplepack/detail/native') / name
    text = (root / 'ikea/include' / relative).read_text()
    text = text.replace('#pragma once', '#pragma once\n#include <v3_tuple_lookup.h>')
    text = text.replace('vqtbl3q_u8(', '::v3_tuple_spike::tbl3(')
    text = text.replace('vqtbl4q_u8(', '::v3_tuple_spike::tbl4(')
    destination = output / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text)
