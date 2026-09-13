# TuplePack validation

`ikea_validate` runs the Ikea modules, with these independently compiled TuplePack checks:

| Target | Evidence |
| --- | --- |
| `ikea_tuplepack_wire_check` | Independent per-bit description reference; widths/shifts/offsets, holes/duplicates, sparse/dense writes, eight contributions, descriptors, exact guard pages |
| `ikea_tuplepack_operations_check` | Whole-call rejection, shared-byte groups, nested writes, complete maintenance, sparse selections, 128-code construction, shared overlap proof |
| `ikea_tuplepack_execution_check` | General/normalized routing against independent bits, native CPS completion, 2/4-row batch shapes, inactive-null lanes |
| `ikea_tuplepack_packets_check` | All seven row shapes; independent buffered/native wire and group-order checks (including short maps and holes), tiny and scattered units, masks through bit 63, range tails, admission, effects and nested packet observations |
| `ikea_tuplepack_gpr_check` | All four GPR shapes; shared independent wire reference, every bounded word-transfer length/shift/width and mask at guard pages, failed admission, nested maintenance, erasure, CPS early completion and capacity-independent register bridges and grouped shared-byte observations |
| `ikea_tuplepack_ownership_check` | In-place retained storage, generation checks, completed frontiers, cancellation, actual substituted sources, exact/conservative/bypass summary publication |

Wire and ownership failures report the failing scenario/source location. The
wire reference derives expected bytes directly from physical descriptions rather
than production controls or the former spike implementation. Random cases use
fixed seeds. Source and pipeline metadata remain separate from native payloads.

Short teaching programs live in `examples/tuplepack`; exhaustive owner-event
checks remain here. Run the [header checker](../headers.py) under each configured
profile (`--module tuplepack` restricts it to this module) and use ASan/UBSan for
boundary changes. The [benchmark suite](../../../workbench/benchmarks/tuplepack/README.md)
has performance controls; these checks do not assert timings.
