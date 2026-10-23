# Compact scans have useful strengths; cold access is not a universal rescue

2026-10-23. The first controlled Zen 5 Frame/native/Polars comparison supports
choosing representations and programs for useful workloads. It does **not**
support a general cold-scan victory. Local clustering gives Frame a large COUNT
win in both conditions; cold contiguous selective consumption can reverse a
native advantage. Scattered cold consumption remains a substantial weakness.
Native forms belong among Frame's alternatives.

## What was actually compared

AMD EPYC 9R45, one pinned CPU, reported 16 MiB L3, Clang21.1.8 O3/znver5.
Actual Polars1.42.1 is single-threaded, with its native runtime hash retained.
Compare complete Python calls including scalar extraction, with a common C
boundary for Frame and native. The same 65,536 / 524,288 / 2,999,808 logical
rows enter every control. OS/ResolutionWidth retain their original Int16;
the other columns retain Int64. Native FOR subtracts the global minimum and
uses the narrowest byte lane. Actual Polars Series and optimized Lazy calls
are both present, including prepared FOR alternatives.

`warm-repeat` immediately repeats the same complete call. `flushed-image` uses
fenced CLFLUSH on actual input ranges: Frame payload/descriptors/parts/coded bits,
native value buffers, or zero-copy views of the actual Polars buffers. Prepared
selections and request metadata remain warm. Repetition does not make a 24 MB
scan fit a 16 MiB cache. These are conditioning experiments, not measured DRAM
traffic, memory-bus saturation, or proof of a serving cache level.

The second cohort adds the existing catalogue packed-leaf program on the same
images. Default images have no dictionaries or retained decoder bindings. No
codec or kernel changed. The two cohorts remain separate; the tables below use
the second only. Catalogue is a useful control, not a universal improvement:
EventTime COUNT improves, while URLHash COUNT becomes much slower.

## Complete full-image queries

Microseconds per complete query, 2,999,808 rows. Frame is the ordinary operation
except the explicitly marked catalogue COUNT. Polars columns show the faster
observed Series/Lazy control for that image, not an implemented selector.

| Query | Condition | Frame | Native FOR | Polars logical | Polars FOR |
| --- | --- | ---: | ---: | ---: | ---: |
| UserID COUNT >= median | repeated | 41.8 | 459.9 | 446.6 | 422.9 |
| UserID COUNT >= median | flushed | 52.4 | 541.7 | 562.8 | 566.4 |
| EventTime COUNT, catalogue | repeated | 226.5 | 219.8 | 461.0 | 196.4 |
| EventTime COUNT, catalogue | flushed | 270.2 | 280.1 | 569.1 | 292.6 |
| OS SUM | repeated | 474.8 | 59.3 | 98.7 | 98.4 |
| OS SUM | flushed | 482.9 | 81.2 | 158.1 | 102.8 |
| ResolutionWidth SUM | repeated | 535.0 | 61.3 | 96.6 | 97.9 |
| ResolutionWidth SUM | flushed | 552.5 | 149.2 | 154.4 | 154.0 |
| EventTime SUM | repeated | 445.0 | 187.4 | 380.2 | 834.0 |
| EventTime SUM | flushed | 454.8 | 269.0 | 533.4 | 901.7 |

UserID is locally clustered, not globally sorted. Its approximately tenfold
COUNT win is useful compressed-domain work/pruning, not evidence that every
compressed scan wins. EventTime cold COUNT is roughly at the compact controls'
level, although substantially faster than logical-Int64 Polars. The close cold
ordering does not select a winner. OS and ResolutionWidth SUM remain decisively
CPU-limited relative to the controls at this operating point: flushing adds
little to Frame's large runtime. Compression alone does not pay their CPU bill.

The [complete cells](evidence/zen5/catalogue/selected/cells.csv) retain every
column, size, operation, condition, program and duplicate. Wide UserID/URLHash/
WatchID exact SUM needs Int128 in Polars under the proven reduction bound;
Frame beats those Polars calls, but generally loses to the native exact wide
reduction. Do not present that Polars win as evidence Frame has the best body
program. Native wide SUM uses vectorisable 32-bit-half accumulators and returns
the exact wide result; no fixed-width overflow or floating-point approximation.

## Selective consumption: locality changes the answer

