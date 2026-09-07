# Calico reference map

Initial local index, 2026-09-07. Links assume sibling `calico`, `calico-ui`, and
`sixdb` directories, as in this workspace. Entries point to earlier work;
they do not establish SixDB contracts or reproduce its benchmark claims.

## Module ancestry

| SixDB | Calico starting points |
| --- | --- |
| Ikea | [frame](../../../calico/frame/README.md), [qhash](../../../calico/qhash/README.md), [keyset](../../../calico/keyset/README.md), [kmath](../../../calico/kmath/README.md) |
| Orbital | [xmem](../../../calico/xmem/README.md), [omachine](../../../calico/omachine/README.md) |
| Loom | [loom](../../../calico/loom/README.md), [arbor](../../../calico/arbor/README.md) |
| Engine | [engine](../../../calico/engine/README.md), [foyer](../../../calico/foyer/README.md) |
| Shore | [calico-ui](../../../calico-ui/README.md) |
| Workbench | [workbench](../../../calico/workbench/README.md) |

## Architecture and investigations

- [Design tour](../../../calico/design/TOUR.md),
  [trie](../../../calico/design/TRIE.md), and
  [data model](../../../calico/design/DATA-MODEL.md).
- [Earlier compression drafts](../../../calico/design/prior-art/README.md):
  QFC/RAFoR sources and their relationship to Frame; their benchmark numbers
  are identified as placeholders in the source index.
- [Engine survey](../../../calico/design/prior-art/engine2-survey/README.md):
  planning, execution/joins, and pruning summaries.
- [Science index](../../../calico/workbench/science/README.md) and
  [prior-art studies](../../../calico/workbench/science/prior-art/README.md).
  The Calico process is historical context; SixDB's lighter arrangement is
  described in the [workbench](../README.md).
- [Prototype directory](../../../calico/workbench/prototypes/): includes
  predictive PFOR, byte packing, compact row filters, point layouts, and
  three-array experiments. Consult each study's own findings and limitations.

- [CPS and reusable stages](../../../calico/workbench/prototypes/bytepack/evidence/cps.md):
  calling protocols, code growth, and compilation measurements.
- [Foyer build survey](../../../calico/workbench/science/systems/foyer-build-shape/BRIEF.md):
  parsing, template, optimization, and shared-compilation costs.

## Operating tools

- [Build](../../../calico/BUILDING.md),
  [CMake root](../../../calico/CMakeLists.txt), and
  [presets](../../../calico/CMakePresets.json).
- [Fleet](../../../calico/tools/fleet/README.md): provisioning, source capture,
  remote execution, S3 collection, and instance lifecycle.
- [Google Benchmark pilot](../../../calico/qhash/bench/GOOGLE.md) and
  [source](../../../calico/qhash/bench/google.cpp).
- [Microbenchmark affinity defaults](../../../calico/tools/fleet/recipes/lib/microbench.sh).
- [Benchmark entry points](../../../calico/workbench/benchmarks/README.md) and
  [bundles](../../../calico/workbench/bundles/README.md).
