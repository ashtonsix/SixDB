# Curated run search improves real point consumers

The [timing-order audit](../ORDER-AUDIT.md) supersedes close rankings from this
historical cyclic-order cohort. Its calibrated rerun preserves the large gains
and the important losing cases; use that paired cohort for current timing claims.

Use the private `unpredictable_point_reader` or
`unpredictable_compact_reader` for unpredictable point work over run-heavy
images. Retain the ordinary aliases for predictable work. Both are instances
of one reader template; only the run-source type changes. There is no new
runtime profile field, and the maintained native source preserves its existing
projection fallback below sixteen runs. No Frame public API or default is
selected by this experiment.

Ikea HELPER first checked this substitution in the actual getters on Zen5
AVX2/AVX-512 and Neoverse V2, including value-dependent serial and eight-stream
requests. A collapsed low-entropy chain was rejected before choosing policy;
the corrected salted trace records its actual row coverage. Predictable
requests and non-RLE code-placement controls remain counterexamples to a blanket
replacement. The Intervals caller audit owns those native results; the table
below is a separate integration run on real columns in the local ARM VM.

Complete images, independent original-value points, native16 checkpoints,
all blocks selected, median ns/request:

| Column | Bound projection | Bound native runs | Shared native prefix + projection | Shared native prefix + native runs | Native FOR |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS | 23.89 | 17.45 | 23.89 | 16.39 | .81 |
| ResolutionWidth | 25.25 | 17.16 | 24.82 | 16.27 | .85 |
| EventTime | 8.34 | 7.94 | 7.88 | 7.46 | .91 |
| UserID | 31.99 | 23.07 | 32.34 | 21.54 | 1.42 |
| URLHash | 12.53 | 11.48 | 11.51 | 10.68 | 1.46 |
| ConnectTiming | 13.53 | 11.95 | 12.33 | 10.83 | .84 |
| RegionID | 29.81 | 20.68 | 28.91 | 19.45 | .91 |
| WatchID | 8.47 | 8.24 | 7.51 | 7.62 | 1.46 |
| ClientIP | 22.51 | 14.80 | 22.70 | 14.18 | .88 |
| Jena pressure | 21.05 | 20.44 | 11.05 | 10.59 | .45 |
| Jena temperature | 17.89 | 17.44 | 11.44 | 11.10 | .47 |

The five prominent run-heavy columns improve about 31–38% with the shared
native-prefix consumer. That is useful, but the large native-array gap remains.
The current program does not make arbitrary encoded point reads comparable to
plain-column access. Whole-operation encoded programs and plain forms remain
essential alternatives.

Keep the losses: WatchID is 1.4% slower with all blocks selected and 7.2% slower
under the grouped-block selection. Common-delta cells also move by a few percent
despite using the same residual algorithm. Static program choice changes linked
code placement; these movements are not run-search gains or a hardware cause
diagnosis. The [complete samples](evidence/curated-runs-local/summary.json) retain
all sixteen arms, all/none/alternating/grouped selections, whole-image and
matched-delta requests, native controls and the tiny fault input. Do not pool
them with the earlier [Intervals integration cohort](INTEGRATION.md).

The ordinary controls also moved between those two cohorts. Linked comparison
finds their admitted hot paths byte-identical to the previous executable:
1,016 bytes for the bound getter and 916 for the compact getter. Differences
are confined to cold assertion/abort addresses and strings; no instruction
normalisation was needed for that hot-path equality. Function placement and
surrounding benchmark work differ, so use the paired within-cohort comparison
above rather than subtracting a new time from an older run.

## Shared code, bytes and verification

The default aliases instantiate `point_reader_program<frame_encoding::run_ends>`
and `compact_reader_program<frame_encoding::run_ends>`; unpredictable aliases
substitute `ikea::intervals::run_end_source`. Width operations and dictionary
composition stay shared. Within either compact program, materialised/native
residual prefixes still use the same emitted getter and differ only at their
existing prefix leaf.

Images, logical coordinates and all original body bytes are identical. The new
programs borrow the existing native16 images rather than allocating more encoded
images. Prepared object sizes and footprint accounting are unchanged. Linked
compact getters have the same 80-byte frame; the bound getters retain their
existing decoded-prefix scratch. Four linked getter bodies, including those
prologues, are retained; the wider run leaf adds code, not per-image metadata.

Every original point on 59 columns passes through the five prepared readers and
four checkpoint options: 237,941,312 default comparisons, 237,972,032 with delta
exceptions, and 238,002,752 each with sorted/frequency dictionary modes. These
also exercise distinct/mixed dictionary editions and original coarse masks.
The existing exact-tail and protected-none checks remain. The timed cohort uses
the default dictionary-none material; these counts do not claim timing coverage
for optional dictionary modes.

Pinned Clang 21.1.8, O3/native ISA/generic tuning, automatic vectorisation,
Apple/OrbStack AArch64 VM, vCPU7. Sixteen rotated trials in each of two reversed
processes give 32 samples/cell. Each sample consumes four batches of 4,096
points and checks the full u128 result. Each arm warms its own requested batch;
this does not prove whole-image cache residence. No native-fleet, serial or cold
claim is made for this local cohort.

Source capture
`9ef15fa4649303193267d5287bbcd33f1664e8f947ffacf8284a32da5b386ce9`
uses frozen f760 material plus these curated readers. The later borrowed-block,
monotone COUNT, dictionary-identity and arrival-code experiments are separate.
[Recovery and identities](evidence/curated-runs-local/REPRODUCE.md) include exact
replacement sources, binary, raw results and linked getter assembly.
