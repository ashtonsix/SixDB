# Tools

Use helpers independently as the study needs them. Commands below run from the
repository root on Linux; prefix with `orb -m ubuntu` from the Mac.

| Task | Tool and starting point |
| --- | --- |
| Run a script on EC2 | [worker.py](worker.py) · [worker guide](workers.md) |
| Record a local experiment | [experiment.py](experiment.py) · [aggregate runner](../spikes/aggregate-maintenance/run.py) (timings), [regexp runner](../spikes/regexp-lowering/run.py) (counts) |
| Keep or recover selected runs | [artifacts.py](artifacts.py), [evidence.py](evidence.py) · [retention guide](artifacts.md) |
| Reuse input data | [datasets.py](datasets.py) · [catalog and examples](../datasets/README.md) |
| Activate a spike for editing | [dev.py](dev.py) · [editor setup and diagnostics](editors.md) |
| Check documentation navigation | `python3 workbench/tools/check_docs.py` · advisory local links, headings, and catalog hints |

## Captured experiment runs

`Run` in [experiment.py](experiment.py) records source snapshots, commands,
hashes, and success/failure receipts. With `workspace=...`, build from
`run.source_root` into `run.build_dir` while the live checkout remains editable.
Stable paths and unchanged source mtimes preserve Ninja reuse; runs sharing a
workspace serialize. A live editor refresh failure does not block captured runs.
Snapshots exclude `build/` and spike evidence, including tracked files.

`run.input()` records a [prepared dependency](../datasets/README.md#using-inputs-in-a-spike);
`run.compact()` selects files and an offline regeneration command for
[retention](artifacts.md#counts-and-other-evidence). Execution and analysis stay
in the study; the runners above are working examples.

## Changing a helper

A separate Git worktree lets other tasks keep using the existing tooling while
a change is exercised. `SIXDB_DATA_CACHE` can share prepared inputs across them.
Run the check relevant to the change with `python3 workbench/tools/CHECK.py`:

| Area | Check |
| --- | --- |
| Incremental builds and release packaging | [check_build.py](check_build.py), using the pinned Linux toolchain, LLVM objcopy/strip, and `readelf` |
| Captured sources and incremental workspaces | [check_experiment.py](check_experiment.py), using the pinned Linux toolchain |
| Evidence integrity and recovery | [check_artifacts.py](check_artifacts.py) |
| Dataset caches and adapters | [check_datasets.py](check_datasets.py), [check_keyset_datasets.py](check_keyset_datasets.py); the latter's `--full` resolves S3 sources |
| Editor configuration and diagnostics | [check_dev.py](check_dev.py), [check_ide.py](check_ide.py); see [prerequisites](editors.md) |
| Worker lifecycle and ownership | [check_worker.py](check_worker.py), [check_worker_reuse.py](check_worker_reuse.py); both run offline |
