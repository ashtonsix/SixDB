# Locality, execution and contiguous demand

Ashton's 15 October request asked whether Loom can own reusable execution policy
without losing the few-nanosecond native path or the existing Ikea cold gains.
This directory retains the private experiments and the subsequent historical
[execution-policy graduation](GRADUATION.md). For the current generic API, use
the [execution guide](../../../loom/docs/execution.md): callers qualify their
legal procedures and evidence cohort, then explicitly reset Policy when that
qualification changes. The retained native timings measured an earlier
identity-bearing interface and do not qualify the revised API automatically.

Two questions are separate. Can actual recent serial performance cheaply select
direct execution versus a memory Ring? Can abstract hints describe a random
starting address followed by a contiguous consumed extent, without assuming a
trained sequence between queries?

[Findings and native comparisons](FINDINGS.md) retain the successful controls,
regressions and limits. This is caller-cohort policy, not a bucket heat directory.

## Private experiments

The private [`locality.h`](locality.h) holds a 56-byte caller-cohort record,
qualified by owner incarnation, storage generation, access population and
assigned worker/cache domain. Only completed serial useful work supplies
cheap-access evidence. Issued hints and pipelined reciprocal throughput do not.
Full outer epochs reject backward time, long idle and timestamp aliasing. The
record owns no bytes or interpretation. Decisions, clocks and samples stay
outside native Ring visits. Its `Tag` is this caller experiment's schema, not a
public Loom protocol.

`execution.cpp` uses the captured Ikea generic set/map/multimap reader and its
same result-dependent graph, with complete-byte independent answers. It compares
bare direct, fixed Ring, instrumentation-disabled selection, charged sampling
without selection, adaptive selection and caller-known direct. Repeated and
changing prefixes use one owner; a 128 MiB traversal conditions interference.
Generation/domain/expiry cases are synthetic identity controls, not real
publication or migration. All oracle traces are prepared before the legitimately
unused all-hit graph is released. Sampling repeats useful queries but does not
advance the measured request denominator; its entire cost remains charged.
The follow-up keeps the full 256-query sampler and adds 32-query probes: four
complete serial chains, rotating across eight head subsets. A first expensive
sample gets one next-batch retry; a second backs off for 32 batches. Two cheap
samples qualify direct selection. The additional cadence state is 24 bytes.
Small probes can miss heterogeneous costs; rotation does not guarantee coverage
before every decision.

[`contiguous.h`](contiguous.h) binds exact contiguous windows once, selecting
sixteen classes for 64/128-byte native lines. Each class hints fixed offsets and the last covered
byte, covering unaligned tails with no size/alignment loop. Duplicate physical
line hints are counted explicitly. A nine-hint cap limits larger windows to an
explicit prefix; the entire requested read window still needs qualification.
Disjoint windows remain separate demands. Hints promise neither readiness nor
content stability.
The optional unique-line emitter computes intersected physical lines and removes
duplicate hints in the smaller classes. Its largest class retains a bounded tail
hint. The additional selector arithmetic is measured as a separate candidate.

The optional `LOOM_CONTIGUOUS_IDENTITY` compiler control uses an empty immediate
class identity to prevent LLVM sharing class suffixes. It emits no assembly,
has no memory/register clobber and is private to this experiment. Compare code
size and complete consumers before choosing it. The default emitter remains a
separate control, including its native unconditional suffix jumps.

`prepare_consumer.py` derives only hint emission/preparation from the captured
Ikea `staged.cpp`. The original source is a separate executable: fixed L1,
caller successor hints, direct/manual/Ring, Abseil, all graph oracles and byte/
visit inventories remain controls. Candidate plans are prepared once in a
finite indexed table (1,025 × 16 bytes); that additional storage is not hidden
in the table's own allocation total. An untimed full-window inventory proves
every later plan index. This table tests binding/codegen cost, not a proposed
global lookup cache. L2 is a fixed locality arm, not measured busy pressure.

## Build a current composition

Build only wanted targets, using the pinned Linux compiler:

```sh
orb -m ubuntu cmake -S . -B build/loom-locality -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=loom-locality-execution
orb -m ubuntu cmake --build build/loom-locality --target \
  loom_locality_check loom_contiguous_check loom_contiguous_identity_check \
  loom_policy_cost -j2
```

Primitive/codegen targets are self-contained. Consumer targets require an explicit
`SIXDB_LOOM_CONSUMER_SOURCE_ROOT`: a complete verified recovered SixDB source tree,
including Ikea's private hashtable header graph. They do not depend on another
role's uncommitted checkout files. For example, recover the retained Zen input:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/loom-locality-execution/evidence/followup-zen5-20261015/artifact.json \
  build/recovered/loom-input
orb -m ubuntu python3 workbench/spikes/loom-locality-execution/replay.py \
  build/recovered/loom-input --followup --source-output build/recovered/loom-source
orb -m ubuntu cmake -S . -B build/loom-locality -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=loom-locality-execution \
  -DSIXDB_LOOM_CONSUMER_SOURCE_ROOT=build/recovered/loom-source
orb -m ubuntu cmake --build build/loom-locality --target \
  loom_window_hot_1_unique loom_execution_hot_1_budgeted -j2
```

This composes the captured caller with current module libraries; it does not
recreate the measured cut. The graduation adapter in `graduation.cpp` translates
its private cohort changes into explicit Policy resets. Source compatibility
supplies no performance qualification. Exact measured sources/binaries remain
in the archive. Worker runners expect an explicit complete source capture via
`worker.py run ... --source CAPTURE`; they select that captured root as their
consumer input.

## Read and recover the measured comparisons

`native.sh` retains matched hot/large all-three-kind panels and actual native
assembly. `report.py` checks logical/job/graph/visit equality, keeps every mode
and repetition, and separates whole-graph throughput, prefix policy costs and
decision-only timing. Neither scale labels nor an interference traversal prove
cache servicing tiers. No PMU, IPC, stall or miss-occupancy attribution is made.
`check_consumer_codegen.py` owns the native callback/outer-loop checks here;
analysis does not import Ikea's private checker.

`followup.sh` alternates reference/unique and full/budgeted process order across
five passes. `followup_report.py` checks every scheduled file/hash, useful answer
inventory and actual window-size inventory. `retain.py` selects complete timing
panels and compact receipts; its archive-backed recipe invokes current `replay.py`
and analysis tools, never captured scripts or binaries. Full source, binaries,
assembly, mapping receipts and build logs remain recoverable from each archive.

Ikea's captured staged consumer remains the historical reference. The frozen
capture keeps allocator and format bytes across all policy variants so unrelated
repairs cannot explain their differences.
