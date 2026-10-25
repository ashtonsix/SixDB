# Fresh ordinary-workflow adversarial review

2026-10-24. The tested composition still allows repeated growth outside the
immutable store and forces manual intervention inside it. A bounded workspace
remains a proposal being investigated elsewhere; these results do not treat that
proposal as implemented or test its eventual containment.

The attack set was formed independently without reading earlier adversarial
reports. Starting evidence was the [research retrospective](../../../notebook/research-experience.md),
the [storage guide](../../../tools/storage.md), the dataset/worker guides and
the actual helpers. The retrospective's shared-input and incremental-capture
mechanisms can be reused, but neither establishes shared bytes across different
input views or bounds mutable output. The storage guide explicitly calls its
budgets retention policies. The experiment therefore asks whether following the
documented normal helpers still requires storage babysitting.

## Reproduction and limits

[fresh_workflow.py](fresh_workflow.py) uses the actual `experiment.Run`,
`capture.capture`, `worker_cache.collect`, `artifacts.cached_input`,
`datasets.cached`, `local_cache.maintain`, and store CLI. It creates tiny private
Git repositories and worktrees; no real caches, cloud services, installed tools,
benchmark builds or production source edits are involved. Only artifact
transport is substituted: a private archive is copied locally and its complete
size and SHA-256 checked. Real extraction, default selection, receipts, locks
and publication still execute.

```sh
orb -m ubuntu bash -lc 'cd /home/ashtonsix/sixdb && \
  PYTHONDONTWRITEBYTECODE=1 python3 \
  workbench/spikes/storage-lifecycle/adversarial/fresh_workflow.py \
  --output build/reviews/storage-20261024/fresh-workflow/another-run'
```

The final complete run is retained in ignored
`build/reviews/storage-20261024/fresh-workflow/complete/`, with `results.json`,
source SHA-256 values before/after, and the small fixtures. `complete.stdout`
and `complete.stderr` are adjacent. An earlier run without the interrupted
recovery case is preserved in the parent directory. Each run requires a fresh
output directory rather than deleting or overwriting its predecessor.

Allocation is independently summed from Linux `st_blocks * 512`, deduplicated
by inode and including directories. For the repeated input/source copies,
FIEMAP also reports distinct guest filesystem extents without the SHARED flag
for the first two copies. This is stronger evidence than path counts or content
hashes alone, but does **not** establish an OrbStack backing-device quota or
measure APFS physical allocation. No real filesystem was filled to ENOSPC.
The configured 256 KiB budgets are intentionally tiny scaling fixtures;
`SIXDB_MIN_FREE_GIB=0` avoids relying on unrelated free space. Increasing these
budgets changes when the failures occur, not the admitting code paths.

## Counterexamples

| Normal workflow | Observed result | Mechanism |
| --- | --- | --- |
| Run a captured experiment under worker results, then collect six jobs by default | Worker allocation rose 233,472 → 466,944 → 700,416 → 933,888 → 1,167,360 → **1,400,832 bytes** against a 262,144-byte worker budget. All preflights and verified collections succeeded; zero bytes were age-eligible for pruning. | `Run` writes `comparison/source.tar.gz`. Default unpacking omits only the exact top-level `source.tar.gz`, so each nested transport archive returns locally. |
| Request six artifact selections that share a column and choose different view metadata | The identical 196,608-byte column occupied **1,179,648 allocated bytes** across six independent inodes. The full input cache occupied 1,306,624 bytes. Repeating the exact same selection reused its directory correctly. | Cache identity includes the entire selection. Shared members across different selections are extracted again, with no member-level content reuse or capacity admission. |
| Vary a dataset preparation parameter eight times | Eight checked variants occupied **1,605,632 allocated bytes**, including 1,572,864 bytes for eight identical payloads. Repeating an identical request reused it. | A distinct request creates a distinct complete prepared directory; recipe/parameter churn retains every variant and bypasses the store. |
| Use dev/release local build trees in a main checkout and a captured worktree | Both checkouts were discovered. Recent output was reported as **802,816 bytes** against 262,144 bytes, with no removal and no admission failure. A further ordinary object write succeeded; a `.keep-local` pin preserved it. | Cross-worktree accounting works, but the policy is reporting/idle-object deletion. It controls neither build writes nor uniquely valuable output. This case writes bounded ELF-looking fixture objects; it does not compile a benchmark. |
| Interrupt four artifact input recoveries after the full archive has downloaded, then retry successfully | Leftover allocation grew **200,704 bytes per interruption**, reaching 802,816 bytes. Four complete verified temporary archives, totaling 787,528 logical bytes, survived the successful retry. Total cache allocation then reached 1,015,808 bytes. | SIGKILL bypasses `TemporaryDirectory` cleanup. Each retry makes a new `.input-*/.fetch-*` tree; normal retry does not reconcile old temporary ownership. |
| Keep an ordinary file descriptor open after default store `get`, then admit another object | Visible store allocation was 180,224 bytes, but the removed object still held 163,840 bytes through its open reader: **344,064 live allocated bytes against a 262,144-byte budget**. The returned pathname disappeared; the already-open reader still returned the exact original hash. | Optional lease defaults expose a path without a held reader obligation. Eviction unlinks it; `used()` walks visible names and cannot count open unlinked inodes. |
| Use the explicit store lease, then kill its consumer | The next individually fitting, recoverable object was refused after consumer SIGKILL. The dead reader's token persisted across fresh store invocations. Explicit manual `release research-reader` allowed progress. | Leases protect data but have no process lifetime, expiry or automatic crash reconciliation. This is a safe refusal that still requires operator babysitting. |
| Repeatedly replace one store key, `latest`, with tiny recoverable objects | After **257 successful ingestions**, the next object was refused despite **zero resident payload files, one key and no leases**. The catalog retained 257 object records and occupied 59,249 logical bytes. | Old object records survive eviction and replacement of their sole key. Metadata and its atomic-publication reservation eventually consume admission capacity even when all payload is recoverable. |

