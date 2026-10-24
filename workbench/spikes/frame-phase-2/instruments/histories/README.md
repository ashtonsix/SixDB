# Original-row histories for fixed Frame policies

This private C++ fixture supplies the ordered commands and independent expected
answers in [PROGRAMS.md](../PROGRAMS.md). It contains no encoded image,
correction store, owner, candidate answer cache, timer or MVCC implementation.
`history.cpp` applies assignments to ordinary original-row vectors and iterates
those current rows directly. It never generates an answer by adding corrections
to a base answer. Immutable original rows supply retained-old-reader answers.

The starting point is [phase 1's native chronology](../../../frame-phase-1/lifecycle/NATIVE.md),
[repair findings](../../../frame-phase-1/lifecycle/REPAIR.md), and
[program costs](../../../frame-phase-1/lifecycle/PROGRAM-COSTS.md): input order,
original coordinates and retained-reader answers matter; folding is not logical
time; fitting setters and less encoding did not eliminate ownership/copy cost;
expensive accounting belongs outside clocks. This module supplies those semantic
inputs without carrying forward phase 1's owner implementation or measured costs.

## Calling it from the substrate

Link the private static target `frame_history_oracles`, add this directory's
CMake file from another caller, and include `history.h`. The phase-2 instruments
parent already includes it. `sixdb_target` supplies the repository toolchain.

```cpp
namespace h = frame_histories;
h::columns original{a_rows, b_rows};
auto batches = h::make_batches(original, h::edit_pattern::dispersed,
                              h::edit_values::local_replacement);
auto reads = h::make_reads(original, h::read_mix::selection,
                          h::thresholds::varying);
if (!batches || !reads) { /* report explicit malformed input */ }
auto commands = h::make_trace(std::move(*batches), std::move(*reads),
                              h::chronology::interleaved);
auto expected = h::prepare_expected(original, commands); // before all clocks
if (!expected) { /* report explicit malformed trace */ }
// Build each candidate from original. Drive its own edit and query operations
// from commands.events. Compare its actual consumed result to expected entries
// outside clocks; these entries must never be returned as candidate results.
```

`trace.events` references ordered `batches` and immutable `reads`. A query entry
in `prepare_expected` records its event index, read index, current answer and
original-reader answer. B may keep/release a candidate's retained reader at its
own declared points. Expected original-reader results remain available; this
module does not implement candidate ownership, fold effects or retention costs.
`fold_after_batches` names opportunities after 4 and 8 applied batches in the
standard eight-batch trace. They do not demand a fold or publish a new version.
The tiny fixture instead offers opportunities after its two batches.

Both schedules preserve all eight ordered 64-assignment batches and eight
copies of the same 16-command read template. Burst places every batch before
all reads; interleaved places one template after each batch. Equal read
multisets deliberately have different intermediate answers. Repeated/varying
threshold modes retain identical point lists. Read templates are:

| Mix | Commands per template |
| --- | --- |
| Reduction | 8 SUM(B), 8 COUNT(A >= threshold) |
| Selection | 8 predicate(A) → SUM(B)/COUNT, 4 COUNT(A >= threshold), 4 SUM(B) |
| Point-heavy | 14 batches of 256 independent B requests, 1 SUM(B), 1 pair |

Query thresholds are frozen from original A's median, 90th percentile, minimum,
maximum and quarter quantile, plus 0, UINT64_MAX and 2^63. Repeated mode uses the
median; varying mode cycles that fixed list. Point rows come from a fixed local
integer generator, independent of candidate state, with deliberate duplicates.
The generator is preparation, not a candidate operation or a workload-frequency
claim. It does not retune selectivity after edits.

The repeated-hot writer revisits four original rows per field, with repeated
assignments inside each batch. Dispersed edits walk blocks and then move the row
within each block. `local_replacement` takes a value from the original same
block. This is a neutral label, **not a promise that any native/Frame basis or
width still fits later**. `dictionary_novelty` chooses each field's successive
absent original values (also distinct across generated assignments). It promises
dictionary novelty, not wider residuals. Different policy/grain admission belongs
to the candidate. Neither generator examines correction state.

## Exact answer and access laws

All values are uint64 stored-domain values; SUM is exact unsigned 128-bit.
Both columns must have equal length and consist of complete 256-row blocks;
empty domains are valid for direct evaluation. Generators require a nonempty
domain. A read's explicit field selects A or B for SUM, COUNT or points;
pair always means `A >= threshold`, consuming B. Answers contain:

| Operation | `sum` | `count` |
| --- | --- | --- |
| SUM | Sum of active field rows | Active rows |
| COUNT | 0 | Active field rows >= threshold |
| Pair | Selected B sum | Selected rows |
| Points | Sum of admitted requests | Admitted request count, including duplicates |

An empty coarse-word vector means all blocks. Otherwise it has exactly
`ceil(blocks/64)` words, unused high bits zero, and one bit per original block.
An all-zero vector is the explicit empty mask. A point request to an inactive
block is excluded; a structurally out-of-range request is always refused.

`evaluate` validates both domain lengths, mask geometry, operation/field and
all point indices **before any value read**, then skips inactive blocks before
accessing A or B. It can therefore evaluate across inaccessible excluded full
blocks. No candidate halo is inferred. The functional guard check protects the
excluded page and exercises every operation, including points into that page;
separate structural-refusal checks protect all values.

`apply` validates the entire batch before any store, applies assignments in
input order and does not need old-value loads. `prepare_expected` validates the
whole trace before copying source values, then owns a current-row copy and
computes read answers outside candidate timing. Construction, read-list
acquisition and original/current snapshot copying require accessible original
input; the excluded-value no-access law concerns `evaluate`, not those setup
copies or quantile acquisition. Returned refusals use `std::expected`.

## Functional witness and current status

`tiny_witness()` covers a threshold crossing in A, replacement and repeated
assignment in B, restoration, a dictionary-novel value, original-reader answers,
all-zero masks and excluded blocks. Its hand answers for the first active block
are initial/restored SUM=360, COUNT=4, pair=170; after the first batch they are
SUM=514, COUNT=5, pair=354. Independent requests including duplicates consume
110 initially and 355 after that batch. Reversing the two B assignments changes
SUM to 513, pair to 353 and the point consumption to 353.

`check.cpp` adds high-bit/UINT64_MAX values whose sums exceed uint64; domain,
mask, event and edit refusals; all-or-nothing malformed-batch admission; generated
schedule/multiset invariants; novelty against original values; and protected
excluded pages. The optional targets are `frame_histories_check` and
`frame_histories_sanitize` (ASan/UBSan).

The parent-reviewed implementation passes **2,066 checks** in both optimized
and ASan/UBSan builds on local Linux AArch64, Clang 21.1.8, C++23,
`armv8-a+simd` / `neoverse-v2`. [The receipt](qualification.json) identifies
the exact sources and binaries. These are functional and access-boundary
checks, not a performance or multi-ISA qualification. No worker or download
was needed.

The substrate's `corrections_trace.cpp` consumes these same commands and direct
expected answers. Its candidate execution, correction representation, coarse
access behavior, dictionary coverage and retained backing have their own
qualification; a correct row oracle does not establish those properties.
