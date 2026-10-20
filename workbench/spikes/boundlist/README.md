# Boundlist

Boundlist describes ordered subdivisions and maps points or ranges to ranks.
It shares reservation geometry with StreamPack, so the same search machinery
can also route through ordered keyspace partitions. StreamPack owns its used
lengths and value compaction.
An equal pair of bounds need not mean an empty keyspace segment: population is
an independent fact.

Boundlist is implemented in Ikea. The study and graduation ran on
19–20 October 2026. The
[implementation report](GRADUATION.md) records the source/phase seams and measured
algorithm choices; [usage](../../../ikea/docs/boundlist/usage.md) teaches the API.
The prototype algorithm menu and flattened adapter remain research controls.
The [geometry/value ownership follow-up](OWNERSHIP.md) supersedes the original
U-bearing sidecar boundary; generic ordered search is unchanged.

| Read next | Purpose |
| --- | --- |
| [Ownership follow-up](OWNERSHIP.md) | Current geometry/value boundary and codec comparison |
| [Implementation](GRADUATION.md) | Production seams, qualification and selected defaults |
| [Recommendation](RECOMMENDATION.md) | Historical prototype selection before implementation |
| [Findings](FINDINGS.md) | Absolute latency, reciprocal throughput and memory; correctness and measurement limits |
| [Production scorecard](evidence/graduation/scorecard.md) | Absolute latency, reciprocal throughput, setup, memory and counterexamples |
| [Extraction parity](evidence/parity/README.md) | Frozen old/current readers, mutations and actual packing, with final cleanup qualification |
| [Production evidence](evidence/graduation/README.md) | Selected matrices, source/host identities and raw recovery |
| [Prototype evidence](evidence/native/README.md) | Earlier candidate comparisons and diagnosis |
| [Original plan](PLAN.md) | Questions and candidate space explored; a research plan, not an implementation contract |
| [Working state](STATUS.md) | Completion evidence and work belonging to implementation or other owners |

Engine owns complete directory policy. This spike supplies
leaf search, extraction, read-grant and source-effect findings; it does not
choose Engine's tree layout or implement a new B+ tree.

## Code and reproduction

`boundlist.h`, `geometry.h` and `metadata.h` are candidate bodies and compatibility
adapters. `fused.h`, `scan-count.h` and `wide-control.h` are independent controls.
`check.cpp` uses logical oracles; `material.cpp` exercises actual Loom allocations
and Orbital retained-reader publication. `bench.cpp`, `search.cpp`, `packed.cpp`,
`ranges.cpp`, `reads.cpp` and `cold.cpp` measure distinct complete consumers.

Use the pinned [Linux build](../../../BUILDING.md), prefixing commands with
`orb -m ubuntu` from macOS. Configure with `-DSIXDB_SPIKES=boundlist`, then build
`boundlist_check` and `boundlist_material` for semantic and lifetime checks.
Each benchmark accepts positional arguments documented at its `main`.

The native runners keep loop/SLP vectorization disabled and audit linked
consumers: `search-native.sh` covers isolated/search-envelope diagnosis,
`qualification-native.sh` covers range and extraction consumers,
`final-native.sh` covers complete-cycle readers and geometry, and
`cold-native.sh` covers the final long-trace Loom/manual comparison.
Their worker configurations select Zen 5, Granite Rapids and Neoverse V2.
`evidence-inputs.json` and `consolidate.py` specify the retained comparisons.

`graduation-native.sh` qualifies production search/reverse, shared mutation laws,
complete readers and prepared Loom phases against frozen pre-change bodies and
independent fused controls. Production tests/examples live under `ikea/`;
`promotion.cpp` and `mutation.cpp` retain the before/after complete consumers.

`binding-native.sh` qualifies numeric preparation and regular/compact owner frames;
`adoption-native.sh` checks the final kernels, upper-bound duplicates and typed
sidecar phases. `graduation-inputs.json` selects the production cohorts.
`consolidate.py`, `supplements.py` and `scorecard.py` rebuild compact matrices,
preparation/call accounting and readable slices. Use the recorded worker archives
for the exact executed sources; the initial binding capture used the runner name
`final-native.sh`, now restored to its earlier prototype purpose.

`parity-native.sh` closes the direct pre-extraction reader/packing comparison:
`legacy-read.h` and `legacy-write.h` contain actual frozen access/writer bodies,
checked against their origin by `control-audit.py`. `reads.cpp`, `mutation.cpp`
and `packing.cpp` include complete consumers and source-qualified effects.
Packing restores a fragmented image outside each timed window; its independent
block stream is distinct from per-slot reciprocal throughput. `parity-inputs.json`
selects this follow-up cohort, keeping old/current/fused readers and old/current
writers beside the earlier search and Loom evidence.

`packing-native.sh` supplies the final packing follow-up: fixed journal placement,
immutable count capture and seven repetitions. The first packing result remains
in the [parity export](evidence/parity/README.md), together with the reader codegen
comparison and all individual losing cells.

`ownership-native.sh` and `ownership-workers.json` qualify the geometry/value
ownership split against recovered old public headers/TUs, using `u-control.py`.
`encoding.cpp` compares sizes, group-relative ends and Direct, including redundant
checkpoints, explicit SIMD maintenance and stronger inverse controls. The current
[ownership assessment](OWNERSHIP.md) records decisions, absolute results and recovery.
