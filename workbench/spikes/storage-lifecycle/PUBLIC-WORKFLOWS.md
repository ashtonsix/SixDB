# Actual public storage workflows

The pre-integration public tools still admit repeated writes past their declared
budgets. This independent regression workload executes their ordinary entrypoints,
including real Clang/CMake builds, rather than inferring rollout behavior from
the byte-store prototype. Public integration and its replay remain outstanding.

## Starting point and safety

The [research retrospective](../../notebook/research-experience.md) found that
shared input references replaced per-bundle input copies. The current
[storage guide](../../tools/storage.md) explicitly calls its limits retention
budgets, and the [owning spike](README.md) distinguishes extent sharing from
admission control. Those findings suggested testing ordinary input recovery,
recent/pinned output and interruptions before adding any new storage harness.
Previous reviewers' outcome reports were not used as an oracle.

[public_workflows.py](adversarial/public_workflows.py) copies the actual helper
sources unchanged, recursively including Python packages, into one tiny isolated
Git repository. This redirects hardcoded `ROOT/build` and `JOBS` paths without
patching the production functions. The real `capture.py` changes only that fixture
repository's worktrees. The real CMake entrypoint and `cmake/` rules compile a
one-function target with the pinned Clang 21.1.8; subsequent builds check useful
incremental reuse. `experiment.Run` is its public Python interface.

Only AWS is fake: an executable at the head of the child commands' private
`PATH` implements bounded local `s3api put-object`, `s3api get-object` and
read-only `s3 cp` responses. It rejects every other operation, including EC2.
Archive packing, transfer subprocesses, verification, extraction, collection,
prepared input publication and public CLI dispatch are actual tool code. The
fixture runs no network operations, real workers, installed-tool changes or
live-checkout Git mutations. Backend bytes represent completed remote jobs,
not an actual S3 durability or IAM test.

`SIXDB_DATA_CACHE`, explicit `artifacts.py input --cache`, `XDG_CACHE_HOME` and
`TMPDIR` point into the private test root. The local limit is 131,072 bytes;
the worker limit is 98,304 bytes. Every payload is 65,536 bytes. A per-file
2 MiB limit and independent 16 MiB fixture guard bound these tests. The completed
baseline fixture used 5,505,024 bytes by unique-inode `st_blocks` accounting.

## Reproduction and source identity

From the Mac-facing repository root, choose a new work directory:

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/public_workflows.py \
  --source /home/ashtonsix/sixdb \
  --work /home/ashtonsix/sixdb/build/reviews/storage-20261024/actual-workflows-baseline/REPLAY
