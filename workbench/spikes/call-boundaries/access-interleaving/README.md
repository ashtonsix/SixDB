# Selected access interleaving

Reusable selected-access state can stay behind the existing `Frame*` while
independent Ikea operations overlap. This bounded experiment uses the current
TuplePack `native_reader<64>`, actual Loom Ring/Task/Signal APIs, actual Orbital
Allocation/Hold, and Loom's implemented abstract demand policy. It adds no
runtime interface or scheduling/cache policy.

Objects and version-aware Representation selection remain prototypes. Logical
version, immutable interpretation, publication and semantic equivalence here are
**fixture scaffolding**. A stable `Selected` object owns layout, prepared reader,
source View, bound operation and physical Hold. The native reader borrows that
descriptor closure; retaining only bytes would leave dangling plan/source pointers.
Each Frame retains the selected closure once outside native work. Callbacks carry
the existing pointer, cursor, remaining count, checksum and native vector values;
they add no selected-version or descriptor arguments.

## Work and matching

Every visit decodes an actual 64-byte TuplePack row, ORs its packet into a native
accumulator, updates an order-sensitive checksum, and derives its next row from
the decoded data. No loop counter supplies the next payload address. An
independent formula oracle reconstructs logical values, traversal, checksum and
packet output without reading encoded storage or calling Ikea.

Scalar execution completes requests serially and has active width one. Manual
widths 1/2/4/8 place independent native reads before dependent cursor/hint updates.
Ring controls use the same fixed groups and active widths, with no overflow in
timings. Ragged lanes remain vacant until the group drains in both interleaved
controls; neither gets a replacement-admission advantage. Partial final groups
do the same work. Retained context, qualification and Ring allocation are outside
timing; frame reset, initial hints, admission/entry and full drain are included.

Hinted controls bind one abstract irregular 64-byte demand outside the loop. The
current policy maps it to one L1 hint at the initial/next selected address. All
three controls have the same logical hint opportunities. Unhinted controls use
the empty demand. `hint_requests` counts opportunities, never cache hits or MSHRs.
No selected profile or pressure changes during these comparisons.

## Correctness

Native and full ASan/UBSan with always-use-after-return checks pass:

- 96 matched cases: small/larger row populations, two encodings, hints on/off,
  four widths and three execution paths, with 19 ragged requests per case.
- Partially progress old selected Frames, wait on an actual lane Signal, release
  their Allocation owner, publish a newer fixture and an equivalent differently
  encoded fixture, pulse/retry without readiness, then finish against the old
  interpretation. The Hold and full descriptor closure survive the wait.
- Detach a waiting parent separately from removing its active/overflow Ring
  borrowers. Destroy the cancelled closure before surviving work resumes; no
  cancelled result is published. Exact final Ring visits are 53, completions 3
  and abandonments 3. Frame `visits` is a completed-result field, not a partial
  visit counter.
- Close Ring storage and end logical interest while an escaped physical Hold
  remains. Worker shutdown still waits. Foreign Hold release and owner-lane
  service settle the actual physical drain.

## Local timing result

Seven sequential repetitions, pinned to CPU 0, on the local Apple AArch64
OrbStack VM, Clang 21.1.8 `-O3`, baseline ISA/generic tuning. Each sample has 16
precomputed batches of 128 ragged requests, with different starts across batches.
The set is reused across samples and modes. The 64 MiB allocation touches
370,951 distinct logical rows (about 22.6 MiB of payload) in 470,016 reads per
sample. Allocation size alone establishes no cache-residency tier.

Median reciprocal throughput, ns per native read:

| Population/control | Serial | Manual | Ring |
| --- | ---: | ---: | ---: |
| 32 KiB, width 2, hints | 9.10 | 7.55 | 10.54 |
| 32 KiB, width 8, hints | 9.18 | 8.93 | 9.78 |
| 64 MiB, width 4, no hints | 131.65 | 45.54 | 51.47 |
| 64 MiB, width 8, no hints | 127.56 | 45.83 | 50.34 |
| 64 MiB, width 4, hints | 127.50 | 40.80 | 39.68 |
| 64 MiB, width 8, hints | 134.38 | 28.59 | 24.74 |

