# Allocation palettes

The question was whether finer reservation classes and a **32-class / 8-home**
byte partition improve Groups on 16 KiB and 64 KiB base pages. Native measurements
favoured **15 classes / 16 homes**, with different fresh-allocation policies for
4, 16 and 64 KiB pages. The chosen changes are implemented in
[finite.cpp](../../../orbital/src/allocation/finite.cpp) and
[group.cpp](../../../orbital/src/allocation/group.cpp).

## Selected profiles

| Native page | Fresh Group and capacities |
| --- | --- |
| 4 KiB | Existing A/B tables and selector. |
| 16 KiB | Keep A for requests ≤192 bytes with alignment <64; otherwise B with the two changed capacities below. |
| 64 KiB | B for all fresh finite Groups, including tiny requests. |

The large-page B table is:

```text
256 320 384 448 512 640 768 896 1088
1280 1536 1792 2176 2560 3072
```

It changes the old 1,024/2,048-byte reservations to 1,088/2,176 bytes.
A 2,050-byte request then reserves 2,176 rather than 2,560 bytes. A request just
below either shifted boundary can instead reserve more. This is a measured
trade-off, not a universal rounding improvement.

The finite cutoff remains **3,072 bytes**. Wider 12/48 KiB candidates were a
separate experimental axis; those request traces cannot be compared directly
with the 3 KiB baseline. Changing the cutoff would also require a matched
comparison against Windows, so this work does not select it.

Each Group still has 256 identities. A byte uses four class bits and four slot
bits; 15 classes leave `0xff` free without losing a usable Home. Class starts and
capacity strides provide at least `gcd(capacity, 64)` natural alignment. Explicit
alignment is checked independently. Capacity may differ from requested length,
including when an existing Group is reused.

## Why these choices

The [geometry screen](geometry.md) initially suggested retaining A everywhere
and adding a separate upper palette. Actual time and lifetime traces changed
that recommendation:

- **64 KiB tiny requests:** mixed A classes touch more expensive class runs.
  A 256-byte minimum lets 256 identities fill one page. B-only churn fell from
  about 157 to 114 ns and accessible backing from 1.06 to 0.31 MiB.
- **16 KiB tiny requests:** B-only churn rose from about 377 to 627 ns despite
  using less backing. Keep A here.
- **Clustered 2 KiB requests:** the shifted B table reduced settled backing
  from 3.44 to 2.14 MiB on 16 KiB and 4.56 to 2.31 MiB on 64 KiB. Broad mixtures
  can pay more backing or time: the optimized mixed traces were 1–2% slower,
  and the 64 KiB log trace retained 8% more backing.
- **4 KiB:** the shifted B table increased broad-mixture backing and time.
  Keep the original table.
- **32 classes:** both even and geometric challengers increased Group searches
  and backing in broad mixtures. Splitting unchanged A/B tables into smaller
  identity grids was also worse. Reduced rounding did not repay those costs.

These figures are three-repetition optimized geometry measurements, with 1,024
live allocations and four replacement rounds. Each successor is acquired while
its predecessor remains live. The 16 KiB host is macOS/ARM and unpinned; the
4 KiB and 64 KiB hosts are CPU-pinned Linux Zen 5 and Graviton. Comparisons are
within each host, never pooled across them. The native controller omits Loom,
the four-entry ready cache and asynchronous preparation. Its configurable
backend was a production candidate when measured; [the frozen copy](backend/README.md)
keeps those alternatives repeatable after the production simplification.

Geometric and evenly spaced tables were generated with the same endpoints,
class count and size quantum. **Spacing and class count are independent axes.**
The full 16–3,072 range and B's 256–3,072 band are separate cases. Quantization
makes a 32-class geometric table locally linear near its minimum. Literal
16/32-class tables lose a final-class Home to `0xff`; hole-free 15/31-class
controls make that cost visible.

## Selection cost and correctness

Production uses shared exact byte-size lookup tables: A needs 192 bytes, B
needs 48, with no per-Group heap charge. Selecting once per candidate-helper call
avoids repeating selection when trying another identity grid. Its byte layout
is fixed: 15 classes and 16 homes. Typed Grid records have a constant stride;
capacity/offset arrays are compact. FiniteGroup is **320 bytes**, down from 448.

The first implementation retained configurable class counts and 8/16-home masks.
Whole-consumer comparisons against original production exposed a **5–11% warm
finite-reuse regression**, hidden by using that intermediate implementation as
the baseline. Fixing the selected layout removed its dynamic shifts, mask branches
and dependent stride loads. The configurable backend now belongs to this spike,
not the production hot path. This is why the public comparison below uses the
allocator from before the investigation, rather than just its intermediate state.

Checks cover every legal Home, binding decode, requested and natural alignment,
retained sources during growth, actual discard/protection, preparation failure,
exact control charges and budget refusal. Production `FiniteConfig` always uses
host pages. The [test-only helper](../../../orbital/test/finite_qualification.h)
exercises larger geometry on smaller-page CI hosts; complete-page protection
and discard cover the aligned reservation endpoints and host subpages. Extra
alignment address space is not mapped backing. Emulation does not qualify native
faults, TLB behavior or performance.
Production [palettes.cpp](../../../orbital/test/palettes.cpp) checks selected
profiles and budgets. Its literal charges pin current layout costs, with derivations
and actual/expected failures; correct accounting remains the contract when layouts
change. [backend_check.cpp](backend_check.cpp) checks rejected
16/32-class and custom-table alternatives separately.

