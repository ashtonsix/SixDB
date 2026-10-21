# Boundlist geometry API cleanup

Boundlist now has one interval geometry contract. Borrowed cuts, projected ends
and compact encoded views supply an interval count, an origin and an exclusive
end per rank. This removes the distinction between a general search source and a
specially composed “sidecar” for ordinary geometry operations.

The public [reference](../../../ikea/docs/boundlist/reference.md) owns exact
contracts; [usage](../../../ikea/docs/boundlist/usage.md) teaches the interface.
The three compact encodings, field-width proofs, 256-interval capacity and
portable bytes remain unchanged.

## Decisions

- `from_cuts(span)` borrows N+1 cuts and exposes N interval ranks.
  `from_ends(projection,origin)` adds an origin to an existing endpoint source.
  Neither requires a compact schema, payload, arithmetic distance or retirement.
- `locate` returns optional coordinate bounds `{first,limit}`. `reverse` returns
  a containing rank or the interval count. `overlap` returns a half-open rank
  window. `range` preflights the complete rank interval before callbacks.
- Raw geometry includes every assigned rank. `selected_source` explicitly
  attaches a `rank_selection`; compact `live_ranks()` exposes an optional
  retirement companion. An excluded point owner returns absent, never a later
  eligible interval. Internal holes can remain in an overlap window.
- Endpoint lower/upper searches remain smaller reusable operations. The paired
  occupied-minimum/maximum convenience helper and `query_range` are removed.
  Engine already owns that policy locally, with no call-site dependency on the
  removed helper or namespace.
- Native geometry packets expose assigned/selected `active` lanes. StreamPack
  supplies its own used-length and retirement policy above that decode. Its
  existing value results and mutation semantics remain unchanged.

Compact geometry stays useful without restricting other representations.
Borrowed sources have unsigned local counts up to UINT_MAX and arbitrary ordered
coordinates; custom wide keys can represent fences beyond a native integer's
maximum. The compact view has at most 256 intervals, implicit zero origin and
per-interval sizes at most 65535. Fixed size, zero geometry and immortal omissions
remain independent schema facts.

## Performance qualification

The retained [absolute scorecard](evidence/api/scorecard.md) separates dependent
C1 latency from reciprocal C8 throughput. Its [complete matrix](evidence/api/summary.csv)
keeps all selected sizes, repetitions, methods and backing-byte counts. Native
AVX2/AVX-512 run on Zen 5 and Granite Rapids; NEON runs on Neoverse V2. Both compiler
vectorizers are disabled. Query preparation and dependent payload consumption
are inside the timed operations.

`portfolio.cpp` compares complete existing StreamPack value/reverse consumers
with the pre-cleanup implementation and independent native fused controls. New
cases use raw/selected Boundlist point ownership and selected overlap before the
same value consumption. `api-geometry.cpp` compares borrowed geometry with the
equivalent old consumer-owned composition and independent SIMD controls, including
both overlap boundaries and a payload-dependent next request. Point/range before
controls include equivalent domain checks. The borrowed locate control starts
with an admitted rank; the new ordinary locate also checks that rank. Its timing
therefore includes any surviving checked-call cost.

Three pool sizes (1, 64 and 4096 blocks) exercise different working sets. They are
allocation/trace descriptions, not universal L1/L2/LLC/DRAM classifications. This
cleanup does not replace the existing actual Loom portfolio qualification or
claim a new DRAM uplift. Memory remains encoded bytes, granted metadata/payload
backing and view size; timing is ns/request.

### Observed cost

Existing StreamPack operations have pooled after/before medians between 0.9988 and
1.0050 across the five native profiles. This is not a per-cell parity claim:
individual large-pool samples vary materially, and the full matrix retains those
counterexamples. No encoding or backing-byte count changes. In the Zen large-pool AVX-512
counterexample, ordinary Sizes reverse/value is 79.52→89.60 ns; the pooled median
must not conceal that row.

For the default Sizes geometry, a selected point followed by value consumption
in one block of 256 intervals measures:

| Profile | Before latency | After latency | Before throughput | After throughput |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX-512 | 23.88ns | 23.71ns | 7.49ns/request | 7.47ns/request |
| Granite AVX-512 | 26.63ns | 26.78ns | 10.42ns/request | 10.28ns/request |
| Neoverse V2 NEON | 36.02ns | 35.44ns | 14.35ns/request | 14.31ns/request |

