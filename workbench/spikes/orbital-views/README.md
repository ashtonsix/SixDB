# Views for embedded Orbital

A spike covering Ashton's [October 5 design notes](../../notebook/orbital/ASHTON-SCRATCH.md),
with views as the main uncertainty. The [findings](FINDINGS.md) own the alternatives
and evidence. The [Orbital and Loom candidate](../../design/orbital/DESIGN.md)
consolidates this work with the other studies into types, TUs and caller examples.

The embedded performance target permits retained in-memory producer inputs and
epoch batches, with actual write maintenance included. The file provider tests
a stronger persistence guarantee. Mechanism timings do not establish LMDB parity.

| Code | Question exercised |
| --- | --- |
| [prototype](prototype.h), [probe](probe.cpp) | Exact part revisions, copy/COW/take, native access, stage/seal/install, worker masks. |
| [lifecycle probe](lifecycle_probe.cpp) | Empty creation, growth, live disjoint writers, cancelled candidates and one physical lifetime through reuse. |
| [acquisition](acquisition.h), [probe](acquisition_probe.cpp) | Sparse readiness, substitutable reads, failure and late completion after cancellation/replacement. |
| [embedded history](embedded.h), [probe](embedded_probe.cpp) | Object/part composition, journal outcomes, atomic head, layout changes and restart. |
| [recycling](recycling.h), [probe](recycling_probe.cpp) | Temperature hints, retained mapped homes, delayed uses and asynchronous cold reclaim. |
| [Ikea effects](ikea_probe.cpp) | Real packed writes, source resolution and stable post-write capture for publication. |
| [Recovered acquisition](embedded_acquisition_probe.cpp) | Object history selects a recoverable part revision, then substitutable ports prepare sparse native bytes. |
| [Semantic admission](systems/intent_probe.cpp) | Retained-memory epoch batches; durable file intent before native work; exact replay, maintenance, retry and checkpoint/suffix recovery. |
| [Raw binding](systems/binding_probe.cpp) | Standalone scratch growth, generic memory/file sources, native/backend lifetime and owner-settled readiness. |
| [Afterimage composition](systems/composition.cpp) | Real packed writes and coherent bytes/summary heads while actual derived writes stall, finish out of order or fail. |
| [Unit boundary](systems/unit_boundary.cpp) | Bec256 body effects alone cannot capture interpretation or summary changes. |

These are independently compiled research TUs. Owner operations are serialized;
native leases and backend requests retain actual backing. The original sealed
memfd sources, full journal snapshots and one admitted mutation are fixtures.
Semantic admission and raw binding close later composition gaps. No consensus,
WASM or Firecracker runtime is implemented. Earlier
[object composition](../orbital-objects/FINDINGS.md) and
[allocation geometry](../memory-descriptors/README.md) remain useful prior experiments.

The follow-up [design-space experiments](#run) compare publication and retained
material, ownership/metadata/lifetime, native bindings/residency, and backing
resources/journal service. Their main recommendations replace the original
fixture choices; detailed comparands remain with their code.

## Run

Checks only, using the pinned Linux toolchain on local AArch64:

```sh
orb -m ubuntu env SIXDB_MARCH=armv8.2-a SIXDB_TUNE=generic \
  bash workbench/spikes/orbital-views/check.sh
```

This builds the seven original probes and four later composition checks, saving
results in ignored storage without running performance comparisons.
The full comparison uses `bash workbench/spikes/orbital-views/explore.sh` on
Linux. It builds independent targets and saves each study separately. Ownership
cells require four allowed CPUs; do not globally pin this runner to one CPU.
For a captured Zen5 run, use `worker.py run` with `--machine zen5`, a
Spot `c8a.xlarge` or larger, and explicit `SIXDB_MARCH=x86-64-v4` /
`SIXDB_TUNE=zen5` environment arguments. The standalone study runners are useful
separately too.
CTest can rerun the original seven with `-R '^orbital_views_.*probe$'` in the
same build directory; the four later checks are standalone systems targets.

`run.sh` measures acquisition/native work/abort; `recycle.sh` measures raw
mapped-home reuse. Their retained [Zen5](evidence/zen5/README.md) and
[recycling](evidence/recycling/README.md) evidence describes earlier captured
sources. Later lifecycle extensions do not retroactively change those results.

`patterns.sh` compares repeated begin/store-64/stage/seal/install cycles. It
records phase costs and actual source/material bytes, so a cheap acquisition
cannot hide full-snapshot publication work. [Recommendations](FINDINGS.md#recommendations)
connect that comparison to the first-module design.
The [publication review](prior-publication.md) connects Calico, current Ikea
effects and earlier aggregate work; the [TLB study](tlb/README.md) measures
cross-worker mapping costs.