## Whole-consumer result and evidence

The [current public comparison](evidence/review/public.csv) uses eight balanced
process launches per pair, eight blocks of 65,536 operations and 256 live slots,
pinned to one CPU. Checksums, retained bytes and zero-charge shutdown passed;
timed VM calls and new allocations were zero. The original allocator and current
capture ran sequentially on the same host; platform results are not pooled.

| Warm operation | Zen 5, 4 KiB: original → current | Graviton, 64 KiB: original → current |
| --- | ---: | ---: |
| Finite control, full payload | 78.4 → 80.0 ns | 133.9 → 134.4 ns |
| Finite control, 64-byte touch | 71.6 → 72.5 ns | 123.93 → 123.98 ns |
| Finite fallback, full payload | 152.3 → 151.3 ns | 201.4 → 203.8 ns |
| Finite fallback, 64-byte touch | 100.2 → 103.0 ns | 137.5 → 139.2 ns |
| Window control, full payload | 336.9 → 337.1 ns | 353.8 → 353.9 ns |
| Window control, 64-byte touch | 33.9 → 33.8 ns | 63.9 → 64.1 ns |

These are block means, not point-latency distributions. This supports the
simplification and roughly preserved warm reuse, not a blanket performance win.
The earlier [three-trial capture](evidence/native/public.csv) showed an **8.8%**
Graviton short-touch loss, with two processes near 135 ns and one near 124 ns.
The current eight-process capture does not reproduce it. This is not proof of
its cause or elimination: [process medians](evidence/review/processes.csv) retain
slow processes in the probe and unchanged Window controls. Do not explain this
timing variation as a rounding cost; the short control still reserves 320 bytes.

**Keep the shifted B policy despite the 64 KiB fallback's 5.3% backing cost.**
That boundary population touches 40 rather than 38 pages per Group: the shifted
1,088/2,176-byte runs each add one page. Three Groups therefore add exactly
`3 × 2 × 65,536 = 393,216` bytes (7,471,104 → 7,864,320). The large clustered/tiny
gains support the simple selected policy; this trace and broad/log mixtures remain
countercases. This choice does not establish an optimal table for every population.

A [cached-class probe](platforms/probe-ready-class.sh) removes the ready hit's
redundant lookup using existing hint padding. It gave no consistent benefit:
Zen 5 fallback became about 2% slower and Graviton remained roughly flat. Leave
it out of production. Both pairs and all process summaries remain in the current
export; [context](evidence/review/context.json) identifies sources, binaries,
native checks and raw S3 recovery. Source hashes match the current runtime;
native 4/64 KiB Orbital/Loom checks and native 16 KiB Release/ASan geometry passed.

[comparisons.csv](evidence/native/comparisons.csv) retains all candidate churn
medians plus mixed-labelled direct and growth controls. [Context](evidence/native/context.json)
identifies every input summary row, measured source/binary, repetitions and raw
S3 archive. [Qualification](evidence/native/qualification.json) records native
4/64 KiB Orbital/Loom checks, native 16 KiB production geometry checks, sanitizer
coverage and AWS cleanup. Bulky captures, compiler output and raw repetitions
remain in S3/ignored storage; [retain-native.py](retain-native.py) reproduces the
selection without replacing historical geometry context.

## Repeat the investigation

From the repository root on Linux, prefixing with `orb -m ubuntu` on the Mac:

```sh
# Real host pages, pinned experimental geometry timing and candidate matrix.
bash workbench/spikes/allocation-palettes/platforms/run-performance.sh
# Whole Orbital/Loom runtime; PALETTE_EXPECTED_PAGE can assert the host profile.
bash workbench/spikes/allocation-palettes/platforms/validate.sh
# Existing public Loom allocator consumer, frozen baseline versus current capture.
bash workbench/spikes/allocation-palettes/platforms/run-public.sh BASE CANDIDATE
```

`performance.py --help` selects candidates, stages and repetitions. Its default
uses the same class lookup optimization for all candidates; `--class-lookup 0`
and `--class-once 0` isolate selection costs. Timing output reports means and
batch-mean percentiles, **not individual-operation latency percentiles**.

For the native 16 KiB experimental matrix on macOS, use
[build-macos.sh](platforms/build-macos.sh) with pinned Clang 21.1.8 and pass its
`toolchain.json` to `performance.py --build-context`. This path deliberately
builds only the experimental backend; it does not qualify the Linux runtime on Darwin.
[qualify-macos.sh](platforms/qualify-macos.sh) instead runs the production finite
geometry checks in Release and ASan/UBSan on actual 16 KiB pages.
[prepare-64k.sh](platforms/prepare-64k.sh) documents a disposable Ubuntu native
64 KiB worker image; delete its temporary AMI and snapshot after a campaign.

The earlier logical screen remains repeatable through [run.sh](run.sh).
Its retained [geometry evidence](evidence/context.json) predates the native
selection; its backing counts are not RSS or timings. Keep historical context
with those measurements rather than replacing it with current-source hashes.
