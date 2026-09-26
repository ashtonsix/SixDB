# Release allocation before executing

The final recommendation uses exclusive access only to **allocate and announce
positions**, then releases it before user computation. Ordered pending versions
retain the unresolved effects. This replaces the earlier candidate's ownership
through installation; it is not an additional fallback protocol.

The question was whether exact output positions had already done the ordering
work that long-held writer gates were meant to do. Two independent semantic
cores, an independent application audit and the shared timing adapter support
that conclusion under the obligations below. It introduces multiple pending
versions, not fewer total states. We select it because it removes unnecessary
execution-time exclusion and permits preparation to proceed ahead of computation.

## Required semantics

- New output allocation follows earlier announced output positions as well as
  committed heads and read bounds. Exclusive allocation access lasts until the
  exact position is published at every possible effect authority. No late
  mutation scope or authority is introduced.
- Pending versions resolve by transaction identity. Aborting or producing no
  write creates no version: an earlier pending writer may still determine the
  value. A DELETE tombstone is an actual version and has different semantics.
- Installation can arrive out of order. The highest logical position determines
  the current row and index membership, never physical arrival. Historical cuts
  still select their own versions.
- A later committed complete replacement may hide an earlier pending version
  only for effects it demonstrably covers. It cannot erase other possible rows
  in a broad envelope or justify bypassing unknown global predicate effects.
- Reserve space for pending announcements and their resolution **before** any
  provisional read blockers exist. Otherwise B can block A's read while waiting
  for memory that only A's completion will release. Admission may queue or fail
  under agreed budgets; metadata finalization must never await user execution.

For example, T1@10 may write x but eventually produces no write. T2@20 increments
x; T3@30 unconditionally replaces x with 9 and commits first. A reader@40 can see 9.
T2 must await T1, then read the genuine version preceding 10, not 9 or a fabricated
no-write tombstone. Its later physical installation cannot overwrite x@30.

Ordinary SQL `UPDATE ... SET v=9` is not necessarily blind: a pending DELETE can
determine whether the row exists and whether the update returns zero or one
affected rows. Uniqueness, cascades, old-image triggers and RETURNING can also
require predecessor values. The independent audit retains these negative controls.

## Timing comparison

[pipeline_simulation.py](pipeline_simulation.py) extends the frozen fixed-position
scheduler and charges a separate delivered allocation-release request. It keeps
the existing single-step contract for eligible local transactions, including a
local complete replacement through an older pending version. Blocked local
read-modify-writes continue through the fixed-position read path. The held-control
matches the frozen backend's cohort outcomes and final serial state for every
case; it does not claim identical metadata traces after introducing pending chains.

The main sweep has 132 runs:19 existing workloads plus 3 envelope controls, two
lifetimes and three seeds. [pipeline_contrasts.py](pipeline_contrasts.py) adds 16
runs separating the harness's old deadlines from a long, single-attempt drain
and explicitly unconstrained full replacements. All pass program/observation
serial replay. Costs are synthetic, transport is failure-free, and resource
capacity/GC is not implemented. These are not database performance predictions.

At seed 7:

| Workload/contract | Admission held through outcome | Released after position |
| --- | ---: | ---: |
| Hot distributed, complete/failed/pending at 1,200 | 16/0/112 | 24/0/104 |
| Hot distributed, p99 after full single-attempt drain | 9,377 | 6,099 |
| Conflicting WAN, local-update p99 after full drain | 3,255 | 2,609 |
| Bulk plus point updates, point p99 after full drain | 294 | 275 |
| Blind replacements during 150-unit private delay, p99 | 259 | 69 |
| Blind replacements during 600-unit private delay, p99 | 711 | 69 |
| Uncontended distributed p99 | 80 | 80 |
| Disjoint rare-WAN regional p99 | 1 | 1 |

All 128 hot requests,96 overlapping regional updates and 64 bulk point updates
complete in their respective full-drain controls. The blind rows each contain 96
unconditional complete-value puts; they do not contain hidden SQL reads. Their
remaining 69 ticks include the earlier transaction's position allocation, not its
private computation. This demonstrates the narrower waiting rule concretely.

The contrary results matter. With the original single-attempt capture-age cutoff,
the bulk workload loses 13 of 64 point updates under pipelining, versus none with
held admission. The common four-attempt harness eventually completes all 64 but
raises their p99 from 294 to 428. The long-wait, no-retry comparison completes all 64
at 275. That is a resource/deadline difference, not source renewal: read waiting
now starts while prior computation is outstanding. The recommendation does not
adopt the harness's automatic retries.

Backlog also grows. In the full conflicting-WAN drain, the maximum persistent
pending chain reaches 64 per key instead of 1. A deliberately whole-shard envelope
still gives severe unrelated-traffic interference and can worsen under greater
in-flight demand. In the width 16 target-only envelope case, point-writer p99 is
221 held versus 234 pipelined, while source/outside writers remain at most 2.
Shorter allocation is no promise that a broad possible effect becomes narrow.

These results favor allocation-only access as the general mechanism, with
bounded admission before announcements. They do not establish the right capacity
policy or authorize interpreting every assignment as an independent blind put.
Actual dependency waits, global-index uncertainty and the execution-epoch
placement requirement remain.

## Reproduction

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/pipeline_simulation.py --seeds 0,7,19 --output build/orbital-pipeline/sweep.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/pipeline_contrasts.py --output build/orbital-pipeline/contrasts.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_position_pipeline.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/release_after_position_audit.py --output build/orbital-release-audit/audit.json
```

The [semantic core](position_pipeline.py) and
[independent audit](RELEASE-AFTER-POSITION.md) keep the application and resource
counterexamples. The [evidence bundle](evidence/position-pipeline/RECOVERY.md)
retains all compared outcomes, source identities and complete replay inputs.
