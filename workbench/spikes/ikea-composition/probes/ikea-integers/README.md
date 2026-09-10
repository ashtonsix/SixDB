# Packed integer locality and composition

Experimental evidence owned by the [composition investigation](../../README.md).
Use its [current design](../../design.md) for authoring direction.

Can packed integers provide strong cold random access and bulk throughput while
remaining useful parts of larger blocks? This spike tests both requirements
against [Calico bytepack](../../../../../../calico/workbench/prototypes/bytepack/README.md)
and the [Ikea composition investigation](../../design.md).
Opened 2026-09-09; consolidated 2026-09-10. No production interface is selected.

## Where to start

| Question | Reading and implementation |
| --- | --- |
| What bytes do LocalPack and ScanPack store? | [Format contract](formats.md), then [data definitions](formats.h) |
| How fast are the current narrow kernels, and where do they lose? | [Measurements](measurements.md); [benchmark contract and commands](bench.md) |
| What does an actual authored composition look like? | [12-bit read/filter/sum](composition/README.md), starting at [authoring.h](composition/authoring.h) |
| Do adjacent wider bodies improve random access? | [Width-56 results](wide56/README.md#hardware-findings-2026-09-10); [native fragments](wide56/kernels.h) |
| Which locality promises are physically possible through 64 bits? | [Exact geometry](locality/README.md); [wider-body extension](wider-bodies.md) |
| Do the emitted instructions obey those footprints? | [Narrow/composition access audit](access-audit.md); [width-56 accesses](wide56/README.md#data-and-native-work) |
| Which capture supports a claim, including discarded alternatives? | [Evidence map and recovery](evidence/README.md) |

## Coverage and current conclusions

| Exercise | Implemented and checked | Finding and remaining limit |
| --- | --- | --- |
| **LocalPack / ScanPack, k=1..7** | Exact encode/decode, point and aligned get16; AVX2, AVX-512, NEON/SVE2 paths | Both formats meet payload locality. Same-wire readers favour different access regimes. Continuous Scan5/7 still have material Zen bulk gaps. |
| **12-bit body + tail** | Three placements; shared authored read/filter/sum; inline and independent-TU CPS | Child substitution, parent placement and native value reuse work in one five-operation mapping. CPS costs 16–57% here, too steep as a blanket solution. |
| **56-bit body, head zero** | Seven-byte bodies; point/get16, encode/decode and native sum; AVX2, AVX-512 BW+VBMI and NEON | At 448 MiB, independent point reads improve 1.43–1.63× and get16 1.42–2.63× over native planes. Bulk beats Calico; Zen encoding still trails the tighter plane encoder. |
| **Remaining widths through 64** | Exhaustive stored-bit geometry, head choices and optional padding analysis | Point locality is feasible throughout. Dense aligned-get16 locality has a proven limit. Native codec coverage beyond the cases above is open. |

Hardware timings come from Zen 5, Granite Rapids and Neoverse V2. AVX2/QEMU
and development-host sanitizer runs establish correctness, not target speed.
Required payload bytes, actual instruction accesses and whole-operation traffic
are distinct. Large allocations and warm passes do not certify cache residence.

The accepted locality contract is at most two **adjacent** 64-byte payload lines
per point, and per aligned group of sixteen wherever physically possible, at
every reachable tile phase. Deliberately separated filter heads are the stated
exception. The [format contract](formats.md#locality-and-admission) gives the
scope, head/body/tail split and trusted-kernel preconditions.

The [review direction](composition/README.md#direction-after-review-2026-09-10)
is curated inlining within regions and freer substitution at coarser seams:
`A × B × C × D → A × B + C × D`. Storage packets, native values and public
request grains need not coincide. Width 56 consumes native 8/4/2-value fragments
directly; its optional 32-value encoder region improves encoding by 6–9% over
the default without changing the wire. Mismatched grains, multi-source handoffs
and general substitution remain open. See the independent
[granularity investigation](../../operation-granularity.md).

## Code and execution map

The root holds the narrow family: [local.h](local.h), [scan.h](scan.h) and
[native byte helpers](native_bytes.h) implement immediate native work;
[codec.h](codec.h)/[codec.cpp](codec.cpp) expose materializing comparison
endpoints. [reference.cpp](reference.cpp) is the independent wire oracle,
[check.cpp](check.cpp) checks accesses and values, and [prior.cpp](prior.cpp)
adapts the [pinned prior](prior-source.json). Data repetition in `Tiles<Child,N>`
does not require a TU or call per repetition.

The [composition directory](composition/README.md) owns authoring, placement,
preparation and the independent read/filter/sum TUs. The [wide56 directory](wide56/README.md)
owns the wider-body experiment and its u64 controls. Neither is a general
all-width framework. [scan_pairs.h](scan_pairs.h) separately tests 64-byte
AVX-512 execution over unchanged 32-byte stripes; its [mixed results](measurements.md#paired-stripe-execution)
do not select a new wire or automatic execution policy.

From Linux, or with `orb -m ubuntu` prefixed in this macOS workspace:

```sh
python3 workbench/spikes/ikea-composition/probes/ikea-integers/run.py --sanitize --check-only
python3 workbench/spikes/ikea-composition/probes/ikea-integers/wide56/run.py --sanitize --check-only
```

The first runner checks narrow kernels and the 12-bit composition; the second
checks width 56 and its comparators. [Narrow running instructions](bench.md#running)
and [width-56 reproduction](wide56/README.md#validation-and-reproduction) describe
focused runs and benchmark scope. [Evidence verification](evidence/README.md#verify-report-and-recover)
is offline and does not rerun benchmarks.

## Open choices

[The next-probe discussion](next-probe.md) compares heterogeneous bitset metadata,
PFoR-like reconstruction, and complete width/performance coverage. It records a
recommendation that led to the new [heterogeneous spike](../ikea-heterogeneous/README.md).
Its `run.py --check-only` checks both live providers and their combined caller.
Known performance gaps remain unfinished. Reflection for block laws, filter and
aggregate maintenance, and MVCC write effects remain unimplemented.
