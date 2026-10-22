# Useful heads need a predicate law and a complete consumer

Both one- and two-byte SeriesPack heads now flow through a private Frame image
consumer. After FOR, the descriptor base translates the logical comparison
without reading the suffix. Ordinary and guarded programs consume identical
headed bytes. The two-byte program composes the maintained eight-bit guard twice.
Prediction residuals currently retain their explicit decode fallback; their
heads do not inherit the FOR predicate law.

The results validate the placement concern, not a universal head default. On
EventTime at its actual population median, early u64 heads tie in every tested
complete16 window. Late FOR head8 ties in only 5,177/124,283 windows: about 96%
are decided without the suffix. Head16 leaves just 43 unresolved among 124,129
eligible windows. This is a descriptive logical trace, not measured cache traffic.
Protected-plane checks separately prove the architectural skip.

OS and ResolutionWidth are counterexamples to selecting by final hit rate.
Their late first heads tie in 16,918/16,921 and 27,033/27,227 eligible windows.
Many OS bodies have no suffix after H8, and neither column has H16-eligible
FOR bodies in this query. The nominal H16 arm falls back to headless there;
small timing differences are not two-byte-head gains. URLHash's first/second
head ties fall to 8,065/59 among 166,053 windows, but avoiding suffixes still
does not beat the strongest native array consumer.

## Complete COUNT results

Local Apple/OrbStack AArch64 VM, Clang 21.1.8, release O3/native ISA/generic
tuning, automatic vectorisation enabled, vCPU7. Original rows and working-domain
exports are unchanged. ClickBench uses 2,999,808 complete rows; Jena uses 65,536.
Every actual result is consumed and checked against a native original-value loop.
Two processes reverse input/arm order; seven rotated trials per process retain
all fourteen samples. One arm runs at a time; conversion is outside query timing.
An earlier wrapper cohort ran separately and is not paired with these timings.

All blocks selected, `COUNT(value >= population median)`, ns per value:

| Column | Existing decode control | Direct headless | Late H8 guarded | Late H16 guarded | Polars u64 | Polars native FOR |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OS | 0.427 | 0.144 | 0.140 | 0.143¹ | 0.177 | 0.038 |
| ResolutionWidth | 0.645 | 0.169 | 0.170 | 0.163¹ | 0.187 | 0.063 |
| EventTime | 0.312 | 0.197 | 0.191 | 0.199 | 0.166 | 0.087 |
| UserID | 0.155 | 0.015 | 0.011 | 0.009 | 0.164 | 0.176 |
| URLHash | 0.723 | 0.361 | 0.476 | 0.356 | 0.193 | 0.180 |
| ConnectTiming | 0.028 | 0.088 | 0.086 | 0.083 | 0.162 | 0.0002² |
| Jena pressure | 0.739 | 0.792 | 0.798 | 0.794 | 0.170 | 0.106 |
| Jena temperature | 0.503 | 0.505 | 0.500 | 0.507 | 0.161 | 0.101 |

¹ Headless fallback, not an H16 result. ² Median zero is an all-hit query; global
FOR bounds answer it from metadata. This is timer-dominated O(1) work, not a
scan rate. UserID is sorted: nearly all blocks are settled without run scans.
Its result cannot be generalised to shuffled IDs or other threshold distributions.

Polars is the actual installed 1.42.1 library, pool limited to one thread and the
same CPU, on the exact input values. Its u64 arm is direct; native FOR separately
subtracts the global base and stores the smallest native unsigned lane. Casting
and preparation are outside both query timers. Its FOR control retains the same
out-of-domain/all-hit shortcuts as the C++ native FOR control. COUNT has no
overflow-law mismatch. Each peer query has fourteen alternating samples; the
retained identity and raw results include thresholds, answers, lanes and versions.
Polars currently covers the all-mask queries; native C++ controls additionally
cover alternating coarse blocks. No gathering cost is silently removed from a
Polars coarse-selection comparison.

The table exposes remaining losses. Native mask-weighted run reductions and
removing wrapper costs cut OS/Resolution by roughly 3×/4×. They still trail
Polars's narrow lanes by roughly 3.7×/2.6×. Wide high-entropy URLHash also trails
native arrays. Conversely, sorted UserID's block decisions have a large useful
advantage. The new path loses ConnectTiming because its patched fallback restores
more than the existing count operation needs. Reuse of an owner block-count
operation can close that seam without inventing a second patch algorithm.

## What code inspection changed

