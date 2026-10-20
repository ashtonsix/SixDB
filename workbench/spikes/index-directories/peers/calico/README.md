# Calico current-call M3 control

This small executable calls the real `calico::runtime` prepared API. It has no
Store adapter. The schema is the logical key and eight non-null `BIGINT` material
fields; Engine's process locators, directory metadata and padding are not inputs
the peer must store. It uses Calico's transactional descriptor preset and native
transient memfd serving, following its YCSB posture.

Build separately from the directory experiment, using an existing Calico source
checkout or [captured source](../../../../tools/README.md#captured-experiment-runs).
This leaves Calico's source unchanged and builds only the executable's actual
library dependencies:

```sh
cmake -S workbench/spikes/index-directories/peers/calico \
  -B build/clang/index-directory-calico -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-21 -DCMAKE_C_COMPILER=clang-21 \
  -DCMAKE_BUILD_TYPE=Release -DCALICO_SOURCE=/home/ashtonsix/calico \
  -DCALICO_NATIVE=ON -DCALICO_COMPILE_JOBS=1
cmake --build build/clang/index-directory-calico --target index_directory_calico -j1
build/clang/index-directory-calico/index_directory_calico \
  8192 262144 4096 dense buffer-first
```

Prefix Linux commands with `orb -m ubuntu` from the Mac. Record the actual Calico
revision/capture, compiler, flags, host and placement alongside output. Initial
qualification uses Calico `ac83c82b9a0c8d76bea92829e471ebc0d98b1978`.
`sparse` uses the same 1/16 key occupancy as the directory driver. Reverse sink
order with `visitor-first`; choose the faster valid numeric sink for a matched
comparison, rather than forcing an extra raw 64-byte repack or printer result.
An optional final `fieldwise` argument selects the native field-wise payload
with its required `exact_min` anchor; `recordwise` is the default transactional
posture with `zero` anchor. These are physical controls for the same logical
table, not different workloads.

Qualification exposed a Calico limitation at the stated revision: a narrow
numeric projection from the eight-field record-wise row refuses with
`point SELECT f-array cursor open failed`. Both sinks are probed. Only that
specific refusal permits the explicit fallback to a full-eight-field prepared
SELECT while consuming just `v0`; stderr records the refusal and actual projected
field count. Field-wise storage supports the true single-field projection.
Calico's implementation is unchanged. Keep these layouts labeled and select the
faster valid control for each standalone operation; do not describe the fallback
as a one-field projection. A mixed workload must use one actual policy throughout
its trace, rather than combining minima from incompatible configurations.

Untimed native wire/COPY setup, preparation and exact field oracles surround the
intervals. Timed reads include one complete current prepared execute and direct
numeric consumption, with the same compact request-key array,
independent/dependent streams and checksums as `src/driver.cpp`. Both
caller-owned numeric buffer and scoped visitor are
available. Timed B1 assignment changes only `v3` through one prepared UPDATE,
including its native result destruction; it needs no separate SELECT or forced
material clone. All eight fields are checked afterward, including key association
and unchanged fields. Qualification also checks unsigned zero/max and absent keys.

The cohort column is **1**, one current call. Calico's public Runtime does not
expose retained old-cut snapshots across publication, late first old reads,
atomic 64-key prepared updates, or persistent prepared mutation. Those cells are
unavailable through this API. A reusable result buffer is not a retained database
snapshot. These transient current-call numbers cannot establish those stronger
guarantees, durability, concurrent histories, cold behavior or complete Engine
performance. Compare eligible cells on the same machine and condition; do not
splice this executable's timings from another host into a directory ratio.