```

The command exits successfully when the baseline workload completes, including
observed boundedness failures. `--expect-bounded` additionally exits 1 when any
expectation fails. It refuses an existing work directory. Output `report.json`
records every exact argv, cwd, per-command environment change, return code and
output, all copied source SHA-256 values, fixture bytes and external assertions.
No helper accounting report is used to decide whether a budget was exceeded.

The final pre-integration execution is
`build/reviews/storage-20261024/actual-workflows-baseline/baseline-4/report.json`.
It performed 58 subprocess commands and 25 external checks: 13 passed and 12
exposed the baseline's limits. Full prior development fixtures are disposable;
only their small execution receipts need remain. The report identifies the exact
workload bytes as well as the sources it exercised.

The report SHA-256 is
`ceef9b749676e0de62905af8d6bec9c3f6cc90c7d4c9692881bb46e3be272215`;
its replay source is
`c47f1bac34fc53cc22f2eeb3edb6ca38a0e09a28fcadc4205d224d0590a328de`.

| Public source | SHA-256 |
| --- | --- |
| `artifacts.py` | `4a2f504a11b77b021fec1ad9084f1547050b3fc7c739a9a2796b477258158c85` |
| `capture.py` | `543d75ba9fed7f5213c427e0cebda049cb4f85bbbc343840fe7d6dfb85ab90d2` |
| `datasets.py` | `3a079cb4c5ffbe01746c1872dfe4b3bf456ec6e569eba032cb8186918894792b` |
| `experiment.py` | `e459c62886a15cee61844b86c52f8dba90ef9b818a34ca0a48fc26f6accff588` |
| `worker.py` | `b29070fd1ccdf64f21ab05e63d4f525bc659e22ae25ee85b4f11233d0a222452` |
| `worker_cache.py` | `6990085bfb61cf0643c52963b5da3b0af02a4d3108942e4fc8d6ca9c265c70e7` |
| `local_cache.py` | `693347282a620655bc10ac7a3757dcabcbb3d41a19b2aff24d8d507d62c1efac` |
| `storage.py` | `cf89b9f01e5e0aa69b538ebaa92f61b6f3ff0a7555e4f695c727b029aa01513c` |

## Observed failures, in consequence order

1. **Ordinary admission bypasses both limits.** Four ordinary
   `artifacts.py fetch` recoveries left 450,560 allocated bytes in local build
   output despite the 131,072-byte limit. Four ordinary `worker.py fetch`
   collections left 376,832 allocated bytes despite the 98,304-byte worker
   limit. Every accepted file matched independent expected bytes. Inputs and
   references are tiny fixtures; no payload expansion bomb is involved.
2. **Interruptions spend the same capacity again on retry.** Five killed public
   artifact fetches left 164,385 compressed bytes in abandoned nested `.fetch-`
   directories (184,320 allocated bytes). Two killed worker fetches left
   131,544 compressed bytes. Three killed dataset preparations left 196,608
   payload bytes. Three killed `Run` writers left another 196,608 payload bytes.
   The killed process groups include the fake transfer process. A normal retry
   still recovered correct bytes; old staging remained alongside it.
3. **Prepared input recovery repeats actual physical data.** A run uses
   `run.input('inputs', prepared_directory)`, is archived with the real
   `artifacts.py put`, and is recovered three times with ordinary `fetch`.
   Each result gets its own `inputs/data.bin`. The Linux FIEMAP physical extent
   union for those three data files is 196,608 bytes for one 65,536-byte input;
   all recovered bytes match. The shared `artifacts.py input` helper passes
   reuse tests, but full ordinary recovery still copies prepared dependencies.
4. **Datasets, changing captures and useful builds write outside admission.**
   Four catalog CLI variants with identical output contents occupied 278,528
   allocated bytes. Three captures separated by actual source edits occupied
   at least 262,144 physical extent bytes. A real CMake workspace occupied
   245,760 allocated bytes and completed two useful builds despite the
   131,072-byte local limit. Its second build performed no compile work.
5. **Maintenance preserves valuable bytes but cannot establish a bound.**
   Public `worker.py cache --prune --older-hours 0` preserved a pin, an edited
   measurement witness and unique local evidence, as it should. Worker output
   still occupied 385,024 allocated bytes. A report showing over-budget output
   is observational; it supplies no missing admission control. Subsequent
   unsafe growth must be refused when protected bytes fill capacity.

The allocation figures include directories and deduplicate hardlinked inodes;
they can count shared reflink extents repeatedly. The capture and repeated input
claims therefore use an independent kernel FIEMAP physical union. That union is
a lower bound excluding unknown/inline extents and filesystem metadata. Initial
identical captures did share extents, so the retained capture workload makes
real source edits before each capture. No claim of physical duplication follows
from equal contents or distinct inode numbers alone.

## Existing behavior to preserve

Repeated `artifacts.py input` requests return one verified cache path. Explicit
artifact and dataset cache paths are honored; a cache inside `SIXDB_RESULTS` is
refused before creating it. Repeated dataset requests reuse the same variant.
Captures preserve earlier bytes after live edits. Real CMake workspace reuse
avoids recompilation. Default worker collection omits root and nested source
archives and releases the matching launch archive; selected recovery restores
the exact nested archive. Worker repair restores the archived measurement and
preserves the differing user's measurement separately. Explicit pruning keeps
pins, edits and unique output.

## Replay after integration

The integration contract reported by the owner is unchanged public commands,
a recursively copied `workbench/tools/sixdb_storage/` package, and private
`SIXDB_STORAGE_CONFIG` pointing to `{pool,budget_bytes,size_bytes}` after explicit
initialization. The final admission replay must prepare recovery references with
enough capacity, then accept correct results or safe refusal under pressure.
Blindly treating every nonzero command as success would hide broken tools.

This baseline runner currently requires successful commands to reproduce the
old bypasses. Its useful functional and interruption workloads are executable
and retained, but the initialized capped-volume replay, restart reconciliation,
concurrent admissions and actual filesystem exhaustion have not been performed
against integrated public paths. The baseline does not establish that any
prototype or future integration passes them.