The large hinted Ring is about 5.4× the corresponding serial throughput and
2.0× its unhinted width-8 control. Direct work wins the hot case. This supports
using the existing separate paths and preserving the narrow native boundary.
It neither chooses a universal width nor proves V2/Zen5 performance. VM noise
is visible: the large width-8 hinted Ring spans 24.19–40.07 ns/read; serial spans
124.27–276.33. [The retained summary](evidence/20261009-summary.json) keeps all
48 controls and seven samples, rather than only the favorable table rows.
No PMU stalls, IPC, hardware-miss occupancy, request-latency distribution or
confidence intervals were measured. Earlier local samples overlapping our
sanitizer build are excluded.

## Dependency depth and code footprint

Each chain still has selected-descriptor/source loads, row-address formation,
payload loads, native decode and decoded successor before its next payload
address. Interleaving supplies independent chains; it does not shorten that
chain or change snapshot meaning. Control flow and register pressure are separate
from measured reciprocal throughput.

Actual single-family, compile-only object sizes:

| Body | V2 bytes | Zen5 bytes |
| --- | ---: | ---: |
| Scalar | 2,928 | 2,926 |
| Manual 1 | 2,876 | 2,977 |
| Manual 2 | 5,908 | 5,833 |
| Manual 4 | 12,048 | 11,258 |
| Manual 8 | 24,248 | 22,730 |
| Ring callback | 2,268 | 2,078 |

The callback has no ordinary call or vector-stack access on either target.
V2 has no callback stack access; Zen5 has scalar spill/reload sites and frame
push/pop sites. V2's outlined recurrent yield is also call/stack-free. The
AArch64 entry bridge saves registers once; retirement/overflow promotion is
reported separately. These costs are not silently attributed to every callback.

Manual width 8 has 166 explicit vector-stack instruction sites on V2. The exact
local timing object also grows to 24,820 bytes and has 141 such sites, while its
2,312-byte callback has none. Counts enumerate the union of paths, including
all inline hint alternatives and ABI saves; they are not dynamic spill counts,
cycles, or proof of the cause of a timing difference. Zen5's wide manual body
has scalar stack traffic but no explicit vector-stack sites. Tooling owns the
separate single-operation fusion and llvm-mca investigation.

## Reproduce

Run on Linux; prefix commands with `orb -m ubuntu` from the Mac. Codegen uses
the local AArch64 headers and installed x86 cross sysroot; all transitive inputs
and exact compiler commands are hashed in its receipt.

```sh
cmake -S . -B build/loom-access-interleaving-local/native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=call-boundaries
cmake --build build/loom-access-interleaving-local/native \
  --target call_boundary_access_interleaving -j2
build/loom-access-interleaving-local/native/workbench/spikes/call-boundaries/access-interleaving/call_boundary_access_interleaving check
taskset -c 0 build/loom-access-interleaving-local/native/workbench/spikes/call-boundaries/access-interleaving/call_boundary_access_interleaving bench 512 128 7
taskset -c 0 build/loom-access-interleaving-local/native/workbench/spikes/call-boundaries/access-interleaving/call_boundary_access_interleaving bench 1048576 512 7
python3 workbench/spikes/call-boundaries/access-interleaving/codegen.py \
  --output build/loom-access-interleaving-local/codegen-report
```

For the lifetime variant, configure a separate `RelWithDebInfo` directory with
`-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fsanitize-address-use-after-return=always -fno-omit-frame-pointer'`
and `-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'`. Build the same target,
then run `check` with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`.

Selected raw runs, binaries, static disassembly and full dependency receipts stay
in ignored `build/loom-access-interleaving-20261009/`. `analyze.py` combines its
`check.json`, `sanitize-check.json`, `selected-{small,large}.jsonl` and
`codegen-report/{summary,receipt}.json` into the compact retained summary. Source
hashes were checked after the runs. Native source association is provisional:
there is no build-time identity for every timed-binary input. V2/Zen5 codegen has
its own verified transitive input manifests. This bounded experiment did not
create a frozen whole-repository capture or archive. The result closes the first executable
uncertainty; actual Objects/View implementation must establish the selected-state
and interpretation guarantees supplied by this fixture.
