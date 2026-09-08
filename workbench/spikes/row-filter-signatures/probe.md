# Executable resident scan probe

The first executable study follows the feedback in the Codex task
**Scaffold SixDB project docs** (`01a07ca5-e01f-7833-b5bf-3a96cb8c4310`), consulted
on 2026-09-08. In particular, Ashton asked for scaffolding that accelerates
research and warned that fixed gates can give the appearance of rigor without
contributing useful evidence. The numbered [experiments](experiments.md) are
possibilities, not mandatory stages. This probe takes small contrasts from
them and follows results with stronger controls and focused reruns.

The transferable lessons were to identify saved **and introduced** work, make
the competing implementations credible, vary workload dimensions independently,
include construction/maintenance, and retain compact evidence with recoverable
sources. The existing dataset cache, captured build workspace, runner receipts,
and artifact storage supply the mechanics without a new framework.

## Run and select

On Linux, from the repository root:

```sh
python3 workbench/spikes/row-filter-signatures/run.py
python3 workbench/spikes/row-filter-signatures/run.py --text --sanitize
python3 workbench/spikes/row-filter-signatures/run.py \
  --select '^(absent|near|far)/(soa|tag32|tag16|tag8|joint64)/q0$' \
  --rows 1048576 --repetitions 5 --min-time 0.02
```

From macOS, prefix with `orb -m ubuntu`. The default is a small comparison.
`--select` is a regular expression over `case/method/phase`; phases are
`q0` through `q3`, `build`, and `replace`. Unsupported maintenance combinations
are omitted. An empty selection fails. `--seed` changes data, `--hash` changes
encoding hashes independently, and `--cpu` selects the pinned Linux CPU.
`--march` is optional; tuning defaults to generic. Native SIMD currently uses
NEON; other architectures have a scalar correctness fallback and must not be
described as an optimized x86 comparison.

`--text` reuses the shared 65,536-description accident sample and caches a
small binary adapter. Original case-sensitive UTF-8 bytes, source ordering,
and duplicates are preserved. Source data and adapters share the checkout's
dataset cache even when code is running from a captured workspace. Real text
is capped at the available sample; synthetic scalar/range/text populations
use `--rows`. Accounting reports the actual denominator.

The runner captures sources, refreshes editor configuration, builds independent
TUs, checks the semantic/native models, accounts work separately, then runs
Google Benchmark repetitions sequentially on the selected CPU. It prints
the result directory and the existing one-command retention command. Analyze
raw or retained evidence with `analyze.py DIRECTORY`; individual repetitions
and accounting must agree before tables are generated.

In the final text run, cached input preparation took 0.058 seconds, the
unchanged optimized build 0.009 seconds, and its full native checker 0.210
seconds. Accounting took 6.478 seconds and timing 53.884 seconds. These are
observed loop costs, not performance promises; captured sources and the shared
cache kept setup small enough that focused reruns were inexpensive.

## Information and execution

Scalar fixtures contain eight uint32 fields. Ordinary values have their high
bit set; four query targets, 0–3, have it clear. The `rate` in the generator is
the combined frequency of those four targets, not the frequency of each
query. `hot` instead gives target zero 90% mass per field; its other queries
are absent controls. The exact true-match counts accompany every comparison. `near` makes
the first atom true for every row. `exclusive` and `paired` have the same
marginal target frequencies with different within-row combinations.

Dedicated field tags use a salted mixed hash and four bits per field, for
32 bits per row in total. `tag32`, `tag16`, and `tag8` store identical information
as one, two, or four streams. `tag8_eager` reads every relevant plane even when
earlier evidence emptied the 16-row group. The other tag paths skip those
groups. Query masks are prepared once per scan request; a conjunction of
resident fields uses one fused masked comparison. OR/CNF expressions retain
their Boolean shape with unknown atoms treated as possibly true.

`tag8_cnf` swaps the second and third field slots, putting `(A OR C)` and
`(B OR D)` into separate bytes for the CNF case. `joint64_cnf` makes the same
change in the rollup. All fields keep four bits; this isolates grouping.

The later `joint16_planar/64_planar/256_planar` comparison holds these summaries
and their total bytes fixed but stores each plane's block summaries contiguously.
The original joint representation interleaves four plane summaries within each
block. Candidate and skip counts must agree; query footprint and traversal can
differ. This checks whether the block-size result is partly a placement result.