The narrow remaining counterexample is NEON Absolute Ends selected overlap:
42.72→47.04ns dependent, 38.38→41.13ns/request independent. It stays visible;
this cleanup does not claim every checked composition is free. It does not earn
another public mode or a layout/default change. Low-level admitted geometry remains
available for consumers that have already established their bounds.

Each default example has 1126 encoded metadata bytes, 1216 bytes of granted metadata
backing and 6016 payload bytes; a StreamPack view is 72 bytes. Those are unchanged.

### Costs found and removed

The first explicit selector used “find next eligible” to test a single rank.
That was correct but needlessly loaded/scanned a word window. Point membership
now tests one bit; endpoint search keeps its next/previous scans. Constant all/none
selectors finish without scanning rank words.

Linked complete-consumer inspection also found an outlined geometry lambda,
which forced argument frames and spills in the eight-request loop. The ready
geometry callable boundaries now carry explicit inline attributes. The final
local linked consumer contains no calls to an outlined reverse/overlap geometry
lambda. `api-codegen.py` makes before/after instruction-shape comparisons
repeatable; its normalized text and opcode counts are diagnostic, not machine-byte
or cycle-equivalence proofs.

## Correctness and composition

Independent literal-cut oracles cover all three encodings, every four-rank
combination of sizes 0/1/2, all retirement masks, signed/wider queries, clipped and
reversed ranges, exact cuts, zero-size ranks, explicit selection and refusal
before callbacks. Additional cases cover near-UINT64_MAX origins, nonnumeric
ordered keys, nested selectors and more than 256 intervals. Native checks
separate assigned geometry masks from selected masks.

Existing checks retain 41,472 independent boundary/resize cases, exact issued-store
effect replay, descriptor compatibility and partial-group admission. StreamPack
checks retain value, used-length, mutation and packing contracts. Actual
Loom/Orbital histories cover retained old/fresh/late readers, shared payload
prefixes, tail reclamation, transfer and rank repair. The six-test sanitizer run
passes with ASan/UBSan and leak detection; ordinary/author headers compile
independently (13 Boundlist, 10 StreamPack).

Frozen two-format prototype controls still compare their original Absolute Ends
and Sizes formats. Their dispatch now explicitly excludes the later Relative Ends
family instead of instantiating an unsupported historical width-only control.
No frozen kernel body was rewritten. `control-audit.py` verifies the frozen
reader/writer bodies against their Git origin; historical extraction equivalence
remains in its original evidence export.

## Reproduce and recover

The before Boundlist/StreamPack headers and sources are byte-identical to
`7a57deae` (35 relevant files verified). Recover the control outside maintained
source, then overlay it into an immutable capture:

```sh
python3 workbench/spikes/boundlist/api-control.py build/api-control
python3 workbench/tools/capture.py create build/captures/api-recheck \
  --replace workbench/spikes/boundlist/api-control=build/api-control
python3 workbench/tools/worker.py run workbench/spikes/boundlist/api-geometry-native.sh \
  --source build/captures/api-recheck --machine neoverse-v2 --detach
```

Use the [pinned Linux toolchain](../../../BUILDING.md), with `orb -m ubuntu` on
macOS. `api-native.sh` runs the existing complete-consumer panel as well;
`api-geometry-native.sh` focuses on new geometry consumers. Set
`BOUNDLIST_API_FULL=1` for the full panel and `BOUNDLIST_API_OPS` for its operation
count. Use `BOUNDLIST_TUNE=granite-rapids` on that platform. Both runners alternate
before/after process order and retain three process passes with three samples
per cell. Prepared setup and fixture construction remain outside timings.

The [evidence index](evidence/api/README.md) identifies the exact native sources,
hosts, selected files and full S3 recovery bundles. Final assembly, control headers,
source snapshots and unselected diagnostic runs remain in those bundles.
Final mixed-carrier/full-width-hook tests pass on all native profiles; both
complete consumer executables rebuild byte-identically to the measured versions
on all five profiles (ten matching binary hashes). The evidence index retains
that check separately from instruction-shape diagnostics. The
[report inputs](evidence/api/inputs.json) reproduce the selected reduction:

```sh
python3 workbench/spikes/boundlist/api-report.py \
  workbench/spikes/boundlist/evidence/api/inputs.json build/api-report
```
