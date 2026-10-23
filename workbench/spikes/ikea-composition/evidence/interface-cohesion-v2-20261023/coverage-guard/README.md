# Keep the batched-tail exclusion test before a footprint call

The isolated private change removes redundant calls from the actual k31h8
substituted mutation caller. It has been applied after code review. Native R4 closes the large AVX512 k31
all-row loss against the original baseline, but leaves AVX2 mutation and
bimodal AVX512 point losses. The full results and their limits follow below.

[guard.patch](guard.patch) is the exact R3-after change.
[codegen.json](codegen.json) records source/dependency hashes, compiler commands,
object hashes, representative complete-caller paths and text/frame tradeoffs.
[coverage_guard.cpp](coverage_guard.cpp) is the independent semantic witness.
[artifact.json](artifact.json) recovers full object records, disassembly, all
callsite extracts, commands and the optimized witness binary from ignored output.
The original R3 native worker artifact in `codegen.json` recovers the common
frozen source tree.

## The actual path difference

The complete `placed_mid.cpp` caller uses a local 31-bit value with an 8-bit
head and a substituted 7-bit striped tail. It mutates 8,192 rows with real
issued-byte effects and sum maintenance. Its enclosing batch covers each whole
tail tile once. `other_coverage` must therefore exclude that **same named tail
source**, while continuing to report body/head and any different tail source.

The original pre-cohesion caller placed its source-identity comparison before
the optional footprint call. R3's after caller unconditionally calls the
same-type tail helper; the helper compares source identities and immediately
returns for this matched source. There are sixteen such sites per 256-row tile:
**512 no-op call/returns per complete invocation**. Equal static call counts
hid the difference. In the linked R3 all-row caller, for example, the original
`0x103c0a` comparison branches around the call at `0x103c2c`; the after caller
calls at `0x1031ff`, then its helper checks identity at `0x107350` and returns.

The accepted patch gives only the same-type/tail overload a small always-inline
guard. Its unmatched case forwards to a separate ordinary footprint helper.
All sixteen sites in both the all-row and partial object paths now compare
identity **before** any optional tail helper call. The heavy unmatched helper
outlines naturally in this control; no forced-noinline annotation was needed.
Body/head and different-source-type tail methods keep their original body.
It preserves command order, effects protocol and physical stores.

## Whole-TU and complete-region costs

The paired objects compile the unchanged actual `placed_mid.cpp` from the R3
after source, first with its original header and then with only the guard patch.
Both use pinned Clang 21.1.8, O3, AVX-512/Zen 5 tuning and the same benchmark
include. The dependency receipt confirms that only `composed.h` changes. These
are unlinked object controls, not the R3 native executable or a timing result.

| Emitted scope | R3 after | Guard wrapper |
| --- | ---: | ---: |
| Complete TU functions | 78 | 78 |
| Complete TU function bytes | 270,317 | 271,008 |
| All-row region bytes | 17,480 | 17,582 |
| All-row frame allocation | `0x1f8` | `0x1e8` |
| All-row static callsites | 54 | 54 |
| Partial region bytes | 36,145 | 36,583 |
| Partial frame allocation | `0x358` | `0x298` |
| Partial static callsites | 102 | 102 |

The full TU grows 691 function bytes. Static stack-reference counts rise in
both regions despite smaller frames; neither frame size nor static call count
alone establishes a runtime gain. The changed matched path skips the optional
calls, while a nonmatching source still executes its real footprint helper.

## Identity, coverage and required controls

The O3 NEON witness passed on the local Linux target. Its bounded hook preserves
every event without journal coalescing. Independent expected spans check:

- The already covered target emits no duplicate tail effects.
- Another same-type tail emits its one/two stripe fragments with its own identity.
- A copied named view at identical physical placement still emits coverage.
- A different-type tail and original body/head keep exact source, plane, offset
  and size events.

The copied-view case prevents broadening the exclusion to storage-address
equality. This is source identity, not a new owner permission or a way to retain
old journal entries after changing a view's placement.

The [R3 native comparison](../native-r3.json) has a consistent roughly 15%
paired-median all-row loss, while its partial case already improves about 6%.
Both paths contain the extra no-op calls, so they cannot by themselves explain
the entire difference. Native R4 retains all/partial cases and wide/other
placements, including the k63 AVX2 partial case recovered by the preceding
visitor change. A positive all-row result cannot offset a loss in those controls.

The archived `all-inline.patch` and `guard-wrapper-noinline.patch` are prepared,
uncompiled alternatives. The first forces the entire original coverage method
inline; the second forces the unmatched helper out of line. They are not
measured competitors and need not be broadened without a useful signal.