`joint16/64/256` maintain a 256-bit observed-code set for **each of the four
byte planes** per block. Thus their additional rollup payload is 8, 2, and
0.5 bytes per row, respectively, beyond the four row-signature bytes. A query
reads only relevant plane summaries. This differs from the one-plane storage
example in [rollups](rollups.md). `marginal64` stores eight 16-bit presence
maps per block. `union64` ORs 32-bit shared row masks. Shared 8/16/32-bit masks
all encode eight fields, two distinct bit positions per field contribution;
unlike the tag comparison, their information budgets differ.

All SIMD paths process 16 row positions per execution group: one byte vector,
two uint16 vectors, or four uint32 vectors. They produce row masks, followed
by immediate exact refinement from AoS fields where necessary. `soa` is a
native exact column control; `aos` is the generic direct row executor. The
latter is not claimed to be the best possible specialized AoS scan.

Unsigned range fixtures use one query across the whole population, with
different thresholds for the four queries. `prefix8/16` store the complete
32-bit value in significance-ordered streams; these are **alternative primary
value layouts**, not additional probabilistic signatures. Unresolved low/high
boundary masks control refinement; interior rows need no residual. `native`
compares full values directly. Broad values, long common prefixes, and sorted
values distinguish early refinement from simply doing narrower comparisons.

Text methods hash every three-byte window into 8/16/32/256-bit row signatures
with two hash-derived positions per gram. Query preparation hashes required
grams; survivors use `std::string::find` with identical byte semantics. Short
queries have no gram evidence and pass through. This is substring containment,
not token search or collation-aware SQL. The 256-bit arm uses a row-oriented
wide signature; the tiny arms use native SIMD lanes. Width, layout, and kernel
therefore change together in the text comparison. Exact `term_bits` materializes
all four requested terms in four bitmaps: it is query-specific, needs a rebuild
for new terms, and is charged for all four during construction/storage accounting.

## Outputs, costs, and limits

Every query writes the complete output bitmap and returns a count plus the sum
of one-based physical row IDs. Exact native controls write the same output.
The checker compares complete masks against an independent scalar oracle.
Every timed query checks count/sum; complete masks are checked before and after
each benchmark invocation. Output allocation is prepared before timing and
reused; resetting/writing the output is included. No payload projection, codec,
dictionary executor, positional search index, sparse incoming mask, adaptive
planner, or CPU-specific dispatch bank is implemented.

Counters come from a separate compile-time instrumented instantiation. Plane
bytes count logical stream spans, with a shared loaded plane counted once;
summary bytes count logical consulted summary fields. Candidate/residual counts
refer to the row-signature output and exact verifier, respectively. Vector exact
paths record their already-exact active rows in the candidate column; generic
direct scans have no candidate count. Only prefilter candidates have a
false-positive interpretation. `text_bytes` is the size of
strings passed to the verifier, not bytes examined by `find` or device traffic.
Skipped-block counts concern the maintained rollup; plane-group counts expose
later reads. No hardware bandwidth or cache-residency classification is implied.

The fixture keeps both AoS rows and SoA columns for the oracle/control comparison.
Both remain resident, and the full fixture capacity is reported separately.
Only one contender's additional metadata is constructed for each invocation;
metadata from all alternatives does not coexist during its timed scan. Build,
fixture generation, oracle checks, and a measured-arm rewarm are outside query
timing. These are warmed local VM measurements with pinned guest affinity;
guest cache/frequency reporting does not establish a physical cache tier or
exclusive host-core ownership.

`build` times allocation and construction of auxiliary metadata (or alternate
prefix storage), excluding source generation and destruction. `replace` updates
the primary AoS field plus metadata, using a pregenerated random trace replayed
twice with XOR values so every cycle restores the initial state. Both replays
are timed and counted. SoA is an unused oracle mirror in this mutation phase;
it requires no duplicate write. Shared masks are fully recomputed, while tags
replace a single slot. Completed state is checked against a rebuild. This is
single-thread field replacement; rollup repair, concurrent visibility, physical
insertion/deletion, WAL, and reclamation are not timed.

The small Python model separately enumerates NULL-aware Boolean bounds, OR and
fixed-bit rollups, joint presence and maximal-mask semantics, threshold bins,
and a dirty bypass with retained snapshots. Its drift example contrasts fixed
old quantiles with rebuilt boundaries and demonstrates why recoding editions
matters. These are semantic checks and work counts, not a production mutation
or concurrency implementation.
