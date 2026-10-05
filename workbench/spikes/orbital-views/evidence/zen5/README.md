# Zen5 view preparation, October 5, 2026

Job `20261005T102545Z-ce093969`: fresh Spot c8a.large, AMD EPYC 9R45,
Clang 21.1.8, Linux 7.0.0-1012-aws, 4 KiB pages, comparison pinned to CPU 0.
The instance had two vCPUs, one thread per core. It terminated after collection.
The medium instance was unavailable across offered zones; this comparison used
the next size. Earlier development runs remain in S3 and are not exported here.

## Comparison

All methods begin with the same prepared private cache, outside timing. Each
iteration begins a draft, borrows it, reads one byte from every prepared page,
writes 64 bytes, closes the borrower and aborts. Aborting discards the draft
mapping. The next iteration prepares its cache again outside timing.

Each cell is the median of six trial means, each with 120 iterations: three
forward and three reverse method orders. These are not latency percentiles.
Internal clock reads contribute to the measured phases; small figures should
be read as the scale of this path rather than precise lower bounds.

| Part / prepared pages | Method | Hardware protection | Acquisition | Full measured cycle |
| --- | --- | --- | ---: | ---: |
| 4 KiB / 1 | Copy | Off | 1.807 µs | 2.665 µs |
| 4 KiB / 1 | COW | Off | 1.103 µs | 2.882 µs |
| 4 KiB / 1 | Take | Off | 0.043 µs | 0.857 µs |
| 4 KiB / 1 | Take | On | 0.621 µs | 1.507 µs |
| 1 MiB / 1 | Take | Off | 0.042 µs | 1.407 µs |
| 1 MiB / 256 | Copy | Off | 327.447 µs | 347.388 µs |
| 1 MiB / 256 | COW | Off | 229.745 µs | 276.352 µs |
| 1 MiB / 256 | Take | Off | 4.395 µs | 24.234 µs |
| 1 MiB / 256 | Take | On | 113.685 µs | 133.501 µs |

Taking an existing allocation makes acquisition cheap. The one-page take cycle
spends about 0.79 µs discarding its mapping; acquisition alone does not explain
complete-operation cost. The checks also demonstrate retaining the allocation
through publication, which this abort comparison does not time.

For 256 pages, metadata traversal and per-page mapping/protection calls matter.
Production preparation should be able to operate on contiguous runs and retain
prepared windows. These figures do not rank optimized bulk-copy, mapping or UFFD
implementations.

## Accounting and limits

Copy records one explicit page copy per prepared page. COW records no explicit
copy, but incurs kernel faults and private-page copying; `minor_faults` records
faults, not bytes or disk I/O. Taking records no new mapping or explicit copy.
All source pages are resident. There is no timed disk/network retrieval,
durability, publication, growth, pressure or contention.

Every view reserves the whole part's address space. Unprepared pages remain
inaccessible. Preparing one page of a 1 MiB part avoids binding the others;
it does not establish a reduction in total RSS, since the fixture retains the
whole source in a memfd. Allocator metadata, source material and prepared cache
are not included in the explicit-copy counter.

## Checks and recovery

The native probe checks 120 mixed copy/COW/take histories against independently
updated bytes. Other cases cover revision intervals, held and late old readers,
concurrent disjoint stores, abort/reopen, sparse holes, immutable staged patches,
out-of-order completion, same-address promotion, unstaged-store rejection,
worker input/output masks, and stale results from aborted drafts. Native traps
test read-only and absent pages. The same probe passes locally under ASan/UBSan;
deliberate child protection faults use `ASAN_OPTIONS=handle_segv=0:handle_sigbus=0`.

At collection, all six executable/runner source files matched the worker's
captured manifest. The live spike has since added composed histories, growth,
sparse acquisition and pooling; these timings describe the archived sources.
Host context, complete source capture,
binaries and build logs are recoverable through [artifact.json](artifact.json).
[Comparisons](comparisons.csv) retain all 30 cells and phase/copy/fault counters;
the six raw CSVs permit independent regrouping. No timing cell was selected away.

Regenerate the summary with:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/orbital-views/evidence/zen5 build/reports/orbital-views
```

The [summary script](../../summarize.py) checks cell coverage and independently
expected explicit work before grouping trial means. [Provenance](provenance.json)
records the retained input hashes.
