# TuplePack packet composition

TuplePack supports every power-of-two shape within a 64-byte decoded packet:
64 bytes × 1 row through 1 byte × 64 rows. The ordinary and native reader/writer
surfaces use the same row count. One row retains its specialized kernels.
See the [readiness review](../readiness-review.md) for the wider audit and the
[packet example](../../../../ikea/examples/tuplepack/packets.cpp) for executable
inline/CPS updates with full, sparse and tail packets.

## What controls the transfer choice

The decoded shape, physical tuple extent, selected byte demand and stride are
independent. A packet with eight output bytes per row might read one physical
byte, a contiguous four-byte window, or four bytes spread across a 64-byte tuple.
The useful transfer boundary consequently cannot be a single tuple-size cutoff.

| Physical situation | Retained implementation |
| --- | --- |
| Full packet, tightly adjacent power-of-two physical windows | Coalesce into bounded word/vector loads and stores |
| Full packet of small complete units with awkward short windows | Read the complete units together and route once; no stride padding is included |
| Compact windows in strided tuples | Bounded per-row word/vector transfers |
| Small scattered demand | Assemble the selected bytes into words, then route the packet |
| Larger scattered projections with only two/four rows | Reuse the point lowering for each row within the native packet body |
| Sparse packet or short tail | Visit only active rows; AVX-512 can use masked transfers for tight windows |

Preparation chooses a physical grain from the bytes transferred, independently
of the decoded bytes per row. The two/four-row fallback applies above eight
scattered physical bytes; smaller projections do not pay for a wide point
decoder. This is a bounded implementation choice tested across the measured
matrix, not a universal microarchitectural threshold.

A later density threshold reduced work for a few sparse updates but discarded
cheap vector windows and complete-unit routes elsewhere. It is
[rejected with comparative evidence](measurements.md#rejected-density-threshold);
the retained policy does not equate fewer accessed bytes with fewer instructions.

The progression through small tuples is not monotonic. Three- and six-byte
transfers can cost more instructions than four- and eight-byte transfers. Tight
complete-unit reads address that case. Reading through arbitrary stride padding
also won some experiments, but would widen the ordinary access contract beyond
the tuple. That technique remains an experimental comparator.

## Composition obligations

The packet survives in registers through decoding, transforms and encoding.
Binding, admission, row selection, effects and maintenance belong to the shell;
there is no per-row erased dispatch. Mask bit `r` means original row `first+r`,
including for one-byte tuples. Groups and erased operations retain their shape.
Whole-call rejection leaves payload, journal and maintenance observations intact.

Physical byte effects describe issued stores, including preserved bytes reissued
by a compact transfer. They are attributed to the actual substituted owner.
Native batch observers see a complete group before or after mutation; adapting
a point observer also preserves that ordering. Owners still decide admission,
lifetime, suspension and publication.

An ordinary checked mutation does more work than a trusted body. In particular,
an alternating mask across one-byte tuples can require a separate journal span
for each selected row even when the payload uses one masked vector store. The
benchmarks retain body, checked inline and checked CPS modes so this cost remains
visible. Tiny bodies also expose continuation-call overhead; the owner chooses
stage granularity rather than assuming each primitive deserves a CPS boundary.

## Evidence and limits

[Measurements](measurements.md) gives the selected comparisons and remaining
tradeoffs. [crossover.cpp](crossover.cpp) varies tuple extents 1, 2, 3, 4, 6, 8,
12, 16, 24, 32 and 64; one through sixteen drawn bytes; compact/spread maps;
tight, extent+1 and 64-byte strides; and two/four/eight/sixteen/64-row shapes.
It checks each retained competitor against independent bits before timing sum
and read/update consumers. The point control uses the prepared scalar operation
for projections of at most eight bytes and native point operations above that.
`gather` means bounded per-row assembly here, not a hardware-gather-instruction
comparison. `window` explicitly owns the surrounding allocation and may read
padding that the ordinary operation may not read.

The [maintained benchmark](../../../benchmarks/tuplepack/README.md) covers all
seven shapes, full/alternating masks, one-byte and 64-byte units, and native
body/ordinary/CPS execution. Both studies use repeated warm scans over 1,024
original rows, with sequential pinned repetitions. Results are per original row,
including inactive rows, and do not establish cold-memory, mixed-workload or
database-level performance. Layout search and sensing remain Engine research.

Run the current crossover and module checks on a captured worker:

```sh
python3 workbench/tools/worker.py run workbench/spikes/tuple-layout/batching/candidate.sh \
  --machine neoverse-v2 --env TUPLE_PROFILES=neon
python3 workbench/tools/worker.py run workbench/spikes/tuple-layout/batching/candidate.sh \
  --machine zen5 --env 'TUPLE_PROFILES=avx2 avx512'
```

`TUPLE_CROSS_FILTER` selects crossover names; `TUPLE_MIN_TIME` and
`TUPLE_REPETITIONS` set duration and repetitions. `summarize.py` selects the
families behind the findings, retaining every competitor and repetition for
those families, and records full-matrix shape counts and exceptions in
`coverage.json`. Choose a new output directory when regenerating a selection.
The `density` selection takes `--baseline JOB_DIRECTORY` to retain both sides of
the rejected density experiment, with separate recovery references.
Full sweeps, binaries, source snapshots and logs remain in the
referenced bundles under the [artifact recovery contract](../../../tools/artifacts.md).

## Retired prototype

The first byte-at-a-time gather and an accidentally outlined aggregate handoff
were useful failures. Their selected evidence remains under `evidence/gather-*`
and `evidence/outlined-zen`. The intermediate small-tuple/short-transfer evidence
is under `evidence/before-*`. Early prototype point controls did not select the
strongest small-map point kernels, so they support before/after transfer
diagnosis, not final packet-versus-point claims.

The duplicate prototype implementation, checker and runner are removed from the
active source tree. Their exact last bytes were checked against
[this archived source](evidence/retired-prototype/artifact.json), with member
hashes in [source-members.json](evidence/retired-prototype/source-members.json).
Recover `source.tar.gz` from that bundle to obtain the original build files and
`batching/run.sh` together. The current crossover uses the production packet
checker and implementation.
