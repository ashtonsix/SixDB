# Checkpoint and resume a worker job

Opt in for work that can save its own resumable state. [Ordinary workers](workers.md)
remain unchanged. A checkpoint is separate from partial logs or the final result:
`complete` still means the script finished successfully and its results were collected.

For TLC, use the included [pinned runner](tlc/README.md):

```sh
python3 workbench/tools/worker.py run workbench/tools/tlc/run.sh \
  --setup minimal --checkpoint-script workbench/tools/tlc/checkpoint.sh \
  --checkpoint-seconds 300 --deadline 5400 -- \
  -workers 1 -config path/to/Model.cfg path/to/Model.tla

python3 workbench/tools/worker.py status JOB
python3 workbench/tools/worker.py resume JOB --detach
python3 workbench/tools/worker.py wait NEW_JOB
```

Model paths are illustrative. Run from the repository root on Linux; prefix
with `orb -m ubuntu` on the Mac. Choose `--instance-type` and `--disk-gb` for the
model's memory and metadata, after a small pilot. A deadline includes setup,
restore and collection. Spot remains the default; resume can change capacity,
hardware, deadline or checkpoint timing. It preserves the exact original source
capture, script, arguments and environment, even if the working tree has changed.
`wait JOB` resumes observation; **`resume JOB` starts a replacement computation**.
It refuses an original job that may still be running. Resubmission is explicit,
so a repeated failure cannot silently keep allocating machines.

## What gets saved

With `--checkpoint-script PATH`, the worker requests a save every five minutes,
before stopping on a Spot interruption notice, and at the execution deadline.
`--checkpoint-seconds 0` disables the periodic saves. The default total budget
for creating and uploading each checkpoint is 90 seconds; change it with
`--checkpoint-timeout`. A notice clips that budget to the reported interruption
time minus ten seconds, with a 105-second ceiling from detection. The observer
continues polling while a periodic save runs, so a notice can shorten its budget.

The producer's finished snapshot is split into 32 MiB chunks. Unchanged chunks
from the preceding generation are referenced, not uploaded again. S3 verifies
SHA-256 on writes; recovery verifies every chunk. Each generation has an immutable
manifest; `checkpoints/latest.json` is updated only after all its data is saved.
A failed producer, timeout or failed upload leaves the previous generation usable.
A replacement interrupted during setup can still use its inherited checkpoint.
No restore mixes files from different generations. Job-private live/restored state
is removed after verified final collection, so worker reuse does not accumulate it.

`status JOB` shows the last committed generation's age, size and reason. The
receipt includes newly uploaded bytes, so repeated saves expose how much reuse
is helping. `checkpoint-events.jsonl` records attempt durations and failures;
`checkpoint.log` contains producer diagnostics. These are collected with normal
results. Checkpoints live under the job's S3 prefix; large state stays outside
ordinary results and Git. Keep these objects together when retiring a job lineage:
newer generations can reference chunks in earlier jobs. Nothing is deleted by
this feature, including a prior usable generation or an incomplete upload.

## Supply a producer for another program

The worker runs `bash PATH SNAPSHOT_DIRECTORY` at the captured repository root,
with the same environment as the experiment. The directory is initially empty.
Exit zero only when it contains a self-consistent, complete set of recovery files
that will no longer change. Copy or reflink files; a hard link to mutable state
isn't a snapshot. Symlinks and special files are rejected. The program owns how to
reach a safe point, flush buffers, and resume after making the snapshot.

| Variable | Use |
| --- | --- |
| `SIXDB_CHECKPOINT_STATE` | Job-private scratch space for live state, PID/control files, etc.; never automatically collected |
| `SIXDB_CHECKPOINT_REASON` | Producer only: `periodic`, `spot-interruption` or `deadline` |
| `SIXDB_RESUME` | Experiment only, when resuming: verified restored snapshot directory |

For example, a producer for an application that atomically replaces one progress
file can simply copy that file into `$1`. A multi-file database or TLC needs its
own checkpoint protocol. The experiment checks for `$SIXDB_RESUME` at startup
and restores through the application's recovery mechanism. Replaying external
side effects safely is also the application's responsibility.

Producers run in their own process group. The time budget kills a stuck producer
and its children, removes its staging files, and continues the workload after a
failed periodic attempt. A producer that pauses work should undo that pause on
termination too; the worker additionally sends `SIGCONT` to the experiment group.
There is no coordinated multi-worker snapshot or group-resume protocol here.

## Limits worth measuring

[AWS interruption notices](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/spot-instance-termination-notices.html)
are best-effort. An abrupt failure can lose work since the last uploaded checkpoint;
a notice cannot guarantee that a large snapshot fits its warning window. Keep
periodic saves enabled for long runs. Measure the first full save and later
incremental saves before relying on an hour-long bare-metal run.

Chunk reuse saves upload bandwidth and retained bytes; the producer still needs
to snapshot consistent files, and the collector still reads/hashes them. TLC's
copy needs additional disk space and briefly pauses the JVM. The default 90-second
budget may be too short for a large model; increasing it helps periodic saves but
cannot lengthen AWS's notice. Checkpoint activity perturbs timings, so record it
when studying performance. `--sync-seconds` remains a separate, mutable partial-output
facility and does not establish recoverability.
