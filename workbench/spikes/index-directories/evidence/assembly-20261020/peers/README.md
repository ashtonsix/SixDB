# Frozen ready-key reads: matched peers and actual memory

The staged Engine graph is faster for this read screen, but uses substantially
more resident memory at 262144 rows. Calico RECORDWISE/ZERO with the prepared
numeric visitor is the fastest tested full-row peer at both populations.
Neither result establishes complete Engine performance or C++ graduation.

| Population | Engine scalar | Engine staged | Calico row buffer | Calico visitor | LMDB native u32 get | LMDB native u32 cursor |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 8192 | 4.750 | 4.741 | 87.749 | 85.978 | 147.965 | 150.764 |
| 262144 | 38.043 | 29.338 | 152.962 | 143.905 | 391.095 | 374.649 |

Values are median ns/completed item over ready independent requests, the reciprocal
of throughput, not individual-request latency. Each item consumes all eight
64-bit values via XOR and accumulates the result; it does not serialize a 64-byte
output row. The staged Engine ratios against the fastest tested peer are about
18.14x and 4.91x. FIELDWISE/exact_min is also retained in `summary.json`; it is
slower here. Native u32 is not uniformly faster than the earlier BE4 control.

Peer ranges and all raw samples remain in `summary.json` and `timing/*/samples.csv`.
For example, the large LMDB cursor samples span 367.611--737.742 ns/item; the
outlier has not been discarded. Calico visitor spans 83.838--91.591 at 8192 and
136.213--157.654 at 262144. These are small, same-host screens at distinct times.

## Actual footprint

| Population | Engine RSS / PSS MiB | Calico recordwise RSS / PSS MiB | LMDB RSS / PSS MiB |
| --- | ---: | ---: | ---: |
| 8192 | 9.215 / 7.031 | 8.793 / 7.424 | 6.867 / 5.357 |
| 262144 | 176.613 / 174.421 | 24.758 / 23.386 | 46.080 / 44.575 |

These are warmed backend-only **untrimmed** process measurements after external
trace, setup material and client buffers are released. Useful retained backend
state and ordinary allocator caches count. Peers have two isolated process
observations each; Engine has Architecture's separate primary profile. Different
binaries have different baselines. Engine's configured-empty RSS is about
3.66/3.70 MiB; the common peer executable baseline is about 5.0 MiB. Consult
the phase distributions and increments rather than silently subtracting code.

At 262144 rows, representative Calico recordwise PSS includes about 16.141 MiB
of memfd-backed pages, 2.688 MiB anonymous and 4.544 MiB file-backed pages.
LMDB includes its 20.168 MiB resident database file and about 20.28 MiB resident
heap. `mallinfo2` reports about 20.33 MiB in-use heap in this native process;
post-measurement `malloc_trim` barely reduces residency. This is a measured
load-and-serve environment, including retained live load-era allocation, not
LMDB's theoretical minimum after closing and reopening.

The matching [LMDB 0.9.31 source](https://github.com/LMDB/lmdb/blob/LMDB_0.9.31/libraries/liblmdb/mdb.c#L1718)
recycles dirty single pages through `me_dpages` after flushing (lines3264--3272),
then frees that environment cache on close (lines4911--4927). This is consistent
with the live load-era heap and explains why glibc trimming cannot release it.
Close/reopen can release that allocation; its exact RSS reduction is unmeasured.
The measured footprint remains labeled load-and-serve.

Engine's primary large profile includes about 4 MiB of freed trace remaining
in the allocator cache. The primary measured number is retained as-is.
Its PMR requested/backing figures (27.216/42.993 MiB) are nested, and neither
its Loom mapped-byte counts nor multi-GB virtual reservation is actual RSS.
Loom's separate geometry audit identifies class rounding and page underfill
as major costs; Architecture is coordinating that work with the allocator owner.

RSS/PSS sharing categories and backing-file categories describe overlapping
properties and must not be added together. `/proc/status`, `smaps_rollup`, full
backend-only `smaps`, classification and malloc diagnostics are retained per
process. Calico's later logical inventory labels recordwise reserved leaves
`unparsed`; it runs after the primary samples and is not used as an RSS measure.

## Remaining planning headroom

Using the faster tested full-row Calico peer P, a hypothetical 2x total-throughput
target corresponds to P/2 = 42.989/71.952 ns per completed item. Subtracting the
current staged read leaves 38.249/42.615 ns/item as **unoverlapped planning
arithmetic** for the remaining work. This is neither measured composed slack
nor a request-latency gate. Fusion, overlap, cache competition and the rest of
Engine can change that arithmetic; only a matched complete caller can settle it.

## Condition and provenance

- Linux ARM/OrbStack, guest CPU8; peer driver and profiling controller CPU0.
  No build overlapped our timed slot. All children exited and the slot was
  explicitly returned to Engine and Architecture.
- Dense native u32 domain N8192/N262144; exactly 1048576 ready flat u32 keys
  `mix(i+17)&(N-1)`. Material word j is `mix(row*17+j+71)`.
  Exact signatures are 7213914273762396471 / 7755292558270058147.
- LMDB 0.9.31, actual named DB flag MDB_INTEGERKEY=8, native aligned four-byte
  keys, 64-byte material. Read txn renew/reset per64 and reusable SET_KEY cursor
  are timed. Same environment loads then serves, map1GiB, no durability claim.
- Calico ac83c82b9a0c8d76bea92829e471ebc0d98b1978, existing native Release
  archives, real prepared full-eight-field SELECT. Numeric reusable buffer and
  visitor, no string conversion or byte repacking. Transactional preset with
  both RECORDWISE/ZERO and FIELDWISE/exact_min. Frozen data makes current-call
  reads useful comparisons; no retained-old-cut or atomic64 equivalence is claimed.
- Six functional processes on CPU0 check every stored row through both APIs,
  absent max-key and output signatures. Twelve isolated CPU8 processes produce
  48 timed rows: two trials per mode, reverse mode order, backend process order
  reversed. Each cell has four intervals, all retained.
- Backend setup, independent expected calculation, full-table oracle and warmup
  are outside read clocks. All useful fetches, numeric consumption and native
  transaction renew/reset are inside. No result-dependent request feedback.

`screen.cpp`, `run.py`, `build.json`, `inputs.json`, `context.json` and
`timing/receipt.json` contain exact commands, hashes, native DB statistics,
placements and phase captures. `analyze.py` reproduces `summary.json`.
Architecture owns the Engine evidence in
`build/architecture-index-throughput-20261020/{summary.json,memory-summary.json}`.
The separate `../lmdb-native-u32/` recipe qualification also covers writes,
repeated private updates, commits, aborts and old/fresh readers; none of those
write timings are implied here.

Producer full-cycle, retained history, coldness/DRAM, p99, native Zen5 and composed
Engine/C++ comparisons remain separate open work. Selected evidence belongs in
Architecture/Engine's combined retained study; this ignored workspace avoids
another maintained benchmark driver or a duplicate bulky archive.