Select 732 of the 11,718 original blocks, then consume their actual SUM. The
contiguous prefix and block-scattered layouts select exactly the same logical
values, asserted by hash. Their encoded image byte counts also remain equal.
All operations receive original-block selections, not expanded fine-row masks.
Microseconds, same second cohort:

| UserID selected SUM | Frame ordinary | Native FOR | Best prepared Polars | Polars build spans + SUM |
| --- | ---: | ---: | ---: | ---: |
| Contiguous, repeated | 21.3 | 19.9 | 111.2 | 264.3 |
| Contiguous, flushed | 23.3 | 40.5 | 120.8 | 267.6 |
| Scattered, repeated | 25.1 | 27.6 | 254.9 | 824.1 |
| Scattered, flushed | 131.6 | 51.1 | 431.4 | 1055.4 |

The contiguous cold case demonstrates the benefit Ashton anticipated: Frame
has no clear repeated-query advantage and wins the flushed comparison against
the same strong native control. The scattered cold case demonstrates the limit. Low
output density alone does not guarantee cheap movement or enough parallel loads.
Frame is still much faster than the measured Polars consumption here, but the
native result shows substantial remaining headroom. No Polars point benchmark
is needed to establish that.

Prepared Polars uses the faster gather/span control, with actual payload
consumption; adjacent blocks coalesce into one zero-copy slice. The scattered
case has 732 chunks. Request-time span construction is included in its own arm;
index, mask and span preparation and retained bytes are separately reported in
[setup](evidence/zen5/catalogue/selected/setup.json). These supplied-mask results
do not include the work of evaluating predicate A to select payload B.

## Footprint and the strategic consequence

Logical image bytes at the largest population, excluding vector spare capacity
and object headers (those are separately retained):

| Column | Frame image | Native FOR values | Polars logical values |
| --- | ---: | ---: | ---: |
| OS | 1,043,219 | 2,999,808 | 5,999,616 |
| ResolutionWidth | 1,465,325 | 5,999,616 | 5,999,616 |
| EventTime | 6,328,853 | 11,999,232 | 23,998,464 |
| UserID | 5,474,121 | 23,998,464 | 23,998,464 |
| URLHash | 22,165,836 | 23,998,464 | 23,998,464 |
| WatchID | 23,436,000 | 23,998,464 | 23,998,464 |

The middle footprint regime exists: the UserID Frame image fits within reported
L3 while its native value buffer exceeds it. Both small-image and both-large
cases also exist in the sweep. This does not provide every regime for each
column: a larger real UserID slice is still needed for its both-large comparison.
Image size is not an actual query traffic counter, particularly for pruned COUNT.

The useful direction is compressed algebra/pruning and selected consumption,
with native representations for workloads where they are better. Two concrete
remaining costs deserve separate treatment: the existing run reduction CPU
path (encoding owner), and staging descriptor/body access for scattered cold
blocks. Address-dependent misses are a plausible explanation for the latter,
not a measured causal attribution yet. Catalogue alone did not repair it.
The next comparison should test that staging and predicate(A)->consume(B),
rather than use more hot-loop rankings to stand in for those workloads.

## Qualification and recovery

The catalogue cohort has 1281 independent checks across 21 images, 28,800
answer-checked calls and 144 executed schedules. A separate raw-record audit
verifies equal treatment positions and ordered predecessors in every workload
and condition. All 3494 captured source hashes match the worker source. The
exact shared image was copied before checks and hash-verified after timing;
both exact native images are retained.

Its 432 exact duplicate ratios have median1.000 and range0.944–1.086. Frame's
displayed UserID cold ordinary duplicates are within0.6%; large advantages/losses survive
the first independent cohort. Close ranks remain unqualified. The first cohort
has 18,432 calls/861 checks and its own full controls; it is not pooled with the
catalogue cohort. Local ARM's 3072-call pilot established semantics only.

[Retained evidence and the complete recovery bundle](evidence/zen5/artifact.json)
include both frozen sources, raw samples, exact shared images, actual Polars
runtime identity, input hashes/reference, flags and host. `report.py` derives
comparisons; `curate.py` independently audits the executed schedule and produces
the small CSV export. Neither output counts logical image bytes as memory traffic.
Point access and staged Loom integration remain separate active investigations;
no Frame API, default encoding or graduation is selected here.
