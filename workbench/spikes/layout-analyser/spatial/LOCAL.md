# Local validation, 2026-09-13

The compact spatial fixture is ready for coordinated server runs. No EC2 worker
was launched for this contribution, and no server timing result is established.

Clang 21.1.8 built the `layout_spatial_probe` target through the captured,
incremental CMake runner. The native checks passed 3,240 combinations of
layout, base phase, row count, order, compute, access mode, concurrency and
software prefetch. Every generated word/cycle link is checked, and an independent
byte enumeration checks 64B/128B line-demand accounting. The Python checks also
passed CSV counters/checksums, optional CLI paths, placement receipts and invalid
geometry refusals. AddressSanitizer and UndefinedBehaviorSanitizer passed the
same native checks on the captured source.

The final smoke ran 216 samples: three layouts, three extension modes, three K
values, two seeds, two base phases and two footprints. All results matched the
logical oracle. Every recorded batch reached its requested 1 ms duration.
Construction/check/calibration time is outside its steady-state ns/op values.
Its complete captured run is in ignored storage:

```text
build/experiments/layout-spatial/local-verified
source digest: 5b7506b84127802af59f4ac553315d0dcaa3b046046df518648f835c5a3e9e4f
probe.cpp SHA-256: 40620819c3bdf76f731ad908f1d4bb7f01a5701826330c5bb791c82c1a7913da
binary SHA-256: 05fac8f4ad9431b8ed30d6ce8df5c909e1c6a31351421ecb19dcd68bf108b0c2
```

This ARM Linux VM reports 128B lines and a largest cache of 64 MiB. Its 128-row
and 8,192-row controls are small relative to that cache; neither establishes a
DRAM comparison. Page receipts show base mappings. The VM has no
`/proc/self/numa_maps`, now explicitly recorded as unavailable. An initial check
incorrectly required that receipt and was corrected before the final run.
Other tasks could contend for local CPU during this smoke, so no layout ranking
or clean performance comparison is retained from it.

The screen's automatic logical footprint is capped at 512 MiB. In particular,
the previous Granite Rapids host reported a 480 MiB LLC: a 512 MiB logical
fixture has only about 341 MiB of split core bytes. The plan records this ratio
and does not claim a beyond-LLC case. A coordinated run can use `--large-mib 1024`
for the same larger row count across hosts (about 683 MiB of split core bytes),
still interpreting measured controls rather than declaring pure DRAM residency.
One placement is live at a time; the largest padded mapping plus temporary
permutation is approximately 17/12 of the logical footprint, before small
runtime/page-rounding overheads. At 1 GiB logical that is about 1.42 GiB.

The full screen contains 1,296 timed batches, at least 6.48 seconds at the
default duration before calibration, full-pass effects, initialization and
validation. Large dependent passes can substantially exceed 5 ms. Its wall
duration has not been measured on a server; allow minutes, not the nominal
timing sum. Fewer phases/repetitions provide an explicit smaller first pass.
