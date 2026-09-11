# Ordinary reader integration

Neither measured integration is retained. The expression-consuming range body
improves many shifted requests. Separating short traversal from the mixed
complete/edge callback recovers much of the complete-request loss, but leaves
broad suffix losses and several short-range regressions. Correct output alone
does not justify either version's performance and maintenance cost.

## Frozen comparison

Zen job `20260910T234310Z-87cf647d` uses capture
`d1ef981446324ab387e2eae36032470a419cc2a78b26cbe94d3788063aafbc54`.
The baseline is freshly built current source, not the older cached runtime
program used by the [earlier expression probes](edges.md).
Both variants use identical benchmark, facade, view, comparator and check
objects. Only `native.cpp.o` is replaced in the three-member library archive;
all four original programs relink byte-identically before comparison.

The isolated candidate covers Local payload widths 1–7 and Striped payload 12,
each with H0/H8/H16 and u32/u64 output: 24 descriptions. Work lanes follow the
actual expression's bit domain. Each child resolves its own placement/stride;
the named source remains borrowed, and stores use original positions within
the exact requested output. The Count1 scalar partial store and Count16/u32
two-part store from the [sink investigation](alignment.md)
are included. The unconditional u64 store split is excluded.

The first policy sends four or more complete Local tiles, or at least one
complete Striped tile, through the existing complete leaf, with expression
reads for prefix/suffix. Smaller requests use the expression traversal.
This is an experimental dispatch choice, not a size limit or public contract.

Both builds pass the public wire checks for all 206 descriptions on scalar,
AVX2 and AVX512: 5,532 placements, 83,787 range reads, 49,140 mutations and
206 append scenarios per target. They also pass 3,737,352 exact guarded ranges
per target. The fresh runtime/transition harness checks all 4,096 independent
query outputs and the actual timed output pointer before measuring. The
independent Python audit checks every recorded query hash, checksum and stride.

The [retained evidence](evidence/ordinary-materialize-20260910/20260910T234310Z-87cf647d/provenance.json)
contains 17,220 timing samples and records 2,870 separate preflight samples.
The four blocks are before/after/after/before, each case has three sequential
30 ms repetitions, and measurement runs on CPU0 with background uploads off.
Both labelled x86 families use the full v4/VBMI/VBMI2/GFNI ceiling; this is not
pure AVX2 timing evidence. An earlier launch with periodic uploads enabled was
canceled during its build; it produced no timing files and is excluded.

## What changed in performance

"Separated" means all six candidate samples lie on one side of all six baseline
samples; it is not a confidence interval. Every case and both ordering edges
remain in the [paired table](evidence/ordinary-materialize-20260910/20260910T234310Z-87cf647d/paired.csv).

| Scope | Cases | Separated wins | Separated losses | Overlap |
| --- | ---: | ---: | ---: | ---: |
| Short runtime ranges | 68 | 54 | 13 | 1 |
| Dispatch transitions | 1,152 | 759 | 380 | 13 |
| 8,192-value bulk/control | 110 | 18 | 28 | 64 |
| Fixed16/u64, 4KiB encoded resident/control | 67 | 14 | 22 | 31 |
| 1,048,576-value bulk/control | 38 | 5 | 4 | 29 |

The short traversal has useful gains: AVX2 Striped12/u32 origin17/count16
falls from 8.781 to 3.329 CPU ns/query. The aligned origin16/count16 case
instead rises from 2.814 to 3.533. AVX512 Local7/u32 origin1/count17 rises
from 9.988 to 11.456. These losses cannot be waived by the winning cases.

Independent shape classification finds 178 separated losses among 192 pure
complete-run transition cases, versus 96/96 wins for prefix-plus-coarse cases
and 96/96 wins for prefix-plus-region cases without a suffix. Each transition
case fixes its origin/count pattern while varying tile addresses; it does not
measure rapid switching across the threshold. Runtime/transition outputs use
natural carrier alignment and do not record their address modulo64. The
recurring resident caller explicitly aligns its output to 64; the earlier
controlled-address results cannot explain unrecorded addresses here.