### Nested captured source survives the new default

The source package in this fixture was generated by `experiment.Run`, rather
than inventing an arbitrary archive-looking output. Its `finish()` receipt
completed with unchanged source hashes. The same nested structure is produced
by existing worker entry points, including `layout-analyser/cloud.sh`
(`--output "$SIXDB_RESULTS/consumers"`) and
`allocation-live-tail/native.sh` (`--output "$SIXDB_RESULTS/comparison"`).

`experiment.py` always writes an output-local `source.tar.gz`.
`artifacts.unpack` checks `name.as_posix() == 'source.tar.gz'`, and
`worker_cache.normally_required` uses the same exact root name. Thus this is
an ordinary producer/consumer mismatch, even after the top-level launch package
is omitted and released. No nested source archive needed to be edited, pinned,
or selected with `--full` to reproduce the growth.

### Shared input means only the identical view

Both input helpers correctly reuse an exact warm request in the fixture. The
failure concerns normal changes in view, parameter, or recipe identity. The
artifact cache's key is the whole reference plus sorted file selection. Selecting
one extra metadata member produces a new complete copy of the common column.
The recipe cache's key is the full recipe/input/parameter request and it writes
directly through the supplied build function. Neither invokes `store.py`.

This remains a problem if an eventual workspace merely bounds each consumer
separately: overlapping consumers still spend capacity repeatedly and can force
premature eviction or refusal. The experiment establishes the duplication and
missing current admission, not how a future workspace will be partitioned.

### Temporary and reader ownership escape pathname accounting

The interrupted-recovery fixture stops immediately after a verified local
download and then sends SIGKILL. It is not an uncontrolled writer filling a
directory: these are allocations made by the real recovery path itself. A
successful retry has no reason to notice the old temporary names. The existing
local-build inventory can report them, but its idle CMake-object collector cannot
reclaim these archives or assert they are disposable.

The open-reader case likewise needs no rogue writer or modified object. It uses
default `get`, opens the returned regular file, and leaves the reader alive
while the next store subprocess evicts the object. An explicit lease makes this
particular operation refuse safely, as the store module's docstring requires.
That restriction must travel with every path-returning API and remain held until
every child process finishes consuming it. Current artifact/dataset path APIs
carry no such reader token. The killed-lease case exposes the corresponding
availability cost; it does not claim corruption or a lease bypass.

### Historical metadata exhausts the useful store

The history case reuses a single key rather than inventing an ever-growing
application key namespace. Each backend source is a small immutable local file,
every fetch supplies its checked SHA-256, and all older sources remain available.
At refusal the kernel reported:

```text
capacity refused: 73728 allocated + 188590 reserved > 262144;
leases=[]; unique/changed/unrecoverable data preserved;
```

The 257 historical catalog entries cannot leave through any exposed CLI operation.
Recovering the existing latest object still worked, so this is not a blanket
claim that every read fails. Continuing the ordinary changing-input workflow
requires an unimplemented catalog reclamation operation or operator reset. The
catalog bound is enforced through refusal, but the refusal is caused by stale
history rather than useful live content.

## Scope of the defeat

The immutable-store kernel does not yet sit on the real producer/consumer paths
tested here. Its limited scope is clear in its module docstring; passing its
isolated admission checks therefore cannot establish bounded research storage.
Normal captures, preparation, collection, recovery, mutable builds, temporary
ownership and reader lifetimes all need to compose with an implemented bounded
workspace before that larger claim can survive these counterexamples.

The current helpers preserve changed, pinned and unique evidence, which these
tests do not ask them to discard. That preservation is compatible with reporting
an over-budget state; it is not capacity enforcement. A useful replacement must
resolve the distinction without requiring researchers to remember custom cache
placement, manually identify dead reader tokens, or clean abandoned transfer
directories after ordinary interruption.

No production edits were made. Reviewed source hashes matched before and after
the complete run. The exact epochs are in its `results.json`; key values are:

- `store.py`: `617d1b7fd6fc0cfc2e6ceac75aee2641c651addbd68effab172bc9755f7c24ba`
- `artifacts.py`: `338b50a90c39dea15beef01802152de231c0ae3442796faaecbcf90d8d3706d3`
- `worker_cache.py`: `dbf521948e9a6c2e58b6e519fd18e3ee7de58a6e0e30b187a243c57396b88fc0`