Recover the isolated audit with:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/ikea-composition/evidence/interface-cohesion-v2-20261023/coverage-guard/artifact.json \
  build/recovered/cohesion-coverage-guard
```

The archive retains `compile-first.py`, `analyze-first.py`, complete dependency
records and all sixteen guarded/unguarded callsite extracts for each region.
Recompilation also needs the common R3 after tree, recoverable from the worker
reference, and the recorded pinned toolchain/benchmark include. No native job
was submitted for this isolated audit.

## Native R4 and historical controls

[native-cases.csv](native-cases.csv) retains all **864 canonical SeriesPack
comparisons**: 96 cases × three profiles × R2/R3/R4, each with all five before
samples, after samples and paired ratios. [native.json](native.json) records
worker artifacts, source/binary/raw-trial hashes and all R4 losses of at least
5%, plus the nearby k63 partial control. Every sample was independently
recomputed from the raw item counter and checked against the common reader.
This retains ordinary bound/concrete read, point and maintained mutation;
separated/interleaved/substituted placed all/partial sum and mutation; and their
materialized controls. There is no combined speed score.

R4 substituted bound mutation, paired-median CPU time change against the
original before arm (negative means less time):

| Case | NEON | Zen5 AVX2 | Zen5 AVX512 |
| --- | ---: | ---: | ---: |
| k20h8 all | −0.25% | −0.33% | +0.42% |
| k20h8 partial | −0.96% | +1.15% | +1.17% |
| k31h8 all | −9.03% | **+5.31%** | +1.69% |
| k31h8 partial | −7.19% | +0.69% | −0.19% |
| k63h16 all | −2.08% | **+6.74%** | +2.08% |
| k63h16 partial | −0.42% | **+4.88%** | +0.16% |

The AVX512 k31 all-row case moves from R2 +30.10% and R3 +14.67% to
R4 +1.69%. Its R4 five ratios are 1.0345, 1.0043, 1.0169, 1.0280 and 1.0108.
The AVX2 k63 partial case moves from R2 +76.51% to R3 +4.72% and R4 +4.88%:
it remains a consequential nearby loss even below the reporting threshold.
The NEON R4 cohort has no canonical loss of at least 5%; its largest paired
median is +2.76% for substituted k31 all-row materialized sum. Earlier NEON
candidate wins do not all survive: for example, R2 k63 partial was −6.86% while
R4 is −0.42%. Each cohort is paired against its original baseline; comparisons
between different cohorts' after medians are unpaired historical context.

All four R4 canonical losses at or above 5% remain explicit:

| Profile and case | Paired median | All five after/before ratios |
| --- | ---: | --- |
| AVX2 k31h8 substituted/all mutation-bound | +5.31% | 1.0508, 1.0492, 1.0600, 1.0531, 1.0759 |
| AVX2 k63h16 substituted/all mutation-bound | +6.74% | 1.0674, 1.0678, 1.0740, 1.0654, 1.0653 |
| AVX512 ordinary k7/local point/bound | +19.94% | 1.1994, 1.1995, 0.8574, 1.1994, 1.2005 |
| AVX512 ordinary k12/striped point/bound | +16.62% | 0.8336, 1.1662, 1.1667, 0.8344, 1.1671 |

These are warm repeated bound/trusted callers. Setup and admission are outside
timing. Mutation repeats an already installed replacement after the independent
pre-timing oracle, while still performing actual reads, stores, journal and
maintenance work. Partial selection is `0xb6db` (11 of 16 lanes); ns/value divides
by all 8,192 logical rows. No cold, one-shot or owner-publication claim follows.
R4 primary timing has no PMU counters.

## Remaining linked-path findings

[linked.json](linked.json) retains the actual R4 linked driver loops, selected
point callees, guarded optional-tail sites, traced coverage paths and original
objdump/binary receipts. This extends the earlier reader-object comparison to
the complete timed callers and their actual selected callees.

For AVX512 k7 and k12 point reads, the complete timed caller regions are
byte-identical (25 and 32 instructions), as are the actual selected
`dense_point` callees (14 and 18 instructions). The loop still loads the query,
resource and indirect function pointer, calls once, accumulates and branches;
there is no added descriptor load, spill or selection branch. Their addresses
change: inner loops and callees move by 32 modulo 64. The raw results have
distinct regimes, including one k7 before trial at 1.5483 ns/value among four at
1.1066, and k12 after trials at approximately 1.107 or 1.549. Equal instruction
bytes bound an added-work explanation; they neither remove the actual losses
nor establish a frontend/predictor/layout cause. Those losses remain unresolved.

The AVX2 substituted all-row driver devirtualizes the bound operation directly
to its region in both arms: each has the same 41-instruction timed opcode
schedule. Both final R4 and original before skip the matched optional tail
helper at all sixteen sites. The main k31/k63 all-row regions shrink from
4,285/7,718 to 4,261/7,687 instructions, with smaller frames. Those static totals
hide extra work in the actual body/head coverage callees.

Each 16-row coverage helper visits two original eight-row physical tiles with
stride 64. Their spans are disjoint, so the fixture appends two entries.
The regular body path grows from 56 to 63 instructions; each head path from
59 to 66. It adds one callee-saved push/pop pair, register bookkeeping and an
unconditional branch; the eight conditional branches are unchanged. The first
body after the substituted tail follows a shorter source-mismatch path, also
adding seven instructions. At 512 calls per leaf this is 7,168 additional
traced helper instructions for k31 and 10,752 for k63 per operation. These are
source-derived path counts, **not measured retired events or cycle estimates**;
other region changes can offset work. The bounded flat-journal lowering ablations below fail to remove this work.
No blanket alignment or inlining change is inferred.

## Rejected flat-journal lowering controls

[journal-lowering.json](journal-lowering.json) records two semantic-equivalent
private overlays against the frozen R4 after tree. The actual unchanged
`placed_mid.cpp` is compiled with the pinned O3 AVX2/Zen5 flags; no selection,
consumer or physical kernel changes. Public `journal::entry` fields and flat
`before(source, plane, offset, size)` remain intact.

A [restores the plane field](journal-restore-plane.patch) on a successful
coalescing merge, emulating the old aggregate assignment's explicit plane.
B [forwards a private span](journal-span-lowering.patch) through an ordinary
inline helper, retaining A and restoring a three-field coordinate argument at
the private boundary. Comparing A to B isolates that argument lowering.

| Emitted scope | R4 after | A | B |
| --- | ---: | ---: | ---: |
| Complete TU function bytes | 262,569 | 263,106 | 263,106 |
| Body helper bytes | 347 | 362 | 362 |
| Head0 helper bytes | 340 | 355 | 355 |
| Actual regular body path instructions | 63 | 63 | 63 |
| Actual regular head0 path instructions | 66 | 66 | 66 |
| All-region bytes | 19,559 | 19,601 | 19,601 |
| All-region frame | `0x1f8` | `0x208` | `0x208` |
| Partial-region bytes | 39,829 | 39,893 | 39,893 |
| Partial-region frame | `0x2f8` | `0x2f8` | `0x2f8` |

All three emit 80 functions and retain 54/102 all/partial region callsites.
A changes sixteen functions without removing the extra append-path work.
Every function byte/relocation, constant and unwind payload is identical
between A and B. The private span therefore adds no recovery in this control.
Both candidates are rejected; neither was applied or timed natively.

A third [local-cursor control](journal-local-cursor.json) copies `used` and the
record pointer into locals, stores the appended entry, then publishes the new
cursor. The [effects-only patch](journal-local-cursor.patch) preserves the
disjoint-metadata/no-fail contract and passes the existing independent journal
witness. It makes the target paths worse: body/head 63/66 to 65/68 instructions,
with the same branches and saved-register work. The complete TU shrinks 215
function bytes, while all/partial regions shrink 16 bytes each with unchanged
calls and frames. Those totals hide the worse issued append paths. This control
is also rejected without native timing or a live change; the AVX2 gap remains.

The [independent journal witness](journal_witness.cpp) passes at O3 on NEON for
all three variants. It checks exact source/plane/offset/size outputs, equal-valued
distinct objects and source types, forward/reverse touching-span merges,
disjoint appends in both offset orders and last-entry-only coalescing. It also
asserts the effects concept and journal's aggregate construction.

This rules out these two proposed fixes, not all effects of record layout,
alias analysis or selection lowering. The original nested record and final
flat record can expose different optimization opportunities even with the same
logical algorithm. Further runtime attribution requires a candidate that changes
the actual issued path, or a targeted counter experiment; these negative controls
do not justify another native timing job. The AVX2 residuals remain unresolved.

[journal-artifact.json](journal-artifact.json) recovers the three objects,
disassemblies, full function/constant/unwind records, dependency hashes,
commands, scripts and witness binaries. It also preserves the raw-trial curation
and linked-path scripts. The common frozen R4 source is recoverable from the
worker reference in `journal-lowering.json`; the pinned compiler and benchmark
headers are external inputs recorded by their hashes.