The first complete consumer called a 2 KB scratch-frame lambda on every block,
then a runtime predicate dispatcher and another function for each window. The
accepted consumer inlines the block visitor, so the scratch frame is set up
once per query, and invokes a typed complete COUNT once per block. Packed bodies
popcount native masks; run bodies weight complete16 masks with native byte-length
reductions. A bounded scalar tail handles endpoints whose final window is not
readable in full. No row expansion or u64 decoded run-value array is needed for
these bodies. Generic five-predicate mask operations remain available separately.

The linked whole consumer and its called K32 H8/H16 count bodies were inspected.
The latter retain the two guarded head decisions before suffix loads; the native
run reduction follows those predicates. The complete query still pays descriptor,
format lookup, indirect block dispatch and fallback costs. This is not evidence
of perfect inlining, a code-size optimum or ISA-wide policy. The first wrapper
cohort is archived as a diagnostic, not selected as the only comparison baseline.

## Limits and next discriminating work

There is no universal head or guard recommendation yet. The measured late cut
is appropriate for independently translated FOR/run predicates. Ordinary and
guarded choices remain distinct; the [prior native investigation](../../packed-filters/EARLY-GRADUATION.md)
already shows narrow and mixed-branch losses. Fleet hot/cold complete consumers,
issued traffic and staged acquisition remain necessary before choosing policy.

For predicted bodies, compare query evidence independent of the residual codec
with splitting logical high/low values before predicting the low part. As a
diagnostic, exact logical block min/max would settle 60.2%/63.3% of blocks in
pressure/temperature median queries, at 16 bytes per block (0.5 bits/value); it would
settle almost no URLHash blocks. Finer logical-head bounds might cost less than
a duplicated per-value head. Neither alternative is implemented or selected here.
Its unresolved windows need checkpoint-restarted reconstruction, and writes must
repair the dependent evidence. Claude is considering this with encoding work.

The migration instrument retains the original image plus converted planes and
16 bytes of new addressing metadata per block; conversion timings and all extra
split bytes are recorded. It establishes neither final resident memory nor an
in-place migration format. Native/plain representation choices, whole-operation
cost and retained prepared state must remain in the decision.

## Correctness and identity

727,520 exact checks cover all 64 widths, five predicates, H0/H8/H16, ordinary and
guarded operations, fused weighted counts, both transform cuts, actual corpora,
both run geometries, coarse masks and partial windows. Protected tests cover first-
and second-head rejection, an exact seventeen-byte endpoint tail, a none mask
with inaccessible column metadata, and four deliberately faulty live reads.
This proves architectural access obligations, not speculative cache behaviour.

The accepted source capture is
`2d26284fff5ccfe9af3fcb0e1d3429e422b8706e8848cf1ff10dd5aa5935ce82`;
input/source/ELF hashes, complete samples, controls and disassembly accompany the
[retained samples](../evidence/heads-points-local/filter/summary.json),
[actual Polars controls](../evidence/heads-points-local/filter/polars.json) and
[verified recovery bundle](../evidence/heads-points-local/artifact.json).
It is local warm research; no DRAM, Loom or fleet win is claimed.

## Logical evidence follow-up: checked, not yet timed

The next implementation adds part-wide logical H8/H16 sidecars over unchanged
encoded bodies, plus conservative byte-quantised min/max for each sixteen-row
window. Both interpret the logical predicate independently of prediction,
exceptions or the body's FOR anchor. Any unresolved block is decoded once;
only its unresolved rows contribute through the exact fallback. Part heads may
lose entropy relative to block heads; bounds may leave a whole window unresolved.
Those costs are explicit, not claimed to disappear through selective decoding.

All duplicated evidence is charged: one/two byte planes per original row plus
part records, or 56 bytes per block for bounds including bases/ranges/alignment.
No residency/default decision follows. The regular FOR-head image also reuses
the owner's block-count operation for fallback kinds, removing its unnecessary
complete-decode requirement on patched blocks. The earlier timing table is still
the qualified historical cohort, not a timing of this changed implementation.

767,200 exact checks pass with these consumers, all widths1..64, both historical
run geometries, default and opt-in prediction-exception bodies, multiple/partial
parts and the nine retained real columns. Independent original-value comparisons
cover the whole query; protected-column checks show that resolved heads/bounds
can finish while all encoded-column metadata is inaccessible. Three deliberately
unresolved sidecar queries fault against that same protection. None masks avoid
all column/evidence traversal. These are architectural accesses and correctness,
not measured cache traffic, native fleet timings or write-maintenance results.
