# Shared SeriesPack operation columns

This is a spike-owned executable comparison, not a new Ikea public interface or
a universal retargeting object. It finishes the concrete missing comparison in
the [reuse survey](../../reusable-execution.md) and Frame's
[runtime-control proposal](../../evidence/interface-cohesion-v2-20261023/frame/runtime-control.md).
No Frame source or packed-bit implementation is copied.

The starting point matters. Frame's existing operation-major catalogue removed
232 bytes of bindings per block by `sizeof`, with no measurable local speed
change across 117 configurations. Its catalogue SUM was not faster than narrow
decode, and COUNT lost on the wide WatchID plane. The
[retired SeriesPack lessons](../../seriespack-history.md) retain short/suffix
regressions and 135–156 KiB of added range text. The
[cached-predicate negative](../PREDICATE-NATIVE.md) shows that fewer hot-path
instructions can lose badly through checked preparation and aggregate copies.
Those results favor reusing the current bodies, retaining exact edges and
charging complete commands before choosing a public representation.

## The comparison

`shape::make` admits a headless Local or supported Striped law and carrier width.
Its copied selector chooses immutable operation-major columns. The index field
is public for this experiment: every use requires the proof established by
`make`, not an arbitrary forged index. It admits no content identity, allocation
ownership, initialization or concurrency facts. A new width selects a new law;
it does not reinterpret bytes encoded under the old law.

One parallel immutable extent column retains row shift and tile bytes. Fresh
admission uses checked ceil-count shifts and multiplication overflow bounds,
instead of redoing width-dependent geometry/gcd. This is precisely the reusable
shape fact under test; the current copied runtime binder remains unchanged.
All 128 extent slots, including unused holes, count toward table residence.

The three read columns use **the exact current** `dense_point`, `dense_range`
and `dense_read16` function templates selected by `erase_dense_reader`. Their
arguments retain the same trusted point, arbitrary-edge exact-tail range, and
aligned complete 16-row access contracts. Narrow carriers are tested only where
the law fits. `shared_reader<U>` copies pointer/count/selector, while the current
`erased_reader<U>` copies pointer/count/three function pointers. A parent can
instead select one operation pointer and supply its own admitted placement.
The already small typed dense reader stays unchanged. The 64 resident bindings
are visited sequentially: this establishes metadata residence/block reuse, not
several independent streams or seven bindings simultaneously live in registers.
Raw shared-binding return ABI and crowded-register pressure remain open until
separately inspected, rather than inferred from `sizeof`.

The read routes are:

| Route | Work within each visited nonempty block |
| --- | --- |
| `columns` | Select one operation pointer; supply fresh body and count from the parent descriptor; q trusted calls |
| `columns-duplicate` | The exact same complete command function as `columns`; separate benchmark name |
| `shared-fresh` | Check this placement against an already admitted shape; copy the shared binding; q trusted calls |
| `shared-retained` | Borrow the prebuilt pointer/count/selector binding; q trusted calls |
| `copied-fresh` | Current runtime `bind_reader(dense_input)` with width/geometry/extent admission and copied entries; q trusted calls |
| `copied-retained` | Borrow the prebuilt current copied binding; q trusted calls |
| `typed` | Runtime-select the same width/geometry's small complete typed operation loop, then execute q operations |

The typed control uses the **same full mixed-width trace**, rather than one
constant width. It specializes only a small point/range/read16 loop, not a copy
of a Frame query. Complete outer traversals are `noinline` for linked inspection.
Operation-specific loops remain visible to the optimizer. That difference in
visibility is part of the complete-command comparison, not a pure call-latency
estimate. There are no per-value opacity barriers.

Initialization is a separate family with actual u64 input, exact destination
extent, input-fit checks, final-tile slack and real preallocated journals:

- `columns` selects one existing-body initializer for q commands.
- `dispatch` uses the current Frame-style 64-width checked setup pattern, over
  the same `attach`/`bind_writer`/`initialize` body. This is not a public runtime
  writer API that Ikea already provides.
- `typed` admits one named typed view/writer for this block and reuses it for q
  checked initializer commands.
- `columns-duplicate` invokes the exact `columns` traversal.

Every raw initializer is selected from its destination's admitted index, so its
typed law already matches that destination. All controls use the existing
`attach(count, planes)` overload equally. Its extent check does not authenticate
content or another encoding's law. Forging a callback/selector pair is outside
this prototype's contract; a future public checked plan/destination interface
would need one shape authority or explicit mismatch admission.

