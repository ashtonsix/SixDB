# Ordinary short-region stores on GNR

An independent ordinary materialization seam exists: splitting full AVX512
stores inside the existing private `finish_region` improves all 15 affected
resident get16 cases by 7.6–54.0%, without importing the general expression
driver. The source change adds eight lines, changes 57 existing native bodies
and adds 1102 bytes of executable text.

This is a promising small GNR candidate with mixed whole-call costs, held in
Workbench under the current experimental scope. Several scalar short/tail calls
into the changed containing function regress materially, although they never
execute the split stores. Earlier Zen alignment-sensitive losses also leave
an all-target policy unestablished. The result supports the smaller implementation seam,
not a width whitelist, blanket bulk-store replacement or a universal latency.

## Matched comparison and code

GNR Spot job `20260911T073014Z-4337cc09` uses capture 604 files, digest
`7cf45e7a631f24904a327826e7f604e4448a9fa3f53d9c7af8bc550457d00fe0`.
All 601 original current-baseline files are unchanged. The
[driver](store-candidate/README.md) restores the verified original GNR library,
reconstructs all 12 common benchmark/check objects to their recorded hashes,
and reproduces the original benchmark byte for byte. Only native.cpp is
recompiled and only that archive member changes; production source stays intact.

The helper retains original coordinates, ZMM reconstruction, source reads,
independent placement and exact output extent/alignment. Each 64-byte output
store becomes a low-half YMM store and a high-half `vextracti64x4` memory store,
with an empty compiler barrier preventing remerging. The barrier's scheduling
effects are part of this experiment. Native register consumers are untouched.

The independent object audit finds 3558 common functions, with exactly 57
changed byte/relocation bodies: 21 AVX512 Local complete helpers and 36 AVX512
Striped boundary helpers. All 1779 AVX2-named bodies are identical; no functions
are added or removed. The 57 changed functions contain no calls before or after.
Selected actual assembly confirms the split stores and retained ZMM expansion.
Local1's complete helper stays 754 bytes, including unchanged ZMM bulk stores;
Striped5's boundary helper grows 581 → 597 bytes.

Native text grows 2680520 → 2681622 bytes; the complete production library
3926768 → 3927870. Read-only data, relro, data and unwind sizes are unchanged. The
linked benchmark grows 1104 text bytes. Candidate native compilation takes
68.60 seconds; the original single-TU compile was not timed separately, so this
is an observed scope, not a compile-speed delta.

Both variants pass scalar/AVX2/AVX512 public-wire checks: per target, 206
descriptions, 5532 placements, 83787 range reads, 49140 mutations and 206 append
scenarios. Each also passes 3737352 exact guarded ranges per target. These
checks and the original fixtures preserve exact output permissions.

## Ordinary gains and remaining losses

The existing benchmark supplies 928 cases: 542 bulk u64 decodes/controls,
134 resident point/get16 controls, 68 runtime ranges, 84 boundary cases and
100 independent head-placement **encode** controls. Long dense headed decodes
are included in bulk; independent gapped read timings belong to runtime/boundary
cases, not an all-placement long-decode matrix. Output contexts remain distinct:
resident get16 is aligned to 64 bytes, boundary output is 8 modulo 64, and runtime output
has natural carrier alignment without recorded address residues.

Order is base/candidate/candidate/base, three sequential repetitions with a
30 ms minimum per case on CPU0: 11136 individual timings. Full x86 feature
ceiling and original callers are fixed. Background result uploads are off;
the normal Spot interruption observer remains part of both variants' environment.
The [paired table](evidence/delivery-store-20260911/paired.csv) keeps every sample,
including unchanged controls. Separation is not a confidence interval.

| Ordinary call | Base → candidate, CPU ns/query |
| --- | ---: |
| Local1 get16 |7.379 → 3.689|
| Local5 get16 |7.418 → 3.410|
| Striped1 get16 |7.396 → 3.490|
| Striped5 get16 |8.317 → 7.270|
| Striped6 get16 |7.867 → 7.271|
| Runtime Striped12 origin16/count16 |7.296 → 4.219|
| Runtime gapped Striped28/H16 origin16/count16 |7.878 → 5.331|
| Boundary Striped12 count8 |7.296 → 3.368|
| Boundary Striped5 count16 |8.196 → 4.314|

All 15 affected resident get16 cases have separated improvements and agree at
both ordering edges. Local12 and Local56 dense complete controls, outside this
store seam, overlap at 7.653 → 7.571 and 7.252 → 7.142 ns. This smaller change
does not resolve their roughly 7 ns ordinary cost.

The boundary losses are significant: Striped5 short3 rises 6.523 → 7.705 ns
(+18.1%), Striped5 last-lane2 rises 5.821 → 6.707 (+15.2%), and gapped
Striped21/H16 last-lane2 rises 6.004 → 6.735 (+12.2%). They agree at both
ordering edges. These requests have fewer than eight remaining values and
skip both full-store branches, entering the scalar residual loop. Their
containing function and executable context change; these are whole-call
regressions, not measured penalties for executing the split store. No particular
branch predictor or cache mechanism is established.

Runtime gapped Striped28/H16 origin17/count17 also loses 14.107 → 14.911 ns
(+5.7%), with unequal +0.7%/+11.0% ordering edges. Five other runtime losses
are unchanged u32/AVX2 bodies and at most 1.25%. They remain in the record.

Long bulk mostly overlaps: 521 of 542 cases overlap, 16 improve and five lose.
The largest bulk losses are unchanged Calico controls around 2.7%, unchanged
AVX2 Striped3 at 2.2%, and unchanged AVX512 Local55/H16 at 0.64%. Construction
controls also move: unchanged AVX2 Local56/H16 dense encode is 7.6% slower,
while unchanged Calico Local1/Local7 point controls lose 19.0%/15.8%. These
observations cannot be subtracted as one normalization factor or attributed to
the changed store execution. The full code audit separates changed bodies from
these controls; source identity does not make their linked timing immutable.

## Disposition

This resolves the feasibility question raised by the
[diagnostic GNR store result](short-stores.md): the ordinary reader has
a small existing seam, and its whole-call gains survive real dispatch and
consumption. It is substantially smaller than the
[rejected general materializer](integration.md). Its large gains and small maintenance cost are meaningful adoption evidence;
the short-call losses are a tradeoff, not an automatic rejection gate. The
current measurement does not establish an all-target policy, and the
[earlier Zen offset results](alignment.md#splitting-count16u64-full-stores)
remain separate contrary evidence. Keep this promising candidate in Workbench for a later adoption decision
under the relevant caller and target costs. No new traversal, format exception or additional matrix is warranted
by this campaign; the [overall delivery assessment](../ikea-composition/seriespack-assessment.md)
keeps these limits alongside the implemented component and composition results.
