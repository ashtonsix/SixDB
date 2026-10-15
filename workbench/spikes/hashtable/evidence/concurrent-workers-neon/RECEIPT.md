# Native shared/private worker comparison

This receipt covers group `20261013T182634Z-6d915b37`. All three jobs completed
successfully. Keep every arm and repetition: the question is whether adding
ordinary shared concurrent owners changes the assessment of private aggregation,
including the existing tight-capacity and reconstruction controls.

| Target | Job | ISA / tuning | Retained measurements |
| --- | --- | --- | --- |
| Neoverse V2, c8g.large | `20261013T182640Z-c406f298` | `armv8.2-a+crypto` / `neoverse-v2` | [hot](hot.csv), [large](large.csv) |
| Zen5, c8a.large | `20261013T182647Z-bd1a309d` | `x86-64-v3` / `znver5` | [hot](../concurrent-workers-zen5/hot.csv), [large](../concurrent-workers-zen5/large.csv) |
| Granite Rapids, c8i.xlarge | `20261013T182653Z-d4207a4d` | `graniterapids` / `graniterapids` | [hot](../concurrent-workers-granite/hot.csv), [large](../concurrent-workers-granite/large.csv) |

Each target has 24 independent-oracle check rows and 216 rows per timing panel:
eight arms × three histories × nine repetitions. Cardinalities are 1,800 and
110,000 distinct keys; uniform repeated and skewed histories have 16 times
as many input rows as distinct keys. Each arm receives the same precomputed two-worker intake. Arm order
rotates with repetition; candidate seeds also vary with repetition. Each target
is one native host, not nine independently provisioned hosts. The panel names
`hot` and `large` describe cardinality, not measured cache residency or DRAM.

## Source, compiler and topology

The canonical capture manifest SHA-256 is
`4c78d330b4a540d3285c54297fbf410c2d857f621d2ec69553700fad264d7688`.
It is the SHA of `json.dumps(source_manifest, sort_keys=True).encode()`, not the
physical manifest file bytes. `workers.cpp` SHA-256 is
`1eba810521d3a90b9c865daa6301b0bc328754387ed6835094d306329a32006e`.
All three archives agree. The captured worker driver, report, codegen guard,
concurrent adapters/dependencies and runner match the frozen source. Boost uses
the pinned 1.90.0 official archive (Unordered revision
`11e05a3c4c449a46819f0030e7d105fbc154d476`); GTL uses revision
`4c0a7a1d4a86143247b6ea7a5e4c7d6104d78590`. Archive hashes are enforced
by the captured `concurrent_baselines.cmake`.

Clang 21.1.8 builds the actual worker target with `HT_CONCURRENT_WORKERS=1`,
`-O3`, `-fno-vectorize`, `-fno-slp-vectorize`, and
`-mllvm -inline-threshold=1000`, plus the target flags above. CPU 0 and CPU 1
are different physical cores on the same socket and NUMA node, as recorded by
each retained `topology.txt`. Granite SMT was disabled; the other two machines
have one hardware thread per core. The coordinator is an additional OS thread,
so timings include its barriers; only the two workers are explicitly pinned.

## Final generated code

The frozen guard was rerun against each final `assembly.gz`, reproducing the
archived `codegen.json` exactly. Per target there are 37 private consumers:
7 accumulate, 4 route-local, 7 rebuild-merge, 8 merge-from and 11 final-read
bodies, including specialized traversal bodies; plus 2 shared accumulate and
2 shared final-read bodies. Every private and shared transfer list was inspected;
there are no indirect calls/branches in these bodies. Private hot search and
aggregate callback boundaries fuse. Remaining private transfers include
first-use StreamPack format admission, link append, whole-table/link traversal,
allocation, Abseil missing-key prepare helpers and failure handling. These are
charged work, not removed from the timings. The audit does not infer equal code
size, register pressure, memory traffic or instruction cost.

Boost's adapter fuses. Each input calls one actual
`boost::mp11::detail::tuple_apply_impl<...call_emplace_or_visit_impl...>` helper;
its body includes one combined search/protected update, including in-place sum
and protection release on a match. It is not a second lookup. The helper also
contains contention, retry and insertion paths. The final-read body performs
its own fused search/consume with `cvisit` protection. V2 uses native LSE
`casa`, `cas`, `ldaddal`, `ldaddl` and `ldsetb`; no compatibility
`__aarch64_*` atomic helper is on these paths. X86 uses native locked atomics
and SIMD control comparisons. Example matched-value updates are at `0x311d8`
(V2 store), `0x34c69` (Zen add) and `0x34499` (Granite add), in each archived
final binary. Contention/sleep/yield code exists but a static call list does
not establish how often it runs.

GTL's protected add performs hash, shard mutex lock, search, in-place sum and
unlock in the outer input loop. Missing-key insertion preparation may call
out. Representative matched updates are `0x31f14`–`0x31f1c` on V2,
`0x35e0f` on Zen and `0x3548f` on Granite. Quiescent GTL final reads are fused
and unlocked. V2 GTL uses its native portable word control scan; it is not
relabeled as a forced NEON implementation. X86 control comparisons are SIMD.
These addresses are evidence about boundaries and operations, not a cycle model.

## Semantics and limits

Keys are K40 stored in ordinary uint64 keys; values are modular uint64 sums.
The two shared controls use ordinary uninstrumented owners, reserve known union
cardinality, and admit both input streams directly. Worker 0 constructs,
first-touches and destroys that owner. Completion is quiescent and checked
against an independent dictionary, including exactly-once insertion counts.
There is no artificial shared routing/merge phase. Boost retains protected
`cvisit` on final reads; GTL uses its explicit unlocked quiescent API.

Private arms retain the existing allocation ownership, routing and merge work.
Reported lifecycle sums setup, intake, finish, source retirement and release,
including their barriers. Runtime creation, final reads and post-release join
are separately reported; the join is not a measurement of complete physical
Pool drain duration. Final reads are warmed after independent verification.
Inventory is sampled live storage, not transient relocation peak or RSS.
Shared allocation totals remain blank/null: only owner metadata is known.
Do not infer a shared memory advantage, or general concurrent-visibility/erase/
last-writer semantics from this aggregation history. This K40 map bridge does
not complete all-width/kind multi-writer coverage, coordinated online growth
or balancing.

## Interpretation and recovery

Private aggregation is substantially faster than the protected shared controls
on repeated/skewed histories, but private Abseil remains the stronger baseline
for most candidate comparisons. The tight distinct-key merge still puts a
median 32,973 of 110,000 keys in chains. The roomy alternative changes memory
and capacity, rather than proving the tight merge competitive. Full medians,
min/max lifecycle ranges, components, memory fields and final reads are in the
retained summary JSON beside every complete raw panel. Small-panel barriers
and destruction contribute materially and have visible repetition variance.

Each directory's `artifact.json` recovers the complete verified worker archive,
including measured executable, full assembly, compile commands, logs and source
snapshot. Retention reused those archives; it did not manufacture a new source
receipt. `artifacts.py verify` checks retained byte identities. The report
recipe regenerates each hot/large summary from retained raw rows. To inspect
final assembly, recover `assembly.gz` with `artifacts.py fetch ... --file
assembly.gz`; the matching guard is in the recovered `source.tar.gz`.