Each command translates the **whole journal** to the actual stable named
`destination` before the temporary typed view expires, preserving plane/offset/
size. The destination's placement/backing remain stable through owner resolution.
No source identity points to a destroyed stack view. The translation cost and
all 128 slots, including table holes, are charged. A single compiler memory
observation follows each complete initialization; no row has its own barrier.
Each route reserves 68 journal entries on the command stack. Conservative
admission can count body/tail fields separately even when execution coalesces
them; the largest checked fixture needs 66 additional entries plus two old ones.
The journal is resolved and reset per command. This does not qualify accumulating
unresolved writes while a source changes generation, preserving replacement,
summary maintenance, or publication.

## Useful work, checks and output

Every traversal visits 64 block descriptors. Eight planes are empty, 16 have 13
rows, eight have 16,16 have 31 and 16 have 256. Eight widths are live, in clustered
or a verified fixed permuted order. Local widths are 1/5/7/12/20/28/31/63;
Striped widths are 1/2/3/4/5/7/12/20. Permutation changes order, never work or
encoding. The q1/8/64 parameter means commands per active block, with fresh
row coordinates/inputs across traversal epochs. Read ranges have a constant
per-block 13- or 31-row take clipped to logical count, with varying arbitrary
starts. Point and range issue 56q calls; native16 issues 40q because short planes
are skipped. Empty work returns before binding, table or payload access.
Native16 always requests sixteen actual logical rows; an occupied larger final
tile does not provide those logical rows for a 13-row plane.

The 300 names comprise 252 `series/read/` cases and 48 `series/initialize/` cases.
Counters record visited/active blocks, issued commands, logical rows, width
transitions, occupied payload, input residence, retained binding capacity,
fixture metadata, table bytes and journal capacity. They are logical/storage
facts, not timings. The raw metric is CPU ns per complete traversal; initialization
and reads, and different operations/reuse counts, are separate comparisons.
All tables/typed bodies coexist in the same ELF; whole linked text/data bytes
must be reported. This is not an optimized minimal table footprint.

`--check-only` covers all 76 supported laws and four unsigned output carriers,
independent prior-wire inputs and exact initialized bytes/slack, arbitrary
edges, native16 access admission, protected exact suffixes, failure preservation,
and successive stable owners with existing same/different-source journal entries.
The complete mixed commands also check independent value/traversal and byte
oracles before timing. `--facts` emits sizes/alignment/triviality and static
table bytes as JSON. These are target observations, not portable ABI promises.

Root's sibling [direct-bind superproject](../direct-bind/CMakeLists.txt) builds
both studies, reusing one Ikea library build per profile:

```sh
orb -m ubuntu cmake -S workbench/spikes/ikea-composition/handle-boundaries/direct-bind \
  -B build/ikea-cohesion-v2/series-controls-local -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_BENCHMARKS=tuplepack \
  -DIKEA_CONTROL_SERIES=ON -DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2
orb -m ubuntu taskset -c 8 cmake --build build/ikea-cohesion-v2/series-controls-local \
  --target ikea_series_controls_commands ikea_predicate_direct_commands -j1
orb -m ubuntu taskset -c 8 build/ikea-cohesion-v2/series-controls-local/series-controls/ikea_series_controls_commands --check-only
```

## Native comparison

The completed [common cohort receipt](../../evidence/interface-cohesion-v2-20261023/execution-controls-native.json)
owns source recovery, command/log hashes, independent archive verification and
resource closure. [SeriesPack findings and linked evidence](../../evidence/interface-cohesion-v2-20261023/series-controls-native.json)
and [every case and five samples](../../evidence/interface-cohesion-v2-20261023/series-controls-native-cases.csv)
retain all 300 cases per profile. All builds and owning checks finished before
timing. Ratios pair trial indices; the exact duplicate is never a correction.
CPU nanoseconds measure one complete 64-descriptor traversal. Different
operations and q counts remain different amounts of work.

These are `columns / copied-fresh` ratios at q1. Each range covers both geometries
and both trace orders; lower is faster. The copied route performs its current
runtime width/geometry/extent binding on every active block. Columns start from
an admitted shape, whose construction precedes timing in this experiment.

| Native profile | Point | Exact-tail range | Native16 |
| --- | --- | --- | --- |
| NEON / Neoverse V2 | 0.320–0.484 | 0.808–0.902 | 0.555–0.709 |
| AVX2 / Zen 5 | 0.381–0.441 | 0.822–0.880 | 0.410–0.421 |
| AVX512 / Zen 5 | 0.387–0.466 | 0.829–0.866 | 0.427–0.455 |

The fresh shared binding also improves the one-shot reads, but smaller retained
bindings do not establish a speed win. At q1, retained shared versus retained
copied point reads lose 6.79% on NEON clustered Local, 6.33% on AVX2 clustered
Striped and 5.98% on AVX512 clustered Striped. Those exceptions are retained.
The following q64 results show the different repeated-command behavior:

| Native profile | Shared-fresh / copied-fresh native16 | Typed / columns native16 | Typed / columns initialize |
| --- | --- | --- | --- |
| NEON / Neoverse V2 | 1.002–1.026 | 0.971–1.013 | 0.843–0.849 |
| AVX2 / Zen 5 | 0.981–0.991 | 0.954–1.019 | 0.736–0.756 |
| AVX512 / Zen 5 | 1.222–1.231 | 0.727–0.734 | 0.718–0.772 |

For example, AVX512 permuted Local native16 at q64 takes 26,427 ns with a fresh
shared binding versus 21,666 ns with fresh copied binding. Its five ratios are
1.2215, 1.2156, 1.2183, 1.2236 and 1.2225. In that same cell, the retained shared
binding takes 21,671 ns versus retained copied's 21,216 ns, a much smaller loss.
This retained observation applies to native16; it does not remove the point
exceptions above. Duplicate median ratio ranges across all operations are
0.9894–1.0039 on NEON, 0.9807–1.0284 on AVX2 and 0.9948–1.0435 on AVX512.
Small differences near those ranges remain uncertain.

Whole typed commands are also uneven: typed range loses 6.2–9.3% against
columns on NEON, with losses on both x86 profiles too. At q1 typed point is
slower on every profile. For initialization, columns versus the current
64-width dispatch pattern ranges from 0.909–0.990 on NEON, 0.954–0.977 on AVX2
and 0.943–0.978 on AVX512 over all q counts. The typed initializer amortizes its
view/writer setup at q8/q64 while retaining checked fit, extent and effects work.
These comparisons do not produce an overall performance score.

## Actual linked callers

[The offline linked reader](linked.py) verifies all 21 complete traversals,
outlined native16 query callees, the current runtime binder, selected native and
typed bodies, and initializer/coverage helpers: 44 functions per profile. It
compares each selected function's full bytes with the measured ELF, verifies all
shared table pointers and carrier holes, and checks the extent facts. Static
instruction counts include cold paths and are not dynamic work estimates.

The AVX512 shared native16 query reloads the selector before a dependent table
lookup; copied binding loads its function pointer directly. Fresh and retained
shared routes use the same outlined query callee. That extra lookup alone does
not explain the fresh route's much larger loss, because their metadata placement
and complete caller contexts differ. This attribution remains unresolved.

The selected typed AVX512 native16 body keeps constants/results in vector
registers and consumes results directly. The indirect body writes the output
buffer and its caller rereads it. Conversely, the selected typed NEON range
reloads command metadata and repeats take/bound/consume setup within q; the
columns caller computes those invariant facts once per block. These are concrete
code-generation differences, not isolated proofs of timing causality.

All profiles report a 24-byte shared binding versus 40-byte copied binding and
16-byte typed dense reader. The 64 retained bindings occupy 1,536 versus 2,560
bytes. All tables, invalid holes and four carriers occupy 15,872 bytes. The
complete executable's `.text` is 1,150,880 bytes on NEON, 1,652,256 on AVX2 and
1,563,872 on AVX512. That includes every control, oracle, library body and Google
Benchmark; it is not incremental candidate text or a minimal representation.

[The result reader](results.py) can reproduce the all-case CSV from the two
recovered result roots. [The common receipt](../../evidence/interface-cohesion-v2-20261023/execution-controls-native.json)
identifies durable archives and complete source recovery. Exact bulky ELFs and
assembly need not be committed:

```sh
orb -m ubuntu python3 workbench/tools/worker.py fetch 20261023T114232Z-c1ba2d45 \
  --file neon/ikea_series_controls_commands --file neon/series-linked.txt
orb -m ubuntu python3 workbench/tools/worker.py fetch 20261023T114225Z-27e9df4a \
  --file avx2/ikea_series_controls_commands --file avx2/series-linked.txt \
  --file avx512/ikea_series_controls_commands --file avx512/series-linked.txt
```

## Scope still open

The experiment establishes no public shared-shape/placement authority, portable
small-handle ABI, optimal table representation or cross-module default. The
public experiment index relies on admitted construction; mismatched/forged
selector semantics remain outside its contract. Warm sequential resident
bindings do not qualify simultaneously register-live handles, independent
interleaved streams, cold tables/data, concurrency or allocator-class effects.

Writes cover initialize, not preserving/masked replacement, summary maintenance,
publication or accumulated unresolved identities across retarget/generation.
Heads, placed/composed sources and narrow-carrier performance are unmeasured.
Complete Frame, StreamPack and Directory application workloads are absent; the
three consumers have distinct ownership, composition and coordinate needs.
No public SP API change was made from this comparison.
