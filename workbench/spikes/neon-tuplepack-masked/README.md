# NEON sparse TuplePack packets

Why is a half-selected 64-row TuplePack packet of one-byte tuples 4–7× slower
than a full one on Neoverse, when AVX-512 runs both at the same speed?
Promoted to Ikea on 2026-10-22.

## Starting point

- The [six-CPU snapshot](../ikea-cpu-comparison/README.md) has
  `packets/64/one_byte/half/sum` at 234 ns on V3 versus 37 ns on Zen 5 (6.4×),
  and `half/update/body` at 8.7×, while the `all` variants differ by 1.3–1.5×.
- The [batching study](../tuple-layout/batching/README.md) retained "visit only
  active rows; AVX-512 can use masked transfers for tight windows" for sparse
  packets. AVX-512 `gather_bound` loads a contiguous stride-equals-grain packet
  with one masked load from the first active row. Other targets fell through to
  `gather_grain`, which assembles 64 one-byte rows with a test and an insert each.
- The same study keeps checked mutation's per-row journal spans visible; this
  change does not touch them or the scatter side.

## Change

For 32- and 64-row packets whose rows are contiguous with stride equal to the
grain, NEON loads the window from the first to the last active row with the
existing bounded `load_unit`, moves it into place with four `TBL4` lookups and
clears inactive rows with the row mask. Rows between the first and last active
one lie in the common strided allocation that the stride hint already promises
the AVX-512 path; nothing outside that window is read. Inactive bytes inside it
are read and discarded, which the masked AVX-512 load avoids.

Applying the path to every packet size ([window.patch](window.patch)) gave the
same targeted wins but reproducibly slowed unrelated 4- and 8-row cases on V2
(`scan/packet4/64` 0.86–0.89×, `packets/8/one_byte/half/sum` 0.91×) through
their callers' code generation. Those packets gather at most eight words, so
the promoted form ([window32.patch](window32.patch)) is limited to 32/64 rows.
Both patches are records against the pre-promotion source;
[rowgather.patch](rowgather.patch) restores per-row gathers in current source.

## Results

Maintained TuplePack benchmark (61 cases), current versus candidate, two
order-reversed trials × five repetitions, pinned, Clang 21.1.8:

| Case | V2 / c8g | V3 / c9g | V3 repeat |
| --- | ---: | ---: | ---: |
| `packets/64/one_byte/half/sum` | 3.27 | 2.61 | 2.60 |
| `.../half/update/body` | 2.24 | 1.81 | 1.81 |
| `.../half/update/cps` | 1.57 | 1.42 | 1.41 |
| `.../half/update/ordinary` | 1.37 | 1.22 | 1.26 |

V3 half-selected sums go from 234 to 90 ns (Zen 5: 37 ns); update/body from
730 to 404 ns. Other cases move between 0.94× and 1.07×, in different cases on each run
([V2](evidence/window32-c8g-neoverse-v2/summary.txt),
[V3](evidence/window32-c9g-neoverse-v3/summary.txt),
[V3 repeat](evidence/window32-repeat-c9g-neoverse-v3/summary.txt)). An A/A
control ([control.patch](control.patch), comment only) on V3 shows the same
band from build placement alone: point/2 and point/4 writes 0.94×
([control](evidence/control-c9g-neoverse-v3/summary.txt)). For Rows < 32 the
compiled path is unchanged.

All seven TuplePack checks and the filter check pass at full test scope in each
candidate build; promotion also ran `ikea_validate` under both ARM tunes and the
same checks under ASan/UBSan ([graduation](evidence/graduation-c8g-neoverse-v2/)).

## Reproduce

```sh
python3 workbench/tools/worker.py run workbench/spikes/neon-tuplepack-masked/cloud.sh \
  --machine neoverse-v2 --instance-type c8g.4xlarge --env JOBS=12
```

`cloud.sh` compares current source with `PATCH` applied (default
`rowgather.patch`); `FILTER` selects benchmark cases. [graduation.sh](graduation.sh)
validates the current source. Run `worker.py` inside the Linux VM; 16 GiB
workers run out of memory building the checks.

## Open questions

- Sparse scatters still store active rows one at a time. NEON has no masked
  store; a read-modify-write would write inactive rows' bytes, which owners
  have not agreed to. SVE predicated stores would need an SVE build.
- Checked updates still pay a journal span per selected row.
