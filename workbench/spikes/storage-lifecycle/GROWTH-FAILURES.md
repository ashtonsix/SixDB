# Independent repeated-job attacks

The `storage_adversarial_growth` subagent owns the external workload in
[`adversarial/growth_workload.py`](adversarial/growth_workload.py). It invokes
fresh command-line processes against a private controller and local object
service. No production cache, cloud service or real dataset is used.

The usage oracle walks the managed filesystem itself: regular allocated bytes
(`st_blocks × 512`), unique regular inodes, directory allocation and symlinks.
It counts temporary output as well as committed output. Tool-reported `stats`
or budget counters are not the correctness oracle. Repeated inputs are generated
independently; expected results and hashes are checked from their original bytes.

## Reproduced before replacement

The working-tree source-archive omission patch does not prevent ordinary result
growth. With a **1 MiB worker budget**, **200 public `worker.py fetch` calls all
succeed**, each restoring another copy of one unchanged 64 KiB input.
Independent retained allocation grew as follows:

| Successful jobs | Allocated bytes | Regular inodes |
| --- | ---: | ---: |
| 1 | 114,688 | 9 |
| 10 | 1,069,056 | 75 |
| 50 | 5,165,056 | 355 |
| 100 | 10,285,056 | 705 |
| 200 | 20,525,056 | 1,405 |

This is about 19.5 times the configured budget. There are no stranded transfer
directories in this reproduction: successful normal operation is sufficient.
The fresh-process controller snapshot and transcripts are under ignored
`build/reviews/storage-20261024/adversarial-growth/legacy-mixed-200/`. The workload
also repairs a changed local measurement while checking that the original edit
survives in conflict output, preserves a pinned launch-source copy, and exercises
explicit selected and full source recovery.

The provisional `artifacts.py input` cache fixes identical-reference reuse, but
creates another unbounded store. Forty artifacts each contain the same 64 KiB
input and a small distinct schema record. Their independently unique payload
totals **66,126 bytes**. All forty calls succeed under a **1 MiB local-build
budget**, retaining **3,280,896 allocated bytes** before another overlapping
selected view, and **3,358,720 bytes** afterward. Selecting the unchanged shared member separately adds another
physical file rather than reusing the full artifact's member. Warm reuse is
correctly offline; that does not establish bounded growth. Transcripts are under
`build/reviews/storage-20261024/adversarial-growth/variants-mixed/`. A local cached
input edit is detected and preserved; that correctness does not repair its
unbounded lifetime.

These tests exercise the tool's current public entry points, with an `aws`
fixture executable that refuses every command except a local `get-object`.
The private controller ensures its jobs actually occupy its own conventional
`build/workers/` scope; this is not a test of an excluded arbitrary directory.

## Replacement workload

The candidate mode is ready for the proposed standalone store CLI. It applies
the same input and source bytes under hundreds of different job labels, adds
independently checked result objects, overlaps selected/full-style acquisitions,
holds an active input lease, and retains an acknowledged unarchived local value.
Every command starts a new process, so process restart is the normal test path.
Four processes then contend for capacity with distinct 32 KiB values. Readers
hold explicit leases while reading. A later refusal must preserve previously
acknowledged unique and active values; rejecting the entire workload is not
accepted as a useful cache.

Peak allocation is sampled every 25 ms and final allocation is inspected after
commands. This measures observed in-flight occupancy; it cannot prove there is
no shorter transient peak. The separate OS-failure agent owns syscall/kill and
small-filesystem exhaustion cases that can expose those intervals.

## First candidate result

Frozen candidate source
`461012d1fb25cd97c5aabb1d1df23461955e53a5f98681dd43f1c8745750382c`
completed the 200-job workload with **450 successful sequential operations**, no
sequential refusals and sixteen successful contended acquisitions. Independent
observed peak was **909,312 allocated bytes**, with **778,240 bytes** retained at
the end, under the **1,048,576-byte** cap. At each inspected checkpoint, the
unchanged shared input occupied exactly one regular payload inode. Explicit
active leases and acknowledged unique bytes survived pressure and restart.

