# Earlier Object and conversion experiments

These findings preserve the useful experiments behind later Object integration,
without keeping three superseded APIs and their launchers live.

| Historical question | Retained findings | Current consequence checks |
| --- | --- | --- |
| One logical selection, later discovery, narrow escape and delayed completion | [Selected access](selected-access.md): lifetime falsifiers, borrowed/owning call costs and limitations | Production catalog/Shape, Object Worker and Ring checks |
| Real row/column conversion with changed Part count and interpretation | [Conversion qualification](representation-conversion.md): independent wire-bit checks, failed targets and simultaneous source/target costs | Composed conversion, count-changing append and physical pack-growth checks |
| Selection, conversion, publication and waiting in one caller | [Composition findings](selection-composition.md): coherent old/new observations and narrower critical-path evidence | Composed Object checks and fused-read authorization |

The [current check inventory](../../../benchmarks/retired-drivers.md#current-integration-checks)
names the executable targets. [Object integration outcomes](../OUTCOMES.md)
connect their observations to the selected modules. The
[material guide](../../../../orbital/docs/material.md) owns current source and
ownership composition. The historical fixture's completed external owner is
not an additional production API.

Carry forward the distinctions: a logical cut protects later discovery;
resolved physical access needs its backing and interpretation; cancellation
ends interest before physical use necessarily ends. Conversion can change Part
count and identifiers without changing logical version. Local lifetime checks
do not establish a durable Port, journal protocol or concurrent catalog.

## Exact recovery

The complete pre-curation repository is Git commit
`35e63b8416d377f216575d6f8d97668b9bce9ab3`. All 29 tracked files in the three
prototype trees matched that commit byte for byte before removal. The
[recovery receipt](recovery.json) records their Git tree IDs and the separately
retired first formal suite. No unique untracked source was removed.

For inspection:

```sh
git show 35e63b8416d377f216575d6f8d97668b9bce9ab3:workbench/spikes/orbital-selected-access/access.h
```

For a runnable historical snapshot with sibling modules, extract the complete
repository into a separate ignored directory:

```sh
mkdir -p build/recovered/object-prototypes-35e63b84
git archive 35e63b8416d377f216575d6f8d97668b9bce9ab3 | \
  tar -x -C build/recovered/object-prototypes-35e63b84
```

The original README files retain run commands. Each measured result's own
captured source, binary and tool receipt remains authoritative for reproducing
its numbers; source recovery alone does not recreate those conditions.
