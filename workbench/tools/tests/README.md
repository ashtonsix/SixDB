# Checks for shared helpers

From the repository root on Linux (prefix with `orb -m ubuntu` from the Mac):

```sh
python3 workbench/tools/check.py --list
python3 workbench/tools/check.py worker
python3 workbench/tools/check.py worker_reuse
```

Choose the check for the helper being changed. The launcher runs the existing
standalone script with its imports available; extra arguments pass through,
for example `check.py worker -v`. It does not allocate cloud workers.

`build`, `dev`, `experiment`, `compile_probe` and `replay` use the pinned Linux toolchain in
disposable fixtures. `build` also uses LLVM objcopy/strip and `readelf`;
`dev` uses clangd-21; `replay` uses taskset and GNU time. The others use offline
inputs and fake cloud clients. `--list` derives descriptions from the scripts themselves.
`results` also needs Node for the viewer's data-handling checks; it can run on
macOS directly as well as Linux, and does not launch a browser.

Research-facing [navigation checks](../check_docs.py) and
[editor diagnostics](../editors.md) remain ordinary tools. Dataset-specific
checks live with their adapters, such as [keyset windows](../../datasets/keyset-windows.md).

The [worker smoke script](../worker-smoke.sh) and
[reuse probe](../worker-reuse-smoke.sh) are optional live checks. The
[group smoke](group-smoke.sh) checks private peer readiness and cached setup across
cohorts; its [group guide](../worker-groups.md) gives configuration. Historical
[lifecycle](worker-validation.json), [reuse](worker-reuse-validation.json) and
[group reuse](worker-group-validation.json) receipts record their original tested conditions, not current coverage claims.
