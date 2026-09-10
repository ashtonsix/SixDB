# Whole-bitset analysis and masked algebra

Completed with the spike's [closing assessment](../closing.md), 2026-09-10.

This extension asks whether an 8,192-byte bitset is worth converting into 256
headless Bec256 bodies, and how two such bitsets compose under union or
intersection with a caller-supplied 256-bit slice mask. The operands may each
be plain or BEC with any of the [three metadata layouts](../composition.md).
The result is plain. Encoding a result remains a separate operation.

Read [analyser findings](analyser-findings.md) for the storage decision,
[measurements](measurements.md) for cost and native granularity, and
[boundary inspection](../notes/operations-boundaries.md) for the actual machine
code. These are spike observations, not a selected Engine policy or production
interface.

## Two different meanings of “worth it”

[analyse.h](../analyse.h) estimates the sum of 256 BEC body sizes using the
frozen cheap or quadrant model from the [bitset predictor](../../ikea-blocks/predictor/README.md).
It offers a full scan and a fixed 32-slice sample. Predictions are scalar byte
estimates; neither is a bound. The full cheap scan extracts features from pairs
of slices without imposing that grain on the caller's one-window result.

[storage_decision.h](../storage_decision.h) adds the chosen metadata layout,
the single 64-byte readable suffix, and optionally allocation rounding. Its
default models this spike's actual 64-byte-aligned allocation. Packed metadata
costs 512 bytes, direct metadata 1,024. Strict savings, optionally above a caller
margin, decide the predicted byte preference. A caller requiring actual savings
must confirm the final encoded size. Even a correct byte decision does not
establish whether conversion pays back its CPU cost in a particular workload.

`encode_body_bytes` supplies a native body-emission comparison: it returns the
actual dense byte count, with no directory emission or allocation. It needs
12,096 writable bytes regardless of a prediction. BEC may expand to 47 bytes
per slice; this probe has no per-slice raw escape. Empty and full slices use no
body bytes and are distinguished by the external population9 metadata.

## Selection, identity and output obligations

Mask bit `s == 1` selects original slice ordinal `s`, covering positions
`[256*s,256*(s+1))` in both operands and the output. It never means the rank
among selected slices. `PreparedAlgebra::apply` establishes the complete result:
selected slices contain the requested union/intersection; all other bits are
zero. The all-selected case avoids an unnecessary clear. `apply_selected`
instead leaves inactive output bytes untouched, exposing the cost of completing
the result separately from the selected computation.

The selected-ordinal iterator skips inactive bodies. Each compressed source
independently chooses point lookup or a retained metadata16 frame. A point
lookup still needs the lengths preceding its record in the checkpoint group;
masking those predecessors out would compute the wrong body address. ScanPack
also retains its actual stripe read footprint. Skipping an inactive BEC decode
does not promise zero reads of its bytes: an active decoder's admitted 64-byte
load window can include subsequent dense bodies.

Local Boolean identities use admitted populations: intersection with empty and
union with full skip both decodes; the other terminal cases copy or decode only
the remaining operand. Both metadata entries are currently resolved before
these shortcuts. The operation does not discover a stronger conjunction plan.
The caller establishes that both sources and the prefilter share a coordinate
domain, and that using this prefilter is valid for the enclosing operation.

## Composition exercised here

[algebra.cpp](../algebra.cpp) has three physical source families—plain/plain,
BEC/plain and BEC/BEC—times two operators. Metadata kinds and their two lookup
strategies bind independently on each input, outside those six heavy kernels.
A pair of curated LocalPack/LocalPack inline kernels supplies a control. This
avoids multiplying the full decoder by every pair of directory choices while
leaving the decoder and Boolean consumer inline together.

The unary point seam returns one scalar u32. The frame seam writes a 64-byte
native frame into caller-owned storage. That is an intentional materialising
seam, with retention and reload costs; it is not a free iterator abstraction.
Direct records remain a curated inline scalar leaf even in the factored path.
Native decoded vectors stay inside the kernel, without aggregate vector returns
or error handling at an ordinary ABI boundary.

Two decoder inputs need not mean two output slices. An isolated selected ordinal
can feed `A[s]` and `B[s]` to the native two-input BEC decoder, then combine its
halves into one result. Adjacent selected ordinals can instead decode two slices
from each source and produce two outputs. The measured grain control changes
that latter choice for BEC-containing kernels; plain pairs remain available.
This is a concrete case where logical identity and physical grain are distinct.

For example, after admitting two immutable sources, binding can choose LocalPack
point lookup on one and ScanPack metadata16 on the other:

```cpp
PreparedAlgebra operation;
auto status = prepare_algebra(left, right, output, SetOperation::intersection,
    Resolution::point, Resolution::cached16, AlgebraExecution::factored,
    operation);
// Handle status at this interface; apply only after successful preparation.
operation.apply(caller_mask);
```

`admit_plain` checks the exact 8,192-byte extent. `admit_bec` checks the actual
directory/body owner association, 256-record geometry, framing and decoder
suffix once. Binding retains those admitted owners and checks output extent
and overlap, without revalidating all bodies. The caller supplies immutable
snapshots, a disjoint mask, and common coordinates. The complete-output and
active-only writers have distinct postconditions. These concrete obligations
remain more useful here than inventing a general tag vocabulary prematurely.

## Prior work and limits

Calico's `keyset/detail/slab.h` supplied useful prior art for selected-cell work
and terminal Boolean identities. Its `keyset/segment.h` also makes the
distinction between an estimate and an exact materialised cost visible. Reading
used Calico commit `ac83c82b9a0c8d76bea92829e471ebc0d98b1978`. This probe does not
adopt its boxes, dynamic programming, cell caps or per-cell raw escape. Engine
still owns intelligent conjunction composition. No Calico timing is claimed
as a comparison for this different operation.

The third-input masks in the timing corpus only say which slices of a third
bitset are nonempty. They are candidate restrictions, not an exact third
intersection. MVCC changed-byte reporting, aggregate updates, and enclosing
row-filter mutation are still separate integration questions. The selected
writer supplies one useful effect boundary without claiming to solve them.

## Reproduce

Run from the Linux repository root; prefix with `orb -m ubuntu` on the Mac.
The runner captures all three live provider spikes and runs their checks.

```sh
python3 workbench/spikes/ikea-heterogeneous/operations/run.py --sanitize
python3 workbench/spikes/ikea-heterogeneous/operations/run.py --sanitize --grain 1
python3 workbench/spikes/ikea-heterogeneous/operations/run.py --quality
python3 workbench/spikes/ikea-heterogeneous/operations/run.py --synthetic -- --quick
python3 workbench/spikes/ikea-heterogeneous/operations/report_check.py \
  workbench/spikes/ikea-heterogeneous/evidence/operations-zen5
python3 workbench/tools/worker.py run workbench/spikes/ikea-heterogeneous/operations/cloud.sh \
  --machine zen5 --capacity on-demand --idle-seconds 0 -- --target zen5 -- --pmu
```

`--grain 1` or `--grain 2` controls adjacent output pairing for BEC-containing
kernels. `--part algebra` and `--part analyser` separate the campaigns. Inputs
reuse shared datasets; [prepare_data.py](prepare_data.py) retains exact source
lineage for coordinate-matched timing pairs. Full sweeps, binaries and captured
sources live in artifact bundles; compact repetitions and quality summaries
live in [evidence](../evidence/).
