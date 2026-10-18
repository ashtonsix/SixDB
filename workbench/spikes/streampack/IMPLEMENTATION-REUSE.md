# Reusing encoded Parts across holder updates

The [consumer](implementation_orbital_reuse.cpp) uses production StreamPack and
the existing five-Part material: payload, directory, lengths, control and an
application holder map. Updating a holder changes a logical reference. It needs
publication at a new logical version, not equivalent-representation adoption.
Only the holder Part changes; the encoded StreamPack planes remain immutable.

## Local cost indication, 2026-10-17

The [retained selection](evidence/part-reuse-local/provenance.json) includes all
three policies, both source placements and both geometries, with setup, three
read controls and both teardown phases. Full raw runs, code-generation controls,
exact captured sources and the measured binary are in its S3 bundle.

Median process-mean publication time in ns, five fresh processes:

| Geometry | Initial backing | Copy all | Protected selection | Escaping Parts |
| --- | --- | ---: | ---: | ---: |
| Direct | Grouped | 1153 | 904 | 1369 |
| Direct | Separate | 1167 | 779 | 974 |
| Width | Grouped | 1262 | 1096 | 1084 |
| Width | Separate | 1217 | 708 | 971 |

Logical copied bytes fall from 6184 (Direct) / 5916 (Width) to 1024, and fresh
Parts from five to one. Protected selection avoids four temporary exports and
reduces measured publication means by 13–42% here. Escaping Parts lose to copying
in the Direct/grouped case: export only when the lifetime actually requires it.
Grouped backing can remain live despite selecting just one plane.

These are local aarch64 OrbStack Linux measurements on CPU 4 with Clang 21.1.8,
193 rows and 256 publications per process. Policies/cases use a fixed order;
the full oracle between rounds warms data. All rounds are retained. This is a
small-update cost indication, not native hardware qualification or database
latency/throughput evidence. The distinct hot binding control exposed an extra
mixed-layout branch cost; cached address-slot spacing removed it without growing
the existing Representation, Part or View. Its raw alternatives and final-code
identity check are archived with this comparison.

## Comparison and checks

The three arms are exact copying of all five admitted extents, retention through
one protected selection, and retention through four escaped Parts. Each arm
copies and edits the complete 1 KiB holder map, captures the resulting material,
prepares/installs it and performs the same bounded maintenance. Export creation
and disposal are timed in the escaped-Part arm. There is no per-point Hold or
export. The full-copy arm does not inherit the older Draft's extra full-capacity
zeroing and linear validation, so this is a new matched comparison.

All destination Parts are independently owned. The initial source is grouped or
separate; retaining a Part of grouped native storage can keep its whole physical
Allocation alive. Part reuse avoids copying but promises no sub-allocation page
refund. This exercise does not implement arbitrary byte-granular COW or shared
mutation. In particular, checked StreamPack replacement writes used lengths even
when length is unchanged; it cannot simply retain that plane.

`--check` covers Direct/Width, both source placements, both escape policies,
refusal with intact fresh Allocations, stale publication, old/late readers,
eight successive generations and import into a different Store/resource with a
separately owned format. Original catalog metadata must settle before the
imported owner dies, exposing hidden predecessor-owner/proof retention.

The CSV retains each complete publication interval and separate setup, read and
teardown rows. Application inputs, full content/holder validation, independent
oracles, statistics and formatting stay outside publication/read clocks on all
arms. The checks between rounds warm data: this is not cold-cache evidence.
`copied_bytes` is a logical count, not measured memory traffic; Pool observations
are not RSS and exclude catalog metadata, reported separately. Pool fields are
blank after physical teardown, when no Pool remains to inspect. Shutdown must
settle all allocation charge. Add setup/teardown when describing lifecycle cost.

Read controls use one coherent selection for the entire query run, one per batch,
or one per point. Each consumes full returned bytes through the holder map; an
independent expected digest is checked afterward. These are all-hit local owner
reads, not a prescribed Engine ABI or database comparison. Per-operation wall
times include clock overhead; no adjusted latency or percentile is inferred.

## Run

```sh
cmake -S . -B build/streampack-reuse -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=streampack -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/streampack-reuse --target streampack_implementation_orbital_reuse -j2
build/streampack-reuse/workbench/spikes/streampack/streampack_implementation_orbital_reuse --check
build/streampack-reuse/workbench/spikes/streampack/streampack_implementation_orbital_reuse \
  --bench --cpu 4 --rows 193 --rounds 256 --queries 65537 --batch 257
```

Use the pinned Linux compiler and an allowed CPU; prefix commands with
`orb -m ubuntu` from the Mac. This target needs no optional DuckDB/StringZilla
inputs. Its material TU uses the public instantiation opt-ins; ordinary workload
code uses declarations only. Module/source changes need fresh source
and binary identities before retaining native comparisons.