Controls also move. Unchanged AVX2 Local6/u8 encode is 9.8% slower at 8,192 values
and 8.2% slower at 1,048,576. The latter scope also has a 6.1% loss in affected
AVX512 Striped28/H16/u64 decode. These remain recorded observations; the
control movements are not subtracted from affected cases.

## Code and maintenance cost

Worker-native text grows from 3,051,595 to 3,187,075 bytes: +135,480, or 4.44%.
Read-only data falls from 144,496 to 143,568 bytes. The aggregate hides a larger
change inside ordinary callbacks: `decode_bound` text grows from 58,010 to
258,890 bytes, about4.46×. Ninety-six old boundary/edge instances disappear,
and complete-leaf text falls by 22,501 bytes. The candidate adds571 lines in
four private headers and changes the native source by+33/−2 lines.

The actual AVX512 Local7 callback previously began with register operations
and tail jumps to existing leaves. The candidate starts by pushing seven
registers before selecting the output width. Its complete branch calls the
leaf and then checks the suffix, even when no edge work is needed. Local
inspection of all 48 affected callbacks finds no SIMD stack spills and two
direct calls per callback, both to the complete leaf. Scalar preservation and
loss of tail calls therefore matter to investigate even without SIMD spills;
this is evidence consistent with the timing pattern, not a cycle attribution.

The incremental candidate native compilation takes 69.41 seconds on the worker.
The original native compilation was not timed separately; this is not a
compile-time delta. The retained
[maintenance accounting](evidence/ordinary-materialize-20260910/20260910T234310Z-87cf647d/code-maintenance.json)
includes source, complete symbol families, text/data and compiler arguments.

## Separate short and mixed callbacks

Job `20260911T002159Z-284ae6d2`, capture
`c2229fcd3a24ded7245bc018ed2ab73b55d342d57685246edb09d42a92b45ae2`,
reuses the original fresh baseline on the same Zen worker. Its receipt pins
the original sources, compile database, common objects, archives and binaries;
the baseline relinks byte-identically. It does not use the first candidate as
its baseline. The same native guards, independent query audit, 1,435-case
inventory, 17,220 measured samples and 2,870 preflights all pass.

This revision restores the existing path for requests whose two endpoints
are physical-tile aligned. The eligible ordinary callback tail-dispatches to
a short-only traversal or a mixed/coarse callback. The four expression/window/
sink headers remain byte-identical, as do the Local4/Striped1 coarse threshold
and scalar callback ABI. A cheap size check avoids computing the complete
region for clearly short requests. No format whitelist is introduced.

| Scope | Cases | Separated wins | Separated losses | Overlap |
| --- | ---: | ---: | ---: | ---: |
| Short runtime ranges | 68 | 54 | 13 | 1 |
| Dispatch transitions | 1,152 | 775 | 263 | 114 |
| 8,192-value bulk/control | 110 | 5 | 28 | 77 |
| Fixed16/u64, 4KiB encoded resident/control | 67 | 20 | 13 | 34 |
| 1,048,576-value bulk/control | 38 | 3 | 9 | 26 |

The [paired results](evidence/ordinary-materialize-20260910/20260911T002159Z-284ae6d2/paired.csv)
and [shape classification](evidence/ordinary-materialize-20260910/20260911T002159Z-284ae6d2/transition-shapes.json)
show what the separation recovers and what remains:

| Transition shape | Cases | Separated wins | Separated losses | Median candidate/baseline |
| --- | ---: | ---: | ---: | ---: |
| Complete request | 192 | 71 | 37 | 0.994 |
| Complete/coarse run, then suffix | 288 | 74 | 187 | 1.069 |
| Prefix, then smaller complete region | 96 | 96 | 0 | 0.600 |
| Prefix, then coarse run | 96 | 96 | 0 | 0.731 |

