# First Frame landing qualification

This is the bounded extraction check for `ikea::frame`, following the joint
[proposal](../PROPOSAL.md) and [reuse audit](../REUSE-AUDIT.md). The research
`proto/` read driver is retained as a regression control; the only consumer
implementation lands under `ikea/`. Mutation/private preparation remain research.

`frame_landing_measure` compares the public checked reader, the retained
runtime-width spike driver and the [qualified typed native controls](../controls/README.md).
Every arm receives the same original values and masks. The two Frame arms use
G16 grouping and zero exception slack, under the same resolved packing family.
The retained spike reads its fitted builder storage through trusted operations;
Ikea reads emitted, placed group images through the public checked API. The
Ikea/spike ratio therefore includes placement, binding representation and request
admission, as well as kernel changes. It is not a same-bytes kernel-only comparison.
The native arm uses typed FOR u8/u16/u32/u64 arrays and exact wide kernels,
not a scalar u64 loop. These are throughput/control comparisons, not memory
matched comparisons or a claim of optimal recipe selection.

The five cases are OS, UserID and URLHash with bound→pack, EventTime with
relative-ends16→bound→pack, and the existing generated monotone timestamps
with sizes16→bound→pack. Each uses the first 65,536 original rows (one binding
unit). SUM, COUNT>=median, dense period-3 selected SUM and 4096 materialized
independent point requests are checked against independent original-value
oracles. Point order and duplicates are retained; every output is checked.
Ikea points use repeated checked `get` calls; native uses one pre-admitted
batch gather. This prices the current consumer boundary, not just leaf latency.
Timers exclude fitting, placement/content validation and output checking.

Three arms rotate by process round. Repeated cells execute 32 queries per
sample; eviction-conditioned cells sweep a separate 256MiB volatile-readable
buffer before one query. Those conditions establish neither hot-cache residence
nor DRAM service. Results report nanoseconds per full query and request/row
counts explicitly. Frame image bytes exclude retained execution-state heap,
original inputs and oracle outputs. Native value bytes count the typed array,
excluding allocation overhead and execution state. Each arm's byte field is
named explicitly; these are not complete retained-memory accounts.
Private preparation/write timings are deliberately absent.

The worker script uses shared input artifacts outside collected results and
runs the maintained Frame/owner/examples/selection/Intervals/SeriesPack checks,
plus native-control conformance, before timing. A single immutable capture is
used for Zen5 AVX2/AVX-512 and Graviton4 NEON. Larger saturation workers and
additional recipe/format campaigns are outside this graduation run.
`SIXDB_FRAME_PROFILES=avx2` limits a retry to that profile; it does not change
the code, flags, checks or query trace for the profile.

Final receipts and any regression decisions belong in [GRADUATION.md](../GRADUATION.md).
