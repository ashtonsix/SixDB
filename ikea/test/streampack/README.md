# StreamPack checks

`ikea_streampack_check` compares actual value histories with independent vectors,
checks each reserved byte's reverse owner, replays damage into old byte snapshots,
and verifies sparse/dense/range/native reads plus transfer and packing. The fixed
256-slot capacity, grouped/separated lengths, Absolute Ends/Sizes/Relative Ends, empty/fixed values and independent
omitted-fact combinations are covered.

`ikea_streampack_boundaries_check` places directory, partial-group lengths and
control against protected pages. It exercises actual 65535-byte reservations,
widened prefix carries, byte-limit boundaries 255/256/4095/65535/65536, preserving
write coverage, insufficient journal/capacity refusal, metadata/input aliases,
transfer journal/source overlap, count-only and extent-only changes.

`boundaries.cpp` also consumes and reproduces a literal byte fixture independently
of the writer/reader pair, including a second checkpoint, retired reservation,
empty ranks and partial-tail padding; corrupt geometry/U/count is refused. The
eight-byte schema fixtures cover every flag combination, fixed zero versus absent,
maximal bounds, noncanonical/legacy rejection and a protected-page boundary. A full
256 × 65,535-byte block checks rank 255, count 256, final-byte reverse ownership,
last tombstone and refusal of the next append; oversized transfer also preserves
bytes and effects.

`ikea_streampack_encodings_check` adds 1,056 cross-encoding histories with
independent R/U/end models, plus fixed/full/immortal omissions, tight extent
bounds, mixed carrier widths, wide-origin relative sources and live phase-state
checks. It exercises literal version 2 values converted to version 3 without
altering the original bytes, along with exact descriptor round trips.

All three targets and executable guides disable both compiler vectorizers. The
composition guide instantiates the scalar and native register carriers through
Ikea's real `preserve_none`/`musttail` chain. Run `ikea_validate`, or build these
targets and use `ctest --test-dir BUILD -R '^ikea_.*streampack'`.
Native worker checks, sanitizer runs, consumer assembly/performance comparisons
and owner integration evidence belong to the [original implementation study](../../../workbench/spikes/streampack/README.md)
and the [Boundlist extraction qualification](../../../workbench/spikes/boundlist/GRADUATION.md).
The [portfolio qualification](../../../workbench/spikes/boundlist/PORTFOLIO.md)
records the current codecs, independent U carriers, StreamPack-owned packing and
actual shared-prefix/tail-reclamation tests.