All84 Local origin0/count33 cases and all 12 Striped origin0/count65 cases
lose: the troublesome shape is a cheap complete run followed by one value,
not a small set of widths. For example, AVX2 Striped12/u32 origin0/count65
rises from 6.585 to 10.105 ns/query. The short helper still loses on AVX512
Local7 origin1/count17: u32 rises from 10.008 to 11.672, and u64 from 10.009 to
12.227. Aligned16 Striped cases also remain among the runtime losses.

The worker code-generation gate, independently regenerated from the retained
native object, checks all 48 eligible ordinary dispatchers and all 48 short
callbacks. Dispatchers have no pushes or calls; short callbacks have no calls
or RSP-relative SIMD traffic. This recovers the intended structure, but does
not establish adequate performance. The mixed Local7 callbacks still preserve
state across the complete call and reload vector constants to process their
suffix. Their general suffix traversal also retains branches that the caller's
known clipped extent could make unnecessary. These are structural observations,
not a measured cycle decomposition.

Native text grows to 3,207,622 bytes, +156,027 or 5.11% over the baseline, and
read-only data grows to 149,152 bytes. Compact dispatchers do not mean less
total code: this version is larger than the first. The
[worker-derived accounting](evidence/ordinary-materialize-20260910/20260911T002159Z-284ae6d2/code-maintenance.json)
records ordinary dispatchers at 56,032 bytes, new short callbacks at 65,751,
and new mixed callbacks at 134,937. The source adds the same571 private-header
lines and changes native.cpp by+71/−3 lines. Incremental native compilation
takes 69.80 seconds, again without a separately timed baseline native compile.

Controls continue to move: unchanged Calico Striped12/u64 large decode is
10.7% slower, alongside a13.5% loss in affected AVX512 Striped28/H16/u64
large decode. The unchanged baseline Local1 fixed16 time also differs between
the two runs. Keep the raw ordering and address context; neither subtract
control movements nor treat these timings as context-free constants.

## Decision and remaining gaps

The isolated integration remains rejected. A bounded local full-x86
code-generation check moves the disjoint prefix/suffix before the complete
run: all 48 mixed callbacks then have zero direct calls, versus 96 total before.
Local7 AVX2/AVX512 preserve six registers instead of seven, but retain their
general edge traversal; native text grows another1,030 bytes. The unchanged
dispatcher/short-callback gates still pass. This is compile/code-generation
evidence only, with no new correctness or hardware performance claim, and
does not justify another complete timing sweep by itself.

This scheduling freedom belongs to this admitted, pure ordinary decoder;
it is not an ordering contract for arbitrary effectful composition. It cannot
cure the independent short-path losses. Additional evaluator variants or
format exceptions need more than marginal wins to justify their permanent
cost. The next measurement returns to the shared H16/u64 head-projection
question, whose older whole-layout encode gaps are materially larger, while
retaining this structural lesson for later integration work.

Each run now retains a baseline-only prior comparison separately from its
candidate comparison. The
[first baseline](evidence/ordinary-materialize-20260910/20260910T234310Z-87cf647d/before-prior.csv)
and [second baseline](evidence/ordinary-materialize-20260910/20260911T002159Z-284ae6d2/before-prior.csv)
use the fastest available Ikea family and fastest retained prior per fixed16
case. The second run observes Striped5 at 5.834 versus ScanPack2.162 ns/query
(2.70×, same wire), Striped6 at 5.549 versus Calico2.002 (2.77×, different
representation), and Striped3 at 4.568 versus Calico2.013 (2.27×, different
representation). These larger gaps remain outside the current integration.
The `after-prior.csv` tables describe uninstalled candidates, not live-reader
performance. The campaign remains open; rejecting this integration does not
establish that its underlying expression model or the current reader is final.
