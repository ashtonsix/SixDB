# Hashtable: reference experiments

These benchmark controls are frozen. The selected hierarchy lives in the
[parent directory](../README.md), following the
[reflection on what to do differently](REASSESSMENT.md). The successor carries
the original brief and separates rejected recipes from untested ideas. This
prototype did not meet the broad performance target; it is not a proposed
production format.

[RESULTS.md](RESULTS.md) owns measurements, limits and recovery references.
[DIRECTION.md](DIRECTION.md) is the historical design record. Keep the code as
comparators and recovery material; do not expand its implementation-mode matrix
or transplant it as the successor's starting architecture.

Useful retained findings include:

- Historical short-remainder wins and several genuine single-bucket map regimes.
- Substantially reduced primitive transfer overhead through admitted physical
  windows, with remaining address, routing and ownership costs kept distinct.
- Complete cold Loom throughput wins on Zen5, alongside fresh V2 and hot serial
  losses. The large map's few chained keys do not establish chain-heavy behavior.
- Construction, churn, capacity and lifetime effects that change the comparison.
- Real single-bucket and bounded multi-bucket ObjectStore publication, retained
  readers, growth, reset/refill and refusal checks.
- Rejected byte-plane/native-word exact-search remedies in their measured shapes;
  neither failure rejects every exact-remainder family or the original brief.

The final bounded online-growth capture completed on V2 and Granite AVX2/AVX-512.
N16's charged mutation/publication lifetime is near parity or modestly faster,
while multi-bucket hot reads still lose. Shared preparation and ownership temper
its requested-payload saving. The two Zen jobs produced no measurements because
Spot capacity was unavailable. The later local point-dispatch edit passes checks
and removes an outlined ARM primary-path call, but has no native timing claim.

| Reference consumer | Evidence it provides |
| --- | --- |
| `prior_compare.cpp`, [historical quotient map](prior_art/README.md) | Reproduction with strong baselines and dependent requests |
| `single_bucket.h`, `single_lifecycle.cpp` | List regime, shared preparation and actual Orbital publication |
| `table_compare.cpp` | Multi-bucket mutation, StreamPack overflow, aging and retained copies |
| `table_ring.cpp` | Fused/manual/actual Loom execution through every tier; ordinary Abseil prefetch |
| `table_values.cpp` | Immediate wider-value consumers and update recipes |
| `growth.cpp` | Two online geometry changes, actual publication and complete bounded lifetime |
| `layout.py`, `layout_check.cpp`, `table_emit.py` | Structural grammar and physical admission; not a general measured layout selection |

`baseline.cmake` pins ordinary Abseil. Exact-byte keys provide additional controls
where native integer padding would flatter packed storage. `report.py` and
`growth-report.py` validate retained measurements. Worker archives hold captured
sources, binaries, assembly and compiler/resource receipts. The unfinished
family-wide scope is recorded in the selected hierarchy's graduation assessment.
