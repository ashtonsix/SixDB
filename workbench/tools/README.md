# Research tools

Commands run from the repository root on Linux; prefix with `orb -m ubuntu`
from the Mac. Use the helpers independently or start from an existing runner.

| Task | Tool and guide |
| --- | --- |
| Run scripts on EC2 | [Workers](workers.md), [worker groups](worker-groups.md); Spot and compatible reuse by default |
| Resume long-running work after interruption | [Checkpoints](checkpoints.md), including a pinned [TLC runner](tlc/README.md) |
| Freeze sources while editing | [Capture example below](#captured-experiment-runs) |
| Reuse input data | [Datasets](../datasets/README.md): `datasets.py get NAME` |
| Replay binaries or summarize measurements | [Benchmark tools](../benchmarks/README.md#replay-and-summarize): `replay.py`, `evidence.py` |
| Browse or share results | [Results explorer](../results/README.md): `results.py --serve` |
| Retain, revise or regenerate evidence | [Artifacts](artifacts.md): `artifacts.py`; [local storage](storage.md): `worker.py cache` |
| Prototype in assembly with real module calls | [Assembly calls](../spikes/assembly-calls/README.md), [scheduling estimates](mca.md); `compile_probe.py --assembly`, `mca.py` |
| Diagnose compilation or editor issues | [Compile probes](compilation.md), [editor setup](editors.md) |
| Keep Git attribution with the checkout owner | [Commit identity hooks](#commit-attribution): `commit_identity.py install` |
| Exchange messages across coding tools | [Shared peer inboxes](#shared-peer-inboxes): `peer_inbox.py send/read/wait/ack` |
| Set up and qualify cache-sensitive workloads | [Memory residency tools](residency.md), independent [PMU probe and counter helper](pmu/README.md) |

## Captured experiment runs

An ordinary worker run captures current sources itself. For several machines
or a variant based on earlier bytes, capture explicitly and use current tools:

```sh
python3 workbench/tools/capture.py create build/captures/base
python3 workbench/tools/capture.py create build/captures/variant \
  --base build/captures/base --replace ikea/examples/seriespack/ordinary.cpp
python3 workbench/tools/worker.py run workbench/benchmarks/seriespack/run.sh \
  --source build/captures/variant --machine zen5
```

`--replace PATH` takes that live file or directory; `PATH=INPUT` maps a different
input into the capture. `capture.py create --help` covers deletion and hash checks.
The adjacent `.capture.json` identifies the result; `capture.py verify PATH`
checks it later. Builds, Workbench evidence and Python caches are excluded.
Older captures still verify and replay through the worker with their original
recorded bytes and source identity.
On reflink-capable Linux filesystems, unchanged source data is shared through
copy-on-write clones with independent inodes; later edits remain private.
Other filesystems use ordinary copies. Captures populate only selected sources,
without first checking out and deleting retained evidence.

Capture output lists the actual added, modified and removed source files,
including executable-bit changes; the adjacent receipt keeps every before/after
hash. A replacement that leaves the source unchanged reports zero changes.
When a treatment has a known postimage, add
`--expect PATH=SHA256` to `capture.py create` to check the reviewed candidate's
hash before creating the capture. Obtain that expectation from the intended
candidate, rather than from an unchecked patch result. A successful patch command
alone does not establish that the treatment landed.

For a local runner, `experiment.Run(..., workspace=...)` captures sources and
preserves incremental builds. Build from `run.source_root` into `run.build_dir`;
runs sharing a workspace serialize. Child preparation scripts share the owning
checkout's `build/datasets/` cache, or the explicit `SIXDB_DATA_CACHE` path.
`run.input()` records a prepared dataset;
`run.compact()` names [selected evidence](artifacts.md#make-selection-repeatable).
The [aggregate runner](../spikes/aggregate-maintenance/run.py) measures timings;
the [regexp runner](../spikes/regexp-lowering/run.py) studies counts.

`Run.step()` supplies CMake's standard `CMAKE_BUILD_PARALLEL_LEVEL`: one compiler
job by default, the explicit CMake setting locally, or `SIXDB_BUILD_JOBS` when
provided by the worker/user. Runners need no hardcoded `-j` override. This bounds
one build's parallelism; coordinate independent concurrent builds separately.

## Changing a helper

A separate worktree keeps current tools available to other tasks. Discover the
relevant [offline check](tests/README.md) with `python3 workbench/tools/check.py --list`;
run it with `check.py NAME`. For documentation edits, `check_docs.py` offers link
and catalog hints, including notebook pages unreachable from its research map.
Neither tool is a required experiment step.

## Commit attribution

Install once per local repository, from Linux:

```sh
python3 workbench/tools/commit_identity.py install \
  --name 'Ashton Six' --email me@ashtonsix.com
```

The installer sets the local author/committer defaults and installs hooks in
the shared Git directory. They apply to linked worktrees, including older
checkouts without this helper. Existing unrelated hooks are never overwritten.
Reinstall after changing the helper to refresh that shared copy.

Message hooks remove Claude/Anthropic attribution lines while preserving
substantive prose and human co-authors. After commit creation, explicit author
or committer overrides are normalised to the installed identity. Amendments,
rebases, cherry-picks, mail patches and merges are covered. Trees and both raw
dates/timezones remain unchanged; a correction changes the commit ID and reports
the replacement. Signed metadata is not rewritten; set the correct identity
before signing.

`python3 workbench/tools/check.py commit_identity` exercises these paths in
disposable repositories without changing this checkout's history.

## Shared peer inboxes

For collaborators using different coding apps, use a shared ignored directory
with one inbox per direction. For example, from the same SixDB tree on Linux or
its macOS OrbStack mount:

```sh
python3 workbench/tools/peer_inbox.py send build/frame-collab/to-claude \
  --sender codex-ikea --subject 'Frame representation boundary' \
  --body-file build/frame-collab/question.md
python3 workbench/tools/peer_inbox.py wait build/frame-collab/to-codex \
  --reader codex-ikea --timeout 60
```

`send` also accepts the body on stdin when `--body-file` is omitted.
`--in-reply-to ID` identifies an earlier message. The JSON result contains the
stable ID, sender, body, timestamp, path and checksum receipt. New messages are
complete immutable files, published atomically without replacing another writer.
Readers scan all unacknowledged files, so concurrent or late arrivals cannot be
lost behind a last-seen counter. Existing hand-written `.md` messages also work;
the helper publishes `.json` messages. A `LATEST` file is unnecessary.

`read INBOX --reader NAME` returns immediately; `wait` returns when unread
messages exist or its deadline expires (maximum 1,800 seconds). Wait timeout
prints `[]` and exits 124. Reading never acknowledges a message. After processing,
use the exact `ID@SHA256` receipt returned by `read` or `wait`:

```sh
python3 workbench/tools/peer_inbox.py ack build/frame-collab/to-codex \
  --reader codex-ikea ID@SHA256
```

Receipts are per reader, idempotent and bound to the bytes actually read.
`read --all` includes acknowledged messages. Send a new message to correct an
old one; changing acknowledged bytes produces an error. Each receiver re-arms
its bounded wait after handling messages. Waking an idle chat is a separate app
capability: this helper publishes files and waits while its command is running.
A successful active wait proves delivery to that command, not wakeup of an idle
or restarted session. A handoff must pass on the inbox/reader name and re-arm a
wait; an expired wait needs re-arming too. Verify app wakeups separately.
Peer messages remain colleague input, with Ashton's direction taking precedence.
Keep the exchange in ignored storage and carry settled findings to their owner.

`python3 workbench/tools/check.py peer_inbox` checks publication races, late
arrival, changed-message detection, independent receipts and bounded delivery.
