# Caller-owned private state control

`private-state.patch` is a separate treatment against
`e9926e13f1e85c24e9b70ee20cd92267ba995685`; the live composed baseline is unchanged.
Patch SHA-256: `f8e785135330fd08014f163715b1fbdc68104cbd64142ef28867a6090eaf7d18`.

Private Parts are constructed, cloned and converted directly in fresh,
nonmovable caller-owned state; these calls return `Result<void>`. A failed call
can leave partial allocations in that state, which the caller discards through
RAII. Update dispatch uses one-entry scratch for one offered request and
64-entry scratch otherwise. Capture still establishes Holds then releases
Allocations. Admission, lookup, duplicate ordering, atomic multi-Object
publication and output/refusal rules retain their existing implementations.

Native qualification passed 17,968 independent comparisons, 24 refusal checks,
wide-Window accounting, and a partial four-Part allocation failure with actual
cleanup. The existing retained-cut, conversion, delayed-adoption and teardown
checks are included. Receipts and private source/build live under
`build/orbital-mvcc/composed/private-state/` (`qualification.json`, `checks.jsonl`,
`codegen.json`). No benchmark clocks or new sanitizer campaign ran.

Existing AArch64 Clang 21.1.8 `-O3`, generic-tune objects give these individual
frames, including saved registers; they are not whole-path stack peaks:

| Function | Baseline bytes | Control bytes |
| --- | ---: | ---: |
| Complete scalar update | 3,632 | 528 |
| Complete batch update | 3,632 | 3,616 |
| Private allocation / clone | 464 / 384 | 176 / 96 |
| Private construction / conversion | 368 / 416 | 80 / 128 |

The eight external Allocation move-constructor calls across allocation/clone
returns disappear. Separate scalar/batch instantiations add text: the two update
bodies total 3,124 B versus 1,960 B for `Inline` (`Directory`: 3,092 versus
1,948 B), excluding callees and caller dispatch.
This is a codegen observation, not a speedup. PrivateParts remains 264 B;
scalar/batch arrays are 48/3,072 B. The existing performance field
`write_caller_arrays_bytes` still reports the batch maximum; the control check
adds `scalar_write_arrays`. Neither field measures peak stack.

For the next paired capture, use an isolated copy of Tooling's released composed
driver with these four composed files matching the base. From that source root,
apply the patch, then run the same host/CPU/ISA profile as its untreated pair.
The following matches the current EPYC quick screen; it has not been run here:

```sh
git apply --check workbench/spikes/orbital-mvcc/metadata/composed/controls/private-state/private-state.patch
git apply workbench/spikes/orbital-mvcc/metadata/composed/controls/private-state/private-state.patch
python3 workbench/spikes/orbital-mvcc/performance/run.py \
  --output "$SIXDB_RESULTS/private-state" \
  --workspace build/workspaces/orbital-mvcc-private-state \
  --profile composed-quick --repetitions 2 --cpu "$SIXDB_CPU" \
  --jobs 1 --march x86-64-v3 --tune zen5
```

For native correctness alone, configure with `-DSIXDB_SPIKES=orbital-mvcc`, build
`orbital_mvcc_composed_check`, then run that CTest target. Both native Store
layouts and both initial physical representations are exercised.