This run took about 28 seconds and is retained under ignored
`build/reviews/storage-20261024/adversarial-growth/candidate-001/`, including the
frozen candidate, harness hash, backend bytes, subprocess transcripts and
`result.json`. Its selected/full-style operations acquire overlapping members
of a logical result through the primitive, not through a deployed worker adapter.

This establishes the tested primitive behavior, not a durable production
workflow. The candidate has no complete worker/build/S3 integration. Local
unarchived `put` objects cannot yet transition to verified archived/evictable
state; indefinite unique production must eventually refuse safely. Lease labels
are explicit group tokens, not independent consumer reference counts: production
callers need distinct lifetime tokens rather than reusing a descriptive owner
string. The 25 ms sampler does not establish a bound on every shorter interval.

The `candidate-pressure` mode further tests alias-metadata exhaustion and a
changed unarchived local value. Its result is recorded separately rather than
inferred from the positive workload above.

## Repaired cut rerun

The first growth-positive cut still had important OS-level failures discovered
by the independent [OS attacks](OS-FAILURES.md). A positive growth panel did not
qualify that implementation. After the parent's initial-catalog, directory-FD
and lock repairs, the same external workload ran against the exact frozen cut
`617d1b7fd6fc0cfc2e6ceac75aee2641c651addbd68effab172bc9755f7c24ba`.

- **200 mixed jobs:** 453 successful sequential operations, zero refusals or
  unexpected errors, plus sixteen successful contended acquisitions. Three old
  result keys also exercise `get` recovery before explicit refetch. Observed
  peak **909,312 bytes**, final **778,240 bytes**, cap **1,048,576 bytes**. The
  common input still has exactly one payload inode at every checkpoint.
- **300 long aliases under a 262,144-byte cap:** 99 are accepted and 201 refused
  for capacity. Observed peak **163,840 bytes**, final **131,072 bytes**. The
  active input and acknowledged unique value survive saturation. The adversary
  explicitly changes permissions on its own otherwise read-only unique blob,
  edits it, confirms `get` rejects the old digest, and confirms twelve later
  pressure requests preserve the edited bytes.

Receipts are under ignored `adversarial-growth/candidate-002/` and
`adversarial-growth/candidate-pressure-002/` beneath the October 24 operational
directory. Both identify the frozen candidate and harness
`aacc58caed2c7bc4591da6b70600f5f8985b63f70557b3a27d8c77f7c982c8e2`.

The metadata test is also a usefulness warning: safe refusal occurs while about
half the cap remains allocated. Conservative reservation and retaining every
alias avoid overflow, but production needs an alias retirement/recovery
lifecycle. Otherwise this can replace disk babysitting with capacity-error
babysitting. It is an unresolved workflow limitation, not a data-loss failure.
These reruns remain implementation feedback; a fresh adversarial-agent round is
still required once the replacement is substantially ready.

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/growth_workload.py \
  legacy-workers --jobs 200 --output build/reviews/storage-20261024/adversarial-growth/legacy-new
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/growth_workload.py \
  cached-variants --variants 40 --output build/reviews/storage-20261024/adversarial-growth/variants-new
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/growth_workload.py \
  candidate --jobs 200 --budget 1048576 --expect-bounded \
  --output build/reviews/storage-20261024/adversarial-growth/candidate-new
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/growth_workload.py \
  candidate-pressure --jobs 300 --budget 262144 --expect-bounded \
  --output build/reviews/storage-20261024/adversarial-growth/pressure-new
```

Each output path must be new. Fixtures are bounded to at most 300 small jobs,
100 small variants and an 8 MiB candidate budget; do not point them at an actual
research cache. The harness writes `result.json`, source identities and complete
subprocess return/output records for inspection.
